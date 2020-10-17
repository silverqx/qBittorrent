#ifndef TORRENTIDBYINFOHASH_H
#define TORRENTIDBYINFOHASH_H

#include "base/export/types.h"
#include "core/basepresenter.h"

namespace Export::Presenter
{
    class Q_DECL_UNUSED TorrentIdByInfoHash final : public BasePresenter
    {
        Q_DISABLE_COPY(TorrentIdByInfoHash)

    public:
        explicit TorrentIdByInfoHash(QSqlQuery &query);

        TorrentIdByInfoHashHash present() const;
    };
}

#endif // TORRENTIDBYINFOHASH_H
