#ifndef TORRENTEXPORTER_H
#define TORRENTEXPORTER_H

#include <QDateTime>
#include <QObject>
#include <QtSql/QSqlDatabase>

#include "base/bittorrent/infohash.h"
#include "base/torrentexporterconfig.h"

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
        typedef quint64 TorrentId;
        typedef QHash<TorrentId, const TorrentHandle *> TorrentHandleByIdHash;
        typedef QHash<InfoHash, const TorrentHandle *> TorrentHandleByInfoHashHash;

        static void initInstance();
        static void freeInstance();
        static TorrentExporter *instance();

        inline void setQMediaHwnd(const HWND hwnd) noexcept
        { m_qMediaHwnd = hwnd; };
        inline void setQMediaWindowActive(const bool active) noexcept
        { m_qMediaWindowActive = active; };

    private:
        typedef quint64 TorrentFileId;
        typedef QHash<TorrentId, QSqlRecord> TorrentSqlRecordByIdHash;
        typedef qint32 TorrentFileIndex;
        typedef QHash<TorrentId, QSharedPointer<QHash<TorrentFileIndex, QSqlRecord>>>
                TorrentFileSqlRecordByIdHash;
        typedef QVariantHash TorrentChangedProperties;
        typedef QHash<TorrentId, QSharedPointer<const TorrentChangedProperties>>
                TorrentsChangedHash;
        typedef QVariantHash TorrentFileChangedProperties;
        typedef QHash<TorrentFileId, QSharedPointer<const TorrentFileChangedProperties>>
                TorrentFilesChangedHash;
        typedef QHash<TorrentId, QSharedPointer<const TorrentFilesChangedHash>>
                TorrentsFilesChangedHash;

        TorrentExporter();
        ~TorrentExporter() override;

        void connectToDb() const;
        void removeTorrentFromDb(const InfoHash &infoHash) const;
        void insertTorrentsToDb() const;
        /*! Remove already existing torrents in DB from commit hash. */
        void removeExistingTorrents();
        void insertPreviewableFilesToDb() const;
        /*! Select inserted torrent ids by InfoHash-es for a torrents to commit and return
            torrent handles mapped by torrent ids. Used only during torrent added alert. */
        TorrentHandleByIdHash
        selectTorrentIdsToCommitByHashes(const QList<InfoHash> &hashes) const;
        TorrentHandleByIdHash
        mapTorrentHandleById(const TorrentHandleByInfoHashHash &torrents) const;
        std::tuple<const TorrentHandleByIdHash, const TorrentSqlRecordByIdHash>
        selectTorrentsByHandles(
                const TorrentHandleByInfoHashHash &torrents,
                const QString &select = "id, hash") const;
        TorrentExporter::TorrentFileSqlRecordByIdHash
        selectTorrentsFilesByHandles(const TorrentHandleByIdHash &torrentsUpdated) const;
        QHash<TorrentId, InfoHash>
        selectTorrentsByStatuses(const QList<TorrentStatus> &statuses) const;
        /*! Needed when qBittorrent is closed, to fix torrent downloading statuses. */
        void correctTorrentStatusesOnExit() const;
        /*! Needed when qBittorrent is closed, to set seeds, total_seeds, leechers and
            total_leechers to 0. */
        void correctTorrentPeersOnExit() const;
        /*! Update torrent storage location in DB, after torrent was moved ( storage path
            changed ). */
        void updateTorrentSaveDirInDb(TorrentId torrentId, const QString &newPath,
                                      const QString &torrentName) const;
        void updateTorrentsInDb(
                const TorrentsChangedHash &torrentsChangedHash,
                const TorrentsFilesChangedHash &torrentsFilesChangedHash) const;
        void updateTorrentInDb(
                TorrentId torrentId,
                const QSharedPointer<const TorrentChangedProperties> changedProperties) const;
#if LOG_CHANGED_TORRENTS
        void updatePreviewableFilesInDb(
                const QSharedPointer<const TorrentFilesChangedHash> changedFilesProperties,
                TorrentId torrentId) const;
#else
        void updatePreviewableFilesInDb(
                const QSharedPointer<const TorrentFilesChangedHash> changedFilesProperties) const;
#endif
        /*! Find out changed properties in updated torrents. */
        TorrentExporter::TorrentsChangedHash
        traceTorrentChangedProperties(
                const TorrentHandleByIdHash &torrentsUpdated,
                const TorrentSqlRecordByIdHash &torrentsInDb) const;
        /*! Find out changed properties in updated torrent files. */
        TorrentExporter::TorrentsFilesChangedHash
        traceTorrentFilesChangedProperties(
                const TorrentHandleByIdHash &torrentsUpdated,
                const TorrentFileSqlRecordByIdHash &torrentsFilesInDb) const;

        QScopedPointer<TorrentHandleByInfoHashHash> m_torrentsToCommit;
        QPointer<QTimer> m_dbCommitTimer;
        HWND m_qMediaHwnd = nullptr;
        bool m_qMediaWindowActive = false;

        static TorrentExporter *m_instance;

    private slots:
        void handleTorrentAdded(BitTorrent::TorrentHandle *const torrent) const;
        void handleTorrentDeleted(BitTorrent::InfoHash infoHash) const;
        void commitTorrentsTimerTimeout();
        void handleTorrentsUpdated(const QVector<BitTorrent::TorrentHandle *> &torrents);
        void handleTorrentStorageMoveFinished(const BitTorrent::TorrentHandle *const torrent,
                                              const QString &newPath) const;
    };

    // QHash requirements
    uint qHash(const TorrentStatus &torrentStatus, uint seed);
}

#endif // TORRENTEXPORTER_H
