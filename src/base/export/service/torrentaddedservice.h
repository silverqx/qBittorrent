#ifndef TORRENTADDEDSERVICE_H
#define TORRENTADDEDSERVICE_H

#include "base/export/repository/torrentsrepository.h"
#include "core/baseservice.h"

namespace Export
{
    class StatusHash;

    class TorrentAddedService final : public BaseService
    {
        Q_DISABLE_COPY(TorrentAddedService)

    public:
        TorrentAddedService(EntityManager &em,
                            QSharedPointer<TorrentHandleByInfoHashHash> &torrentsToCommit);

        void handleTorrentAdded();

    private:
        /*! Remove already existing torrents in DB from commit hash. */
        void removeExistingTorrents();
        void insertTorrentsToDb() const;
        void insertPreviewableFilesToDb() const;

        const StatusHash *m_statusHash;
        QSharedPointer<TorrentHandleByInfoHashHash> m_torrentsToCommit;
        QSharedPointer<TorrentsRepository> m_torrentsRepository;
    };
}

#endif // TORRENTADDEDSERVICE_H
