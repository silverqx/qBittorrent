#include "torrentsrepository.h"

#include <QDebug>
#include <QtSql/QSqlError>

#include "base/export/exportererror.h"
#include "base/export/presenter/firstid.h"
#include "base/export/presenter/torrenthandleandsqlrecondbyiddata.h"
#include "base/export/torrentexporterconfig.h"
#include "logquery.h"

using namespace Export;

TorrentsRepository::TorrentsRepository(EntityManager &em)
    : BaseRepository(em)
    , m_statusTextHash(StatusTextHash::instance())
{}

const QString &TorrentsRepository::tableName() const
{
    static const QString cached {"torrents"};
    return cached;
}

const QString &TorrentsRepository::columns() const
{
    static const QString cached {"id, name, progress, eta, size, seeds, total_seeds, leechers, "
                                 "total_leechers, remaining, added_on, hash, status, "
                                 "movie_detail_index, savepath"};
    return cached;
}

void TorrentsRepository::removeTorrentFromDb(const BitTorrent::InfoHash &infoHash) const
{
    deleteWhere({{"hash", static_cast<QString>(infoHash)}});
}

void TorrentsRepository::correctTorrentStatusesOnExit() const
{
    updateWhereIn<QString>({{"status", (*m_statusTextHash)[TorrentStatus::Stalled]}},
                           "status",
                           {(*m_statusTextHash)[TorrentStatus::Downloading]});
}

void TorrentsRepository::correctTorrentPeersOnExit() const
{
    update({{"seeds",          0},
            {"total_seeds",    0},
            {"leechers",       0},
            {"total_leechers", 0}});
}

TorrentId TorrentsRepository::getTorrentIdByInfoHash(const BitTorrent::InfoHash &infoHash) const
{
    // TODO add cache for torrent ids or may be for torrent handles, or check if qbt doesn't already has this cache somewhere silverqx
    const auto hash = static_cast<QString>(infoHash);
    const auto torrentId = findWhere<Presenter::FirstId>({{"hash", hash}},
                                                         {{"id"}});

    if (torrentId == 0) {
        qDebug() << "Torrent isn't in db, in getTorrentIdByInfoHash(), this "
                    "should never have happen :/ :"
                 << hash;
        return 0;
    }

    return torrentId;
}

std::tuple<const TorrentHandleByIdHash, const TorrentSqlRecordByIdHash>
TorrentsRepository::selectTorrentsByHashes(const TorrentHandleByInfoHashHash &torrents) const
{
    const auto hashes = torrents.keys();
    const auto [torrentsUpdated, torrentsInDb] =
            findWhereIn<Presenter::TorrentHandleAndSqlRecondByIdData>(
                "hash",
                QVector<QString>(hashes.constBegin(), hashes.constEnd()),
                torrents);

    return {torrentsUpdated, torrentsInDb};
}

void TorrentsRepository::updateTorrentInDb(
        const TorrentId torrentId,
        const QSharedPointer<const TorrentChangedProperties> changedProperties
) const
{
    // Nothing to update
    if (changedProperties->isEmpty())
        return;

#if LOG_CHANGED_TORRENTS
    auto [ok, query] = update(*changedProperties, torrentId);
#else
    bool ok;
    std::tie(ok, std::ignore) = update(*changedProperties, torrentId);
#endif

    if (!ok)
        qDebug() << "Update query in updateTorrentInDb() failed";

#if LOG_CHANGED_TORRENTS
    qDebug() << "Number of updated torrents :"
             << query.numRowsAffected();
    LOG_EXECUTED_QUERY(query);
#endif

    if (!ok)
        throw ExporterError("Update query in updateTorrentInDb() failed.");
}
