#include "torrentidbyinfohash.h"

#include <QtSql/QSqlQuery>

using namespace Export::Presenter;
using Export::TorrentIdByInfoHashHash;

TorrentIdByInfoHash::TorrentIdByInfoHash(QSqlQuery &query)
    : BasePresenter(query)
{}

TorrentIdByInfoHashHash
TorrentIdByInfoHash::present() const
{
    // Create new QHash of selected torrents
    TorrentIdByInfoHashHash torrents;
    while (m_query.next()) {
        BitTorrent::InfoHash hash(m_query.value("hash").toString());
        torrents.insert(m_query.value("id").toULongLong(), hash);
    }

    return torrents;
}
