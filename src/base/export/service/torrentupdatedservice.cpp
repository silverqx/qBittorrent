#include "torrentupdatedservice.h"

#include <QDebug>

#include "base/export/exportererror.h"
#include "base/export/exutils.h"
#include "orm/entitymanager.h"

using namespace Export;

namespace
{
    /*! Filter out non previewable torrents. */
    const auto filterPreviewableTorrents =
            [](const QVector<BitTorrent::TorrentHandle *> &torrents)
            -> TorrentHandleByInfoHashHash
    {
        TorrentHandleByInfoHashHash result;
        result.reserve(torrents.size());
        std::for_each(torrents.constBegin(), torrents.constEnd(),
                     [&result](const auto torrent)
        {
            if (ExUtils::torrentContainsPreviewableFiles(torrent))
                result.insert(torrent->hash(), torrent);
        });
        return result;
    };
}

TorrentUpdatedService::TorrentUpdatedService(EntityManager &em)
    : BaseService(em)
    , m_statusHash(StatusHash::instance())
    , m_torrentsRepository(m_em.getRepository<TorrentsRepository>())
    , m_torrentPreviewableFilesRepository(m_em.getRepository<TorrentPreviewableFilesRepository>())
{}

QList<BitTorrent::InfoHash>
TorrentUpdatedService::handleTorrentsUpdated(
        const QVector<BitTorrent::TorrentHandle *> &updatedTorrents)
{
    // Filter out non previewable torrents
    const auto previewableTorrents = filterPreviewableTorrents(updatedTorrents);
    // Nothing to update in DB
    if (previewableTorrents.size() == 0)
        return {};

    // TODO record updated torrents, if db is disconnected, so we don't lose state silverqx

    /* If the database disconnects, then the changed properties remain saved in the store
       and will be processed later, when the database connects back. */
    if (!m_em.pingDatabase())
        return {};

    // Define hashes here and pass them to the fill method that fills them.
    // I tried std::optional() / std::tuple(), nothing worked good, this is the best solution.
    TorrentsChangedHash torrentsChangedProperties;
    TorrentsFilesChangedHash torrentsFilesChangedProperties;
    // Nothing to update
    if (!fillTorrentsChangedProperties(previewableTorrents,
                                       torrentsChangedProperties,
                                       torrentsFilesChangedProperties))
        return {};

    // Use transaction to guarantee data integrity
    m_em.transaction();
    try {
        // Update torrents and previewable files with changed properties
        updateTorrentsInDb(torrentsChangedProperties, torrentsFilesChangedProperties);
        m_em.commit();
    }  catch (const ExporterError &e) {
        m_em.rollback();
        qCritical() << "Critical in handleTorrentsUpdated() :"
                    << e.what();
        return {};
    }

    return previewableTorrents.keys();
}

void TorrentUpdatedService::updateTorrentsInDb(
        const TorrentsChangedHash &torrentsChangedHash,
        const TorrentsFilesChangedHash &torrentsFilesChangedHash
) const
{
    // Count successful updates
    int successUpdates = 0;

    auto itTorrentChanged = torrentsChangedHash.constBegin();
    while (itTorrentChanged != torrentsChangedHash.constEnd()) {
        const auto torrentId = itTorrentChanged.key();
        const auto changedProperties = itTorrentChanged.value();

        ++itTorrentChanged;

        // If something unexpected happens, only currently processed torrent will be
        // rollback-ed, so savepoints are ideal for this.
        const auto savepointId = QStringLiteral("torrent_%1").arg(torrentId);
        m_em.savepoint(savepointId);
        try {
            // Torrents update
            m_torrentsRepository->updateTorrentInDb(torrentId, changedProperties);
            // Previewable files update
            if (torrentsFilesChangedHash.contains(torrentId))
#if LOG_CHANGED_TORRENTS
                m_torrentPreviewableFilesRepository->updatePreviewableFilesInDb(
                            torrentsFilesChangedHash.value(torrentId), torrentId);
#else
                m_torrentPreviewableFilesRepository->updatePreviewableFilesInDb(
                            torrentsFilesChangedHash.value(torrentId));
#endif
        } catch (const ExporterError &e) {
            m_em.rollbackToSavepoint(savepointId);
            qFatal("Critical in updateTorrentsInDb() : \"%s\"",
                   e.what());
        }

        ++successUpdates;
    }

    const auto torrentsChangedSize = torrentsChangedHash.size();
#if LOG_CHANGED_TORRENTS
    {
        // TODO add the same for files updates, to much work for now, fuck it 😂 silverqx
        const auto unsuccessUpdates = (torrentsChangedSize - successUpdates);
        if (unsuccessUpdates > 0)
            qWarning("%d torrents out of %d were not updated.",
                     unsuccessUpdates,
                     torrentsChangedSize);
    }
#endif

    if (successUpdates == 0)
        throw ExporterError(QStringLiteral("All '%1' updates was unsuccessful.")
                            .arg(torrentsChangedSize).toUtf8().constData());
}

