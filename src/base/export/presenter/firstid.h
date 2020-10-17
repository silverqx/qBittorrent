#ifndef FIRSTID_H
#define FIRSTID_H

#include "base/export/types.h"
#include "core/basepresenter.h"

namespace Export::Presenter
{
    /*! Get ID value from the first row. */
    class FirstId final : public BasePresenter
    {
        Q_DISABLE_COPY(FirstId)

    public:
        explicit FirstId(QSqlQuery &query);

        TorrentId present() const;
    };
}

#endif // FIRSTID_H
