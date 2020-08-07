#ifndef TORRENTEXPORTER_H
#define TORRENTEXPORTER_H

#include <QObject>
#include <QtSql/QSqlDatabase>

#include "base/bittorrent/infohash.h"

namespace BitTorrent
{
    class TorrentHandle;

    class ExporterError final : public std::logic_error
    {
    public:
        explicit inline ExporterError(const char *Message)
            : std::logic_error(Message)
        {}
    };

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
        inline void setQMediaWindowActive(const bool active) { m_qMediaWindowActive = active; }

    private slots:
        void handleTorrentAdded(BitTorrent::TorrentHandle *const torrent);
        void handleTorrentDeleted(BitTorrent::InfoHash infoHash);
        void commitTorrentsTimerTimeout();
        void handleTorrentsUpdated(const QVector<BitTorrent::TorrentHandle *> &torrents);

    private:
        void connectToDb();
        void removeTorrentFromDb(InfoHash infoHash) const;
        void insertTorrentsToDb() const;
        void removeDuplicitTorrents();
        void insertPreviewableFilesToDb() const;
        QHash<quint64, TorrentHandle *> selectTorrentsByHashes(const QList<InfoHash> hashes) const;
        QHash<quint64, TorrentHandle *> selectTorrentsByHandles(
            const QVector<BitTorrent::TorrentHandle *> &torrents) const;
        void updateTorrentsInDb(const QVector<BitTorrent::TorrentHandle *> &torrents) const;
        void updatePreviewableFilesInDb(const QVector<BitTorrent::TorrentHandle *> &torrents) const;

        QTimer *m_dbCommitTimer;
        QHash<InfoHash, TorrentHandle *> *m_torrentsToCommit;
        HWND m_qMediaHwnd = nullptr;
        bool m_qMediaWindowActive = false;
        QSqlDatabase m_db;

        static TorrentExporter *m_instance;
    };
}

#endif // TORRENTEXPORTER_H