bool TorrentUpdatedService::fillTorrentsChangedProperties(
        const TorrentHandleByInfoHashHash &torrents,
        TorrentsChangedHash &torrentsChangedProperties,
        TorrentsFilesChangedHash &torrentsFilesChangedProperties
) const
{
    // Create torrents hash keyed by torrent id, it's selected from db by torrent info hashes.
    // Needed because torrent id is needed in update query.
    // Also return actual QSqlRecords keyed by torrent id, which are needed to trace changed
    // properties.
    const auto [torrentsUpdated, torrentsInDb] =
            m_torrentsRepository->selectTorrentsByHashes(torrents);
    // Nothing to update
    if (torrentsUpdated.size() == 0) {
        qDebug() << "Selected torrents by handles count is 0, in handleTorrentsUpdated(), "
                    "this should never have happen :/";
        return false;
    }

    /* Create torrent files hash keyed by torrent id, torrent files are keyed by file index and
       populate by QSqlRecords.
       Needed to trace property changes in a torrent previewable files. */
    const auto torrentsFilesInDb =
            m_torrentPreviewableFilesRepository->selectTorrentsFilesByHandles(torrentsUpdated);

    // Find out changed properties in torrent and torrent files
    traceTorrentChangedProperties(torrentsUpdated, torrentsInDb,
                                  torrentsChangedProperties);
    traceTorrentFilesChangedProperties(torrentsUpdated, torrentsFilesInDb,
                                       torrentsFilesChangedProperties);
    // Nothing to update
    if (torrentsChangedProperties.isEmpty() && torrentsFilesChangedProperties.isEmpty())
        return false;

    return true;
}

void TorrentUpdatedService::traceTorrentChangedProperties(
        const TorrentHandleByIdHash &torrentsUpdated,
        const TorrentSqlRecordByIdHash &torrentsInDb,
        TorrentsChangedHash &torrentsChangedProperties
) const
{
    auto itTorrentsHash = torrentsUpdated.constBegin();
    while (itTorrentsHash != torrentsUpdated.constEnd()) {
        /*! Torrent sent by qBittorrent as changed. */
        const auto torrentUpdated = itTorrentsHash.value();
        const auto torrentId = itTorrentsHash.key();
        /*! Torrent from db as QSqlRecord. */
        const auto torrentDb = torrentsInDb[torrentId];
        const auto torrentChangedProperties =
                QSharedPointer<TorrentChangedProperties>::create();

        // Determine if torrent properties was changed
        auto changed = false;

        const auto recordChangedProperty =
                [&torrentChangedProperties]
                (const auto &column, auto &wasChanged,
                const auto &updatedValue, const auto &dbValue)
        {
            if (updatedValue == dbValue)
                return;
            torrentChangedProperties->insert(column, updatedValue);
            wasChanged = true;
        };

        recordChangedProperty(QStringLiteral("name"), changed,
                              torrentUpdated->name(),
                              torrentDb.value("name").toString());
        recordChangedProperty(QStringLiteral("progress"), changed,
                              ExUtils::progressString(torrentUpdated->progress()).toInt(),
                              torrentDb.value("progress").toInt());
        recordChangedProperty(QStringLiteral("eta"), changed,
                              torrentUpdated->eta(),
                              torrentDb.value("eta").toLongLong());
        recordChangedProperty(QStringLiteral("size"), changed,
                              torrentUpdated->totalSize(),
                              torrentDb.value("size").toLongLong());
        recordChangedProperty(QStringLiteral("seeds"), changed,
                              torrentUpdated->seedsCount(),
                              torrentDb.value("seeds").toInt());
        recordChangedProperty(QStringLiteral("total_seeds"), changed,
                              torrentUpdated->totalSeedsCount(),
                              torrentDb.value("total_seeds").toInt());
        recordChangedProperty(QStringLiteral("leechers"), changed,
                              torrentUpdated->leechsCount(),
                              torrentDb.value("leechers").toInt());
        recordChangedProperty(QStringLiteral("total_leechers"), changed,
                              torrentUpdated->totalLeechersCount(),
                              torrentDb.value("total_leechers").toInt());
        recordChangedProperty(QStringLiteral("remaining"), changed,
                              torrentUpdated->incompletedSize(),
                              torrentDb.value("remaining").toLongLong());
        recordChangedProperty(QStringLiteral("status"), changed,
                              (*m_statusHash)[torrentUpdated->state()].text,
                              torrentDb.value("status").toString());

        ++itTorrentsHash;

        // All properties are the same
        if (!changed)
            continue;

        torrentsChangedProperties.insert(torrentId, torrentChangedProperties);
    }
}

