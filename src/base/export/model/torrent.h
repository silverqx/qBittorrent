#ifndef TORRENT_H
#define TORRENT_H

#include <QString>

namespace Export
{
    class BaseModel
    {
    public:
        virtual const QString &tableName() const = 0;
    };

    class Torrent final : public BaseModel
    {
    public:
        const QString &tableName() const
        {
            static const QString cached {"torrents"};
            return cached;
        }
    };
}

#endif // TORRENT_H
