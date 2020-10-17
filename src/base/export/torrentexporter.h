#ifndef TORRENTEXPORTER_H
#define TORRENTEXPORTER_H

#include "base/export/repository/torrentsrepository.h"

namespace BitTorrent
{
    class TorrentHandle;
}

namespace Export
{
    class TorrentExporter final : public QObject
    {
        Q_OBJECT
        Q_DISABLE_COPY(TorrentExporter)

    public:
        static void initInstance();
        static void freeInstance();
        static TorrentExporter *instance();

        inline void setQMediaHwnd(const HWND hwnd) noexcept
        { m_qMediaHwnd = hwnd; };
        inline void setQMediaWindowActive(const bool active) noexcept
        { m_qMediaWindowActive = active; };

    private:
        static TorrentExporter *m_instance;
        static const int COMMIT_INTERVAL_BASE = 1000;
        /*! Maximum interval between connect attempts to db. */
        static const int COMMIT_INTERVAL_MAX  = 5000;

        TorrentExporter();
        ~TorrentExporter() override;

        inline void deferCommitTimerTimeout() const
        {
            m_dbCommitTimer->start(std::min(m_dbCommitTimer->interval() * 2,
                                            COMMIT_INTERVAL_MAX));
        }

        // TODO create m_torrentsToCommit on the stack silverqx
        QSharedPointer<TorrentHandleByInfoHashHash> m_torrentsToCommit;
        // TODO create m_dbCommitTimer on the stack silverqx
        QPointer<QTimer> m_dbCommitTimer;
        HWND m_qMediaHwnd = nullptr;
        bool m_qMediaWindowActive = false;
        EntityManager m_em;
        QSharedPointer<TorrentsRepository> m_torrentsRepository;

    private slots:
        void handleTorrentAdded(BitTorrent::TorrentHandle *const torrent);
        void handleTorrentDeleted(BitTorrent::InfoHash infoHash) const;
        void commitTorrentsTimerTimeout();
        void handleTorrentsUpdated(const QVector<BitTorrent::TorrentHandle *> &torrents);
        void handleTorrentStorageMoveFinished(const BitTorrent::TorrentHandle *const torrent,
                                              const QString &newPath);
    };
}

#endif // TORRENTEXPORTER_H
