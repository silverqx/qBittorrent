#ifndef STORAGEMOVEFINISHEDSERVICE_H
#define STORAGEMOVEFINISHEDSERVICE_H

#include "base/export/repository/torrentsrepository.h"
#include "core/baseservice.h"

namespace Export
{
    class StorageMoveFinishedService final : public BaseService
    {
        Q_DISABLE_COPY(StorageMoveFinishedService)

    public:
        explicit StorageMoveFinishedService(EntityManager &m_em);

        void handleTorrentStorageMoveFinished(const BitTorrent::TorrentHandle *const torrent,
                                              const QString &newPath) const;

    private:
        QSharedPointer<TorrentsRepository> m_torrentsRepository;
    };
}

#endif // STORAGEMOVEFINISHEDSERVICE_H
