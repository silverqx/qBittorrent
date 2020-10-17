#ifndef TORRENTPREVIEWABLEFILESREPOSITORY_H
#define TORRENTPREVIEWABLEFILESREPOSITORY_H

#include "base/export/torrentexporterconfig.h"
#include "base/export/types.h"
#include "core/baserepository.h"

namespace Export
{
    class EntityManager;

    class TorrentPreviewableFilesRepository final : public BaseRepository
    {
        Q_DISABLE_COPY(TorrentPreviewableFilesRepository)

    public:
        explicit TorrentPreviewableFilesRepository(EntityManager &em);

        const QString &tableName() const override;
        const QString &columns() const override;

        TorrentFileSqlRecordByIdHash
        selectTorrentsFilesByHandles(const TorrentHandleByIdHash &torrentsUpdated) const;
#if LOG_CHANGED_TORRENTS
        void updatePreviewableFilesInDb(
                const QSharedPointer<const TorrentFilesChangedHash> changedFilesProperties,
                TorrentId torrentId) const;
#else
        void updatePreviewableFilesInDb(
                const QSharedPointer<const TorrentFilesChangedHash> changedFilesProperties) const;
#endif
    };
}

#endif // TORRENTPREVIEWABLEFILESREPOSITORY_H
