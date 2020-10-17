#include "torrentfilesqlrecordbytorrentid.h"

#include <QtSql/QSqlQuery>

using namespace Export::Presenter;
using Export::TorrentFileSqlRecordByIdHash;

TorrentFileSqlRecordByTorrentId::TorrentFileSqlRecordByTorrentId(QSqlQuery &query)
    : BasePresenter(query)
{}

TorrentFileSqlRecordByIdHash
TorrentFileSqlRecordByTorrentId::present() const
{
    // Create new QHash of selected torrent files
    TorrentFileSqlRecordByIdHash torrentFilesInDb;
    while (m_query.next()) {
        const auto torrentId = m_query.value("torrent_id").toULongLong();
        QSharedPointer<QHash<TorrentFileIndex, QSqlRecord>> torrentFiles;
        // Obtain existing or create new hash if doesn't exist
        if (torrentFilesInDb.contains(torrentId))
            torrentFiles = torrentFilesInDb.value(torrentId);
        else
            torrentFiles.reset(new QHash<TorrentFileIndex, QSqlRecord>);
        torrentFiles->insert(
                    m_query.value("file_index").toInt(),
                    m_query.record());
        torrentFilesInDb.insert(torrentId, torrentFiles);
    }

    return torrentFilesInDb;
}
