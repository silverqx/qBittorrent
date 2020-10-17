#ifndef EXPORTERTYPES_H
#define EXPORTERTYPES_H

#include <QtSql/QSqlRecord>

#include "base/bittorrent/infohash.h"

namespace BitTorrent
{
    class TorrentHandle;
}

namespace Export
{
    // TODO split to common types also used in qMedia and extract this types to qMediaCommon silverqx
    using TorrentHandleByInfoHashHash  =
          QHash<BitTorrent::InfoHash, const BitTorrent::TorrentHandle *>;
    using TorrentId = quint64;
    using TorrentIdByInfoHashHash      = QHash<TorrentId, BitTorrent::InfoHash>;
    using TorrentHandleByIdHash        = QHash<TorrentId, const BitTorrent::TorrentHandle *>;
    using TorrentSqlRecordByIdHash     = QHash<TorrentId, QSqlRecord>;
    using TorrentChangedProperties     = QVariantHash;
    using TorrentFileId                = quint64;
    using TorrentsChangedHash          =
          QHash<TorrentId, QSharedPointer<const TorrentChangedProperties>>;
    using TorrentFileChangedProperties = QVariantHash;
    using TorrentFilesChangedHash      =
          QHash<TorrentFileId, QSharedPointer<const TorrentFileChangedProperties>>;
    using TorrentsFilesChangedHash     =
          QHash<TorrentId, QSharedPointer<const TorrentFilesChangedHash>>;
    using TorrentFileIndex             = qint32;
    using TorrentFileSqlRecordByIdHash =
          QHash<TorrentId, QSharedPointer<QHash<TorrentFileIndex, QSqlRecord>>>;
}

#endif // EXPORTERTYPES_H
