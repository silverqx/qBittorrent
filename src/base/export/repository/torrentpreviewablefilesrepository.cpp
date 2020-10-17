#include "torrentpreviewablefilesrepository.h"

#include <QDebug>
#include <QtSql/QSqlError>

#include "base/export/exportererror.h"
#include "base/export/presenter/torrentfilesqlrecordbytorrentid.h"
#include "logquery.h"

using namespace Export;

TorrentPreviewableFilesRepository::TorrentPreviewableFilesRepository(EntityManager &em)
    : BaseRepository(em)
{}

const QString &TorrentPreviewableFilesRepository::tableName() const
{
    static const QString cached {"torrent_previewable_files"};
    return cached;
}

const QString &TorrentPreviewableFilesRepository::columns() const
{
    static const QString cached {"id, torrent_id, file_index, filepath, size, progress"};
    return cached;
}

TorrentFileSqlRecordByIdHash
TorrentPreviewableFilesRepository::selectTorrentsFilesByHandles(
        const TorrentHandleByIdHash &torrentsUpdated) const
{
    const auto torrentIds = torrentsUpdated.keys();
    return findWhereIn<Presenter::TorrentFileSqlRecordByTorrentId>(
                "torrent_id",
                QVector<TorrentId>(torrentIds.constBegin(), torrentIds.constEnd()));
}

#if LOG_CHANGED_TORRENTS
void TorrentPreviewableFilesRepository::updatePreviewableFilesInDb(
        const QSharedPointer<const TorrentFilesChangedHash> changedFilesProperties,
        const TorrentId torrentId) const
#else
void TorrentPreviewableFilesRepository::updatePreviewableFilesInDb(
        const QSharedPointer<const TorrentFilesChangedHash> changedFilesProperties) const
#endif
{
    // Handle nullptr
    if (changedFilesProperties.isNull())
        throw ExporterError("'changedFilesProperties == nullptr' in "
                            "updatePreviewableFilesInDb().");
    // Nothing to update
    const auto changedFilesPropertiesEmpty = changedFilesProperties->isEmpty();
    Q_ASSERT(!changedFilesPropertiesEmpty);
    if (changedFilesPropertiesEmpty)
        return;

    // Loop over all changed previewable files
    auto itChangedProperties = changedFilesProperties->constBegin();
    while (itChangedProperties != changedFilesProperties->constEnd()) {
        const auto torrentFileId = itChangedProperties.key();
        const auto changedProperties = itChangedProperties.value();

        // WANRNING should be incremented as soon as possible to avoid infinite loops, consider to change all while() to for(;;) to avoid this problem and also investigate alternatives silverqx
        ++itChangedProperties;

#if LOG_CHANGED_TORRENTS
        auto [ok, query] = update(*changedProperties, torrentFileId);
#else
        bool ok;
        std::tie(ok, std::ignore) = update(*changedProperties, torrentFileId);
#endif

        if (!ok)
            qDebug() << "Update query in updatePreviewableFilesInDb() failed";

#if LOG_CHANGED_TORRENTS
        qDebug("Number of updated torrent(ID%llu) files : %d",
               torrentId, query.numRowsAffected());
        LOG_EXECUTED_QUERY(query);
#endif

        if (!ok)
            throw ExporterError("Update query in updatePreviewableFilesInDb() failed.");
    }
}
