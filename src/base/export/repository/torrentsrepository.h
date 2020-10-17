#ifndef TORRENTSREPOSITORY_H
#define TORRENTSREPOSITORY_H

#include "base/export/torrentstatus.h"
#include "base/export/types.h"
#include "core/baserepository.h"

namespace BitTorrent
{
    class TorrentHandle;
}

namespace Export
{
    class EntityManager;
    class StatusTextHash;

    class TorrentsRepository final : public BaseRepository
    {
        Q_DISABLE_COPY(TorrentsRepository)

    public:
        explicit TorrentsRepository(EntityManager &em);

        const QString &tableName() const override;
        const QString &columns() const override;

        /* TorrentExporter section */
        void removeTorrentFromDb(const BitTorrent::InfoHash &infoHash) const;
        /*! Needed when qBittorrent is closed, to fix torrent downloading statuses. */
        void correctTorrentStatusesOnExit() const;
        /*! Needed when qBittorrent is closed, to set seeds, total_seeds, leechers and
            total_leechers to 0. */
        void correctTorrentPeersOnExit() const;

        /* StorageMoveFinishedService section */
        TorrentId getTorrentIdByInfoHash(const BitTorrent::InfoHash &infoHash) const;

        /* handleTorrensUpdated() section */
        /*! Create torrents hash keyed by torrent id, it's selected from db by torrent info hashes.
            Also return actual QSqlRecords keyed by torrent id, which are needed to trace changed
            properties*/
        std::tuple<const TorrentHandleByIdHash, const TorrentSqlRecordByIdHash>
        selectTorrentsByHashes(const TorrentHandleByInfoHashHash &torrents) const;
        void updateTorrentInDb(
                TorrentId torrentId,
                const QSharedPointer<const TorrentChangedProperties> changedProperties) const;

    private:
        const StatusTextHash *m_statusTextHash;
    };
}

#endif // TORRENTSREPOSITORY_H
