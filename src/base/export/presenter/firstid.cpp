#include "firstid.h"

#include <QtSql/QSqlQuery>

using namespace Export::Presenter;
using Export::TorrentId;

FirstId::FirstId(QSqlQuery &query)
    : BasePresenter(query)
{}

TorrentId FirstId::present() const
{
    const auto idIndex = m_query.record().indexOf("id");
    if ((m_query.size() < 1) || (idIndex == -1))
        return 0;

    m_query.first();
    return m_query.value(idIndex).toULongLong();
}
