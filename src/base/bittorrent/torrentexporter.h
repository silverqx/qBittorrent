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

    // Starts from 1 because of MySQL enums starts from 1
    enum struct TorrentStatus
    {
        Allocating = 1,
        Checking,
        CheckingResumeData,
        Downloading,
        Error,
        Finished,
        ForcedDownloading,
        MissingFiles,
        Moving,
        Paused,
        Queued,
        Stalled,
        Unknown,
    };

    class TorrentExporter final : public QObject
    {
        Q_OBJECT
        Q_DISABLE_COPY(TorrentExporter)

    public:
        // TODO move this to private section silverqx
        // Like this:
//    private:
//        Logger();
//        ~Logger() = default;
        explicit TorrentExporter();
        ~TorrentExporter() override;

        static void initInstance();
        static void freeInstance();
        static TorrentExporter *instance();

        void setQMediaHwnd(const HWND hwnd);
        inline void setQMediaWindowActive(const bool active) { m_qMediaWindowActive = active; }

    private:
        void connectToDb();
        void removeTorrentFromDb(InfoHash infoHash) const;
        void insertTorrentsToDb() const;
        void removeDuplicitTorrents();
        void insertPreviewableFilesToDb() const;
        QHash<quint64, TorrentHandle *> selectTorrentsByHashes(const QList<InfoHash> hashes) const;
        QHash<quint64, TorrentHandle *> selectTorrentsByHandles(
            const QVector<BitTorrent::TorrentHandle *> &torrents) const;
        QHash<quint64, InfoHash> selectTorrentsByStatuses(
            const QList<TorrentStatus> &statuses) const;
        void updateTorrentsInDb(const QVector<BitTorrent::TorrentHandle *> &torrents) const;
        void updatePreviewableFilesInDb(const QVector<BitTorrent::TorrentHandle *> &torrents) const;
        /*! Needed when qBittorrent is closed, to fix torrent downloading statuses. */
        void correctTorrentStatusesOnExit();
        /*! Update torrent storage location in DB, after torrent was moved ( storage path changed ). */
        void updateTorrentSaveDirInDb(
                const std::pair<quint64, BitTorrent::TorrentHandle *> &torrentHashPair,
                const QString &newPath) const;

        QTimer *m_dbCommitTimer;
        QHash<InfoHash, TorrentHandle *> *m_torrentsToCommit;
        HWND m_qMediaHwnd = nullptr;
        bool m_qMediaWindowActive = false;
        QSqlDatabase m_db;

        static TorrentExporter *m_instance;

    private slots:
        void handleTorrentAdded(BitTorrent::TorrentHandle *const torrent);
        void handleTorrentDeleted(BitTorrent::InfoHash infoHash);
        void commitTorrentsTimerTimeout();
        void handleTorrentsUpdated(const QVector<BitTorrent::TorrentHandle *> &torrents);
        void handleTorrentStorageMoveFinished(BitTorrent::TorrentHandle *const torrent, const QString &newPath) const;
    };

    // QHash requirements
    uint qHash(const TorrentStatus &torrentStatus, uint seed);
}

#endif // TORRENTEXPORTER_H
