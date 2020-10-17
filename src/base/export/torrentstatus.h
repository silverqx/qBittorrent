#ifndef TORRENTSTATUS_H
#define TORRENTSTATUS_H

#include "base/bittorrent/torrenthandle.h"

namespace Export
{
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

    struct StatusProperties
    {
        TorrentStatus state;
        QString text;
    };

    /*! Map qBittorent TorrentState to our custom TorrentStatus used in qMedia. */
    class StatusHash final
    {
        Q_DISABLE_COPY(StatusHash)

    public:
        static StatusHash *instance();
        static void freeInstance();

        inline const StatusProperties &operator[](const BitTorrent::TorrentState key) const
        {
            // Cached reference to statusHash, wtf 😂
            static auto &statusHash {getStatusHash()};
            return statusHash[key];
        }

    private:
        StatusHash() = default;
        ~StatusHash() = default;

        QHash<BitTorrent::TorrentState, StatusProperties> &getStatusHash() const;

        static StatusHash *m_instance;
    };

    /*! Map our custom TorrentStatus to the text representation. */
    class StatusTextHash final
    {
        Q_DISABLE_COPY(StatusTextHash)

    public:
        static StatusTextHash *instance();
        static void freeInstance();

        inline const QString &operator[](const TorrentStatus key) const
        {
            // Cached reference to statusTextHash, wtf 😂
            static auto &statusTextHash {getStatusTextHash()};
            return statusTextHash[key];
        }

    private:
        StatusTextHash() = default;
        ~StatusTextHash() = default;

        QHash<TorrentStatus, QString> &getStatusTextHash() const;

        static StatusTextHash *m_instance;
    };

    // QHash requirements
    uint qHash(const TorrentStatus &torrentStatus, uint seed);
}

#endif // TORRENTSTATUS_H
