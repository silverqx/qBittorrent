#ifndef TORRENTEXPORTER_H
#define TORRENTEXPORTER_H

#include <QObject>

#include "base/bittorrent/infohash.h"

namespace BitTorrent
{
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

        void setQMediaHwnd(const HWND hwnd);

    private slots:
        void handleTorrentAdded(BitTorrent::TorrentHandle *const torrent);
        void handleTorrentDeleted(BitTorrent::InfoHash infoHash);
        void commitTorrentsTimerTimeout();
        void handleTorrentsUpdated(const QVector<BitTorrent::TorrentHandle *> &torrents);

    private:
        void connectToDb() const;
        void removeTorrentFromDb(InfoHash infoHash) const;
        void insertTorrentsToDb() const;
        void removeDuplicitTorrents();
        void insertPreviewableFilesToDb() const;
        QHash<quint64, TorrentHandle *> selectTorrentsByHashes(const QList<InfoHash> hashes) const;
        void updateTorrentsInDb(const QVector<BitTorrent::TorrentHandle *> &torrents) const;

        QTimer *m_dbCommitTimer;
        QHash<InfoHash, TorrentHandle *> *m_torrentsToCommit;
        HWND m_qMediaHwnd = nullptr;

        static TorrentExporter *m_instance;
    };
}

#endif // TORRENTEXPORTER_H
