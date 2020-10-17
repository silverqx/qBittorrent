#include "torrentstatus.h"

using namespace Export;

namespace
{
    const StatusProperties statusAllocating
            {TorrentStatus::Allocating,         QStringLiteral("Allocating")};
    const StatusProperties statusChecking
            {TorrentStatus::Checking,           QStringLiteral("Checking")};
    const StatusProperties statusCheckingResumeData
            {TorrentStatus::CheckingResumeData, QStringLiteral("CheckingResumeData")};
    const StatusProperties statusDownloading
            {TorrentStatus::Downloading,        QStringLiteral("Downloading")};
    const StatusProperties statusError
            {TorrentStatus::Error,              QStringLiteral("Error")};
    const StatusProperties statusFinished
            {TorrentStatus::Finished,           QStringLiteral("Finished")};
    const StatusProperties statusForcedDownloading
            {TorrentStatus::ForcedDownloading,  QStringLiteral("ForcedDownloading")};
    const StatusProperties statusMissingFiles
            {TorrentStatus::MissingFiles,       QStringLiteral("MissingFiles")};
    const StatusProperties statusMoving
            {TorrentStatus::Moving,             QStringLiteral("Moving")};
    const StatusProperties statusPaused
            {TorrentStatus::Paused,             QStringLiteral("Paused")};
    const StatusProperties statusQueued
            {TorrentStatus::Queued,             QStringLiteral("Queued")};
    const StatusProperties statusStalled
            {TorrentStatus::Stalled,            QStringLiteral("Stalled")};
    const StatusProperties statusUnknown
            {TorrentStatus::Unknown,            QStringLiteral("Unknown")};
}

/* Section for the StatusHash class. */

StatusHash *StatusHash::m_instance = nullptr;

StatusHash *StatusHash::instance()
{
    if (!m_instance)
        m_instance = new StatusHash();
    return m_instance;
}

void StatusHash::freeInstance()
{
    if (m_instance == nullptr)
        return;

    delete m_instance;
    m_instance = nullptr;
}

QHash<BitTorrent::TorrentState, StatusProperties> &
StatusHash::getStatusHash() const
{
    using TorrentState = BitTorrent::TorrentState;
    static QHash<TorrentState, StatusProperties> cached
    {
        {TorrentState::Allocating,          statusAllocating},
        {TorrentState::CheckingResumeData,  statusCheckingResumeData},
        {TorrentState::CheckingDownloading, statusChecking},
        {TorrentState::CheckingUploading,   statusChecking},
        {TorrentState::Downloading,         statusDownloading},
        {TorrentState::DownloadingMetadata, statusDownloading},
        {TorrentState::Error,               statusError},
        {TorrentState::Uploading,           statusFinished},
        {TorrentState::ForcedUploading,     statusFinished},
        {TorrentState::StalledUploading,    statusFinished},
        {TorrentState::QueuedUploading,     statusFinished},
        {TorrentState::PausedUploading,     statusFinished},
        {TorrentState::ForcedDownloading,   statusForcedDownloading},
        {TorrentState::MissingFiles,        statusMissingFiles},
        {TorrentState::Moving,              statusMoving},
        {TorrentState::PausedDownloading,   statusPaused},
        {TorrentState::QueuedDownloading,   statusQueued},
        {TorrentState::StalledDownloading,  statusStalled},
        {TorrentState::Unknown,             statusUnknown},
    };
    return cached;
}

/* Section for the StatusTextHash class. */

StatusTextHash *StatusTextHash::m_instance = nullptr;

StatusTextHash *StatusTextHash::instance()
{
    if (!m_instance)
        m_instance = new StatusTextHash();
    return m_instance;
}

void StatusTextHash::freeInstance()
{
    if (m_instance == nullptr)
        return;

    delete m_instance;
    m_instance = nullptr;
}

QHash<TorrentStatus, QString> &
StatusTextHash::getStatusTextHash() const
{
    static QHash<TorrentStatus, QString> cached
    {
        {TorrentStatus::Allocating,         QStringLiteral("Allocating")},
        {TorrentStatus::Checking,           QStringLiteral("Checking")},
        {TorrentStatus::CheckingResumeData, QStringLiteral("CheckingResumeData")},
        {TorrentStatus::Downloading,        QStringLiteral("Downloading")},
        {TorrentStatus::Error,              QStringLiteral("Error")},
        {TorrentStatus::Finished,           QStringLiteral("Finished")},
        {TorrentStatus::ForcedDownloading,  QStringLiteral("ForcedDownloading")},
        {TorrentStatus::MissingFiles,       QStringLiteral("MissingFiles")},
        {TorrentStatus::Moving,             QStringLiteral("Moving")},
        {TorrentStatus::Paused,             QStringLiteral("Paused")},
        {TorrentStatus::Queued,             QStringLiteral("Queued")},
        {TorrentStatus::Stalled,            QStringLiteral("Stalled")},
        {TorrentStatus::Unknown,            QStringLiteral("Unknown")},
    };
    return cached;
}

uint Export::qHash(const TorrentStatus &torrentStatus, const uint seed)
{
    return ::qHash(static_cast<int>(torrentStatus), seed);
}
