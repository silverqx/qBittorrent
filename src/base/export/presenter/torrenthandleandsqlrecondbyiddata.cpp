#include "torrenthandleandsqlrecondbyiddata.h"

#include <QtSql/QSqlQuery>

#include "base/bittorrent/torrenthandle.h"

using namespace Export::Presenter;
using Export::TorrentHandleByIdHash;
using Export::TorrentHandleByInfoHashHash;
using Export::TorrentSqlRecordByIdHash;

TorrentHandleAndSqlRecondByIdData::TorrentHandleAndSqlRecondByIdData(QSqlQuery &query)
    : BasePresenter(query)
{}

std::tuple<const TorrentHandleByIdHash, const TorrentSqlRecordByIdHash>
TorrentHandleAndSqlRecondByIdData::present(const TorrentHandleByInfoHashHash &torrents) const
{
    // Create new QHash of selected torrents
    TorrentHandleByIdHash torrentsHash;
    TorrentSqlRecordByIdHash torrentRecords;
    while (m_query.next()) {
        // Find torrent handle by info hash
        BitTorrent::InfoHash hash(m_query.value("hash").toString());
        const auto itTorrentHandle =
                std::find_if(torrents.constBegin(), torrents.constEnd(),
                             [&hash](auto *const torrent)
        {
            return (torrent->hash() == hash) ? true : false;
        });
        // Insert
        const auto torrentId = m_query.value("id").toULongLong();
        torrentsHash.insert(
            torrentId,
            *itTorrentHandle
        );
        torrentRecords.insert(torrentId, m_query.record());
    }

    return {torrentsHash, torrentRecords};
}
