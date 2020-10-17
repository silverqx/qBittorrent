#include "torrentaddedservice.h"

#include <QDateTime>
#include <QDebug>
#include <QtSql/QSqlError>

#include "base/export/exportererror.h"
#include "base/export/exutils.h"
#include "base/export/presenter/torrenthandlebyinfohashdata.h"
#include "base/utils/fs.h"
#include "base/utils/misc.h"
#include "orm/entitymanager.h"

using namespace Export;

namespace
{
    /*! Get the count of all previewable files in torrents hash. */
    const auto countPreviewableFiles =
            [](const TorrentHandleByIdHash &torrents) -> int
    {
        int previewableFilesCount = 0;
        for (const auto *const torrent : torrents)
            for (int i = 0; i < torrent->filesCount() ; ++i)
                if (Utils::Misc::isPreviewable(Utils::Fs::fileExtension(torrent->fileName(i))))
                    ++previewableFilesCount;
        return previewableFilesCount;
    };
}

TorrentAddedService::TorrentAddedService(
        EntityManager &em,
        QSharedPointer<TorrentHandleByInfoHashHash> &torrentsToCommit)
    : BaseService(em)
    , m_statusHash(StatusHash::instance())
    , m_torrentsToCommit(torrentsToCommit)
    , m_torrentsRepository(m_em.getRepository<TorrentsRepository>())
{}

void TorrentAddedService::handleTorrentAdded()
{
    // Remove existing torrents from commit hash
    removeExistingTorrents();

    // Nothing to insert to DB
    if (m_torrentsToCommit->size() == 0)
        return;

    // Use transaction to guarantee data integrity.
    // I decided to use multi insert queries for better performance reasons, of course
    // it has its drawbacks.
    // If one of the insert queries fails, an exception is thrown and data will be rollback-ed.
    // TODO use transaction with savepoints, like in a update strategy silverqx
    // Use transaction to guarantee data integrity
    m_em.transaction();
    try {
        // Multi insert for torrents
        insertTorrentsToDb();
        // Multi insert for previewable torrent files
        insertPreviewableFilesToDb();
        m_em.commit();
    }  catch (const ExporterError &e) {
        m_em.rollback();
        qCritical() << "Critical in commitTorrentsTimerTimeout() :"
                    << e.what();
        return;
    }
}

void TorrentAddedService::removeExistingTorrents()
{
    const auto torrentIds = m_torrentsToCommit->keys();
    auto [ok, query] =
            m_torrentsRepository->findWhereIn(
                "hash",
                QVector<QString>(torrentIds.constBegin(), torrentIds.constEnd()),
                {"hash"});

    if (!ok)
        qDebug() << "Select torrents in removeExistingTorrents() failed";

    // Any duplicit torrents in DB
    if (query.size() <= 0)
        return;

    // Remove duplicit torrents from commit hash
    while (query.next()) {
        BitTorrent::InfoHash hash(query.value(0).toString());
        if (m_torrentsToCommit->contains(hash))
            m_torrentsToCommit->remove(hash);
    }
}

