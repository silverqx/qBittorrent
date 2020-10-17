#ifndef TORRENTUPDATEDSERVICE_H
#define TORRENTUPDATEDSERVICE_H

#include "base/export/repository/torrentpreviewablefilesrepository.h"
#include "base/export/repository/torrentsrepository.h"
#include "core/baseservice.h"

namespace Export
{
    class StatusHash;

    class TorrentUpdatedService final : public BaseService
    {
        Q_DISABLE_COPY(TorrentUpdatedService)

    public:
        explicit TorrentUpdatedService(EntityManager &em);

        QList<BitTorrent::InfoHash>
        handleTorrentsUpdated(const QVector<BitTorrent::TorrentHandle *> &updatedTorrents);

    private:
        void updateTorrentsInDb(
                const TorrentsChangedHash &torrentsChangedHash,
                const TorrentsFilesChangedHash &torrentsFilesChangedHash) const;
        bool fillTorrentsChangedProperties(
                const TorrentHandleByInfoHashHash &torrents,
                TorrentsChangedHash &torrentsChangedProperties,
                TorrentsFilesChangedHash &torrentsFilesChangedProperties) const;
        /*! Find out changed properties in updated torrents. */
        void traceTorrentChangedProperties(
                const TorrentHandleByIdHash &torrentsUpdated,
                const TorrentSqlRecordByIdHash &torrentsInDb,
                TorrentsChangedHash &torrentsChangedProperties) const;
        /*! Find out changed properties in updated torrent files. */
        void traceTorrentFilesChangedProperties(
                const TorrentHandleByIdHash &torrentsUpdated,
                const TorrentFileSqlRecordByIdHash &torrentsFilesInDb,
                TorrentsFilesChangedHash &torrentsFilesChangedProperties) const;

        const StatusHash *m_statusHash;
        QSharedPointer<TorrentsRepository> m_torrentsRepository;
        QSharedPointer<TorrentPreviewableFilesRepository> m_torrentPreviewableFilesRepository;
    };
}

#endif // TORRENTUPDATEDSERVICE_H
