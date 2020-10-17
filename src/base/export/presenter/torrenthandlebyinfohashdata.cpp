#include "torrenthandlebyinfohashdata.h"

#include <QtSql/QSqlQuery>

using namespace Export::Presenter;
using Export::TorrentHandleByIdHash;

TorrentHandleByInfoHashData::TorrentHandleByInfoHashData(QSqlQuery &query)
    : BasePresenter(query)
{}

TorrentHandleByIdHash
TorrentHandleByInfoHashData::present(
        const QSharedPointer<const TorrentHandleByInfoHashHash> data) const
{
    // Create new QHash of selected torrents
    TorrentHandleByIdHash torrents;
    while (m_query.next()) {
        BitTorrent::InfoHash hash(m_query.value("hash").toString());
        if (!data->contains(hash))
            continue;

        torrents.insert(m_query.value("id").toULongLong(), data->value(hash));
    }

    return torrents;
}
