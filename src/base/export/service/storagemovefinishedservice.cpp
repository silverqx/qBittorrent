#include "storagemovefinishedservice.h"

#include <QDebug>

#include "base/utils/fs.h"
#include "orm/entitymanager.h"

using namespace Export;

StorageMoveFinishedService::StorageMoveFinishedService(EntityManager &em)
    : BaseService(em)
    , m_torrentsRepository(m_em.getRepository<TorrentsRepository>())
{}

void StorageMoveFinishedService::handleTorrentStorageMoveFinished(
        const BitTorrent::TorrentHandle *const torrent, const QString &newPath) const
{
    const auto torrentId = m_torrentsRepository->getTorrentIdByInfoHash(torrent->hash());
    // Nothing to udpate
    if (torrentId == 0)
        return;

    qDebug("Updating savepath in db for torrent(ID%llu) : \"%s\"",
           torrentId, qUtf8Printable(torrent->name()));

    // Update torrent's savepath
    m_torrentsRepository->updateWhere(
                {{"savepath", Utils::Fs::toUniformPath(QDir::cleanPath(newPath))}},
                {{"id", torrentId}});
}
