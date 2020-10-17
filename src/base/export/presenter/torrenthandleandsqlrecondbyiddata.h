#ifndef TORRENTHANDLEANDSQLRECONDBYIDDATA_H
#define TORRENTHANDLEANDSQLRECONDBYIDDATA_H

#include "base/export/types.h"
#include "core/basepresenter.h"

namespace Export::Presenter
{
    class TorrentHandleAndSqlRecondByIdData final : public BasePresenter
    {
        Q_DISABLE_COPY(TorrentHandleAndSqlRecondByIdData)

    public:
        explicit TorrentHandleAndSqlRecondByIdData(QSqlQuery &query);

        std::tuple<const TorrentHandleByIdHash, const TorrentSqlRecordByIdHash>
        present(const TorrentHandleByInfoHashHash &torrents) const;
    };
}

#endif // TORRENTHANDLEANDSQLRECONDBYIDDATA_H
