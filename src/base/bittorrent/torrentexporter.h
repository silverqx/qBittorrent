#ifndef TORRENTEXPORTER_H
#define TORRENTEXPORTER_H

#include <QObject>

#include "base/bittorrent/infohash.h"

namespace BitTorrent
{
//    class InfoHash;
    class TorrentHandle;

    class TorrentExporter final : public QObject
    {
        Q_OBJECT
        Q_DISABLE_COPY(TorrentExporter)
    public:
        explicit TorrentExporter();
        ~TorrentExporter() override;

        static void initInstance();
        static void freeInstance();
        static TorrentExporter *instance();

    private slots:
        void handleTorrentAdded(BitTorrent::TorrentHandle *const torrent);
        void handleTorrentDeleted(BitTorrent::InfoHash infoHash);
        void commitTorrentsTimerTimeout();

    private:
        void connectToDb() const;
        void removeTorrentFromDb(InfoHash infoHash) const;
        void insertTorrentsToDb() const;
        void removeDuplicitTorrents();
        void insertPreviewableFilesToDb() const;
        QHash<quint64, TorrentHandle *> selectTorrentsByHashes(const QList<InfoHash> hashes) const;

        QTimer *m_dbCommitTimer;
        QHash<InfoHash, TorrentHandle *> *m_torrentsToCommit;

        static TorrentExporter *m_instance;
    };
}

#endif // TORRENTEXPORTER_H
