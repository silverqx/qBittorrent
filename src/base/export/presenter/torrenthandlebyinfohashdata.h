#ifndef TORRENTHANDLEBYINFOHASHDATA_H
#define TORRENTHANDLEBYINFOHASHDATA_H

#include "base/export/types.h"
#include "core/basepresenter.h"

namespace Export::Presenter
{
    /*! Map torrent handles by ids.
        Torrent handles are obtained from data parameter. Used only during
        a torrent added alert. */
    class TorrentHandleByInfoHashData final : public BasePresenter
    {
        Q_DISABLE_COPY(TorrentHandleByInfoHashData)

    public:
        explicit TorrentHandleByInfoHashData(QSqlQuery &query);

        TorrentHandleByIdHash
        present(const QSharedPointer<const TorrentHandleByInfoHashHash> data) const;
    };
}

#endif // TORRENTHANDLEBYINFOHASHDATA_H