void TorrentUpdatedService::traceTorrentFilesChangedProperties(
        const TorrentHandleByIdHash &torrentsUpdated,
        const TorrentFileSqlRecordByIdHash &torrentsFilesInDb,
        TorrentsFilesChangedHash &torrentsFilesChangedProperties
) const
{
    // TODO consider move to anonym. namespace, because duplicate code is also in traceTorrentChangedProperties() silverqx
    const auto recordChangedProperty =
            [](const auto &torrentFileChangedProperties, const auto &column,
            auto &wasChanged, const auto &updatedValue, const auto &dbValue)
    {
        if (updatedValue == dbValue)
            return;
        torrentFileChangedProperties->insert(column, updatedValue);
        wasChanged = true;
    };

    auto itTorrentsHash = torrentsUpdated.constBegin();
    while (itTorrentsHash != torrentsUpdated.constEnd()) {
        /*! Torrent sent by qBittorrent as changed. */
        const auto torrentUpdated = itTorrentsHash.value();
        const auto torrentId = itTorrentsHash.key();
        const auto filesProgress = torrentUpdated->filesProgress();
        // Skip if something unexpected happend
        const auto filesExist = torrentsFilesInDb.contains(torrentId);
        Q_ASSERT_X(filesExist, "traceTorrentFilesChangedProperties()",
                   "no files in torrentsFilesInDb");
        if (!filesExist)
            continue;
        /*! Torrent files from db as QSqlRecords QHash ( QSharedPointer ). */
        const auto torrentFilesInDb = torrentsFilesInDb.value(torrentId);
        const auto torrentFilesChangedProperties =
                QSharedPointer<TorrentFilesChangedHash>::create();

        for (const auto &torrentFileDb : qAsConst(*torrentFilesInDb)) {
            // Determine if torrent properties was changed
            auto changed = false;
            const auto torrentFileChangedProperties =
                    QSharedPointer<TorrentFileChangedProperties>::create();

            const auto fileIndex = torrentFileDb.value("file_index").toInt();

            recordChangedProperty(torrentFileChangedProperties,
                                  QStringLiteral("filepath"), changed,
                                  torrentUpdated->filePath(fileIndex),
                                  torrentFileDb.value("filepath").toString());
            recordChangedProperty(torrentFileChangedProperties,
                                  QStringLiteral("size"), changed,
                                  torrentUpdated->fileSize(fileIndex),
                                  torrentFileDb.value("size").toLongLong());
            recordChangedProperty(torrentFileChangedProperties,
                                  QStringLiteral("progress"), changed,
                                  ExUtils::progressString(filesProgress[fileIndex]).toInt(),
                                  torrentFileDb.value("progress").toInt());

            // All properties are the same
            if (!changed)
                continue;

            torrentFilesChangedProperties->insert(
                        torrentFileDb.value("id").toULongLong(),
                        torrentFileChangedProperties);
        }

        ++itTorrentsHash;

        // Nothing to update
        if (torrentFilesChangedProperties->isEmpty())
            continue;

        torrentsFilesChangedProperties.insert(torrentId, torrentFilesChangedProperties);
    }
}
