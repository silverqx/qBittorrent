#ifndef TORRENTFILESQLRECORDBYTORRENTID_H
#define TORRENTFILESQLRECORDBYTORRENTID_H

#include "base/export/types.h"
#include "core/basepresenter.h"

namespace Export::Presenter
{
    /*! Hash of torrent files by torrent id, torrent files are keyed by file index and populated
        by QSqlRecords. */
    class TorrentFileSqlRecordByTorrentId final : public BasePresenter
    {
        Q_DISABLE_COPY(TorrentFileSqlRecordByTorrentId)

    public:
        explicit TorrentFileSqlRecordByTorrentId(QSqlQuery &query);

        TorrentFileSqlRecordByIdHash present() const;
    };
}

#endif // TORRENTFILESQLRECORDBYTORRENTID_H