void TorrentAddedService::insertTorrentsToDb() const
{
    // Assemble query binding placeholders for multi insert on the base of count
    // of torrents to insert.
    auto placeholders = QStringLiteral("(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?), ")
                        .repeated(m_torrentsToCommit->size());
    // Will never be empty, is checked earlier
    placeholders.chop(2);

    const auto torrentsQueryString =
            QStringLiteral("INSERT INTO torrents (name, progress, eta, size, seeds, "
                           "total_seeds, leechers, total_leechers, remaining, added_on, "
                           "hash, status, savepath) "
                           "VALUES %1")
            .arg(placeholders);

    auto torrentsQuery = m_em.query();
    torrentsQuery.prepare(torrentsQueryString);

    // Prepare query bindings for torrents
    const BitTorrent::TorrentHandle *torrent;
    auto itTorrents = m_torrentsToCommit->constBegin();
    while (itTorrents != m_torrentsToCommit->constEnd()) {
        torrent = itTorrents.value();
#ifdef QT_DEBUG
        const auto torrentName = torrent->name();
        Q_UNUSED(torrentName);
#endif

        torrentsQuery.addBindValue(torrent->name());
        torrentsQuery.addBindValue(ExUtils::progressString(torrent->progress()));
        torrentsQuery.addBindValue(torrent->eta());
        torrentsQuery.addBindValue(torrent->totalSize());
        torrentsQuery.addBindValue(torrent->seedsCount());
        torrentsQuery.addBindValue(torrent->totalSeedsCount());
        torrentsQuery.addBindValue(torrent->leechsCount());
        torrentsQuery.addBindValue(torrent->totalLeechersCount());
        torrentsQuery.addBindValue(torrent->incompletedSize());
        torrentsQuery.addBindValue(torrent->addedTime());
        torrentsQuery.addBindValue(QString(torrent->hash()));
        torrentsQuery.addBindValue((*m_statusHash)[torrent->state()].text);
        torrentsQuery.addBindValue(
                    Utils::Fs::toUniformPath(QDir::cleanPath(torrent->savePath(true))));

        ++itTorrents;
    }

    if (torrentsQuery.exec())
        return;

    qDebug() << "Insert torrents in insertTorrentsToDb() failed :"
             << torrentsQuery.lastError().text();
    throw ExporterError("Insert torrents in insertTorrentsToDb() failed.");
}

void TorrentAddedService::insertPreviewableFilesToDb() const
{
    /* Select inserted torrent ids by InfoHash-es for a torrents to commit and return
       torrent handles mapped by torrent ids. */
    const auto hashesList = m_torrentsToCommit->keys();
    const auto insertedTorrents =
            m_torrentsRepository->findWhereIn<Presenter::TorrentHandleByInfoHashData>(
                "hash",
                // WARNING Find out, how / why this is possible 😲 silverqx
                QVector<QString>(hashesList.constBegin(), hashesList.constEnd()),
                m_torrentsToCommit,
                {"id", "hash"});

    if (insertedTorrents.size() == 0) {
        qDebug() << "Inserted torrents count is 0, this should never have happen :/";
        return;
    }

    // Assemble query binding placeholders for multi insert on the base of
    // count of the torrents to insert.
    // Everything will be inserted in one insert query.
    auto placeholders = QStringLiteral("(?, ?, ?, ?, ?), ")
                        .repeated(countPreviewableFiles(insertedTorrents));
    // Will never be empty, is checked above
    placeholders.chop(2);

    const auto previewableFilesQueryString =
        QStringLiteral("INSERT INTO torrent_previewable_files "
                       "(torrent_id, file_index, filepath, size, progress) "
                       "VALUES %1")
        .arg(placeholders);

    auto previewableFilesQuery = m_em.query();
    previewableFilesQuery.prepare(previewableFilesQueryString);

    // Prepare query bindings for torrent_previewable_files
    const BitTorrent::TorrentHandle *torrent;
    QVector<qreal> filesProgress;
    auto itInsertedTorrents = insertedTorrents.constBegin();
    while (itInsertedTorrents != insertedTorrents.constEnd()) {
        torrent = itInsertedTorrents.value();
#ifdef QT_DEBUG
        const auto torrentName = torrent->name();
        Q_UNUSED(torrentName);
#endif

        filesProgress = torrent->filesProgress();

        for (int i = 0; i < torrent->filesCount(); ++i) {
            if (!Utils::Misc::isPreviewable(Utils::Fs::fileExtension(torrent->fileName(i))))
                continue;

            previewableFilesQuery.addBindValue(itInsertedTorrents.key());
            previewableFilesQuery.addBindValue(i);
            previewableFilesQuery.addBindValue(torrent->filePath(i));
            previewableFilesQuery.addBindValue(torrent->fileSize(i));
            previewableFilesQuery.addBindValue(ExUtils::progressString(filesProgress[i]));
        }

        ++itInsertedTorrents;
    }

    if (previewableFilesQuery.exec())
        return;

    qDebug() << "Insert torrent files in insertPreviewableFilesToDb() failed :"
             << previewableFilesQuery.lastError().text();
    throw ExporterError("Insert torrent files in insertPreviewableFilesToDb() failed.");
}
