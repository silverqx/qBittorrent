#include "torrentexporter.h"

#include <qt_windows.h>
#include <Psapi.h>

#include <regex>

#include "app/application.h"
#include "base/bittorrent/session.h"
#include "base/export/exutils.h"
#include "base/export/service/storagemovefinishedservice.h"
#include "base/export/service/torrentaddedservice.h"
#include "base/export/service/torrentupdatedservice.h"
#include "base/utils/fs.h"
#include "commonglobal.h"
#include "gui/mainwindow.h"

using namespace Export;

// TODO fix _DLL, BOOST_ALL_DYN_LINK and clang analyze, also /MD vs /MT silverqx
// https://www.boost.org/doc/libs/1_74_0/boost/system/config.hpp
// https://www.boost.org/doc/libs/1_74_0/boost/config/auto_link.hpp
// https://docs.microsoft.com/en-us/cpp/build/reference/md-mt-ld-use-run-time-library?view=vs-2019
// May be relevant, do not know:
// https://stackoverflow.com/questions/9527713/mixing-a-dll-boost-library-with-a-static-runtime-is-a-really-bad-idea
namespace {
    TorrentExporter *l_torrentExporter = nullptr;

    BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM)
    {
        // For reference, very important:
        // https://docs.microsoft.com/en-us/archive/msdn-magazine/2015/july/c-using-stl-strings-at-win32-api-boundaries
        const int windowTextLength = ::GetWindowTextLength(hwnd) + 1;
        auto windowText = std::make_unique<wchar_t[]>(windowTextLength);

        ::GetWindowText(hwnd, windowText.get(), windowTextLength);
        std::wstring text(windowText.get());

        // Example: qMedia v4.2.5 or qMedia v4.2.5beta3
        const std::wregex re(L"^( {0,5}qMedia) (v\\d+\\.\\d+\\.\\d+([a-zA-Z]+\\d{0,2})?)$");
        if (!std::regex_match(windowText.get(), re))
            return true;

        DWORD pid;
        ::GetWindowThreadProcessId(hwnd, &pid);
        const HANDLE processHandle = ::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, false, pid);
        if (processHandle == NULL) {
            qDebug() << "OpenProcess() in EnumWindows() failed : "
                     << ::GetLastError();
            return true;
        }

        wchar_t moduleFilePath[MAX_PATH];
        ::GetModuleFileNameEx(processHandle, NULL, moduleFilePath, ARRAYSIZE(moduleFilePath));
        // More instances of qBittorrent can run, so find proper one
#ifdef QT_DEBUG
        // String has to start with moduleFileName
        if (::wcsstr(moduleFilePath, L"E:\\c\\qMedia\\build-qMedia-")
            != &moduleFilePath[0])
            return true;
#else
        if (::wcsstr(moduleFilePath, L"C:\\optx64\\qMedia") != &moduleFilePath[0])
            return true;
#endif
        const auto moduleFileName = Utils::Fs::fileName(QString::fromWCharArray(moduleFilePath));
        // TODO create finally helper https://www.modernescpp.com/index.php/c-core-guidelines-when-you-can-t-throw-an-exception silverqx
        // Or https://www.codeproject.com/Tips/476970/finally-clause-in-Cplusplus
        ::CloseHandle(processHandle);
        if (moduleFileName != "qMedia.exe")
            return true;

        qDebug() << "HWND for qMedia window found :"
                 << hwnd;
        l_torrentExporter->setQMediaHwnd(hwnd);

        return false;
    };
}

TorrentExporter *TorrentExporter::m_instance = nullptr;

TorrentExporter::TorrentExporter()
    : m_torrentsToCommit(
          QSharedPointer<TorrentHandleByInfoHashHash>(new TorrentHandleByInfoHashHash))
    , m_dbCommitTimer(new QTimer(this))
    , m_torrentsRepository(m_em.getRepository<TorrentsRepository>())
{
    l_torrentExporter = this;

    // Initialize the commit timer for added torrents
    m_dbCommitTimer->setSingleShot(true);
    connect(m_dbCommitTimer, &QTimer::timeout,
            this, &TorrentExporter::commitTorrentsTimerTimeout);

    // Connect events
    connect(BitTorrent::Session::instance(), &BitTorrent::Session::torrentAdded,
            this, &TorrentExporter::handleTorrentAdded);
    connect(BitTorrent::Session::instance(), &BitTorrent::Session::torrentDeleted,
            this, &TorrentExporter::handleTorrentDeleted);
    connect(BitTorrent::Session::instance(), &BitTorrent::Session::torrentsUpdated,
            this, &TorrentExporter::handleTorrentsUpdated);
    connect(BitTorrent::Session::instance(), &BitTorrent::Session::torrentStorageMoveFinished,
            this, &TorrentExporter::handleTorrentStorageMoveFinished);

    // Find qMedia's main window HWND
    ::EnumWindows(EnumWindowsProc, NULL);
    // Send hwnd of MainWindow to qMedia, aka. inform that qBittorrent is running
    if (m_qMediaHwnd != nullptr)
        ::PostMessage(m_qMediaHwnd, ::MSG_QBITTORRENT_UP,
            (WPARAM) dynamic_cast<Application *>(qApp)->mainWindow()->winId(), NULL);
}

TorrentExporter::~TorrentExporter()
{
    m_torrentsRepository->correctTorrentStatusesOnExit();
    m_torrentsRepository->correctTorrentPeersOnExit();

    StatusHash::freeInstance();
    StatusTextHash::freeInstance();

    if (m_qMediaHwnd != nullptr)
        ::PostMessage(m_qMediaHwnd, ::MSG_QBITTORRENT_DOWN, NULL, NULL);
}

void TorrentExporter::initInstance()
{
    if (!m_instance)
        m_instance = new TorrentExporter();
}

void TorrentExporter::freeInstance()
{
    delete m_instance;
    m_instance = nullptr;
}

TorrentExporter *TorrentExporter::instance()
{
    return m_instance;
}

void TorrentExporter::handleTorrentAdded(BitTorrent::TorrentHandle *const torrent)
{
    if (!ExUtils::torrentContainsPreviewableFiles(torrent))
        return;

    qDebug("Add torrent(%s) to db : \"%s\"",
           qUtf8Printable(static_cast<QString>(torrent->hash())),
           qUtf8Printable(torrent->name()));

//    auto db = QSqlDatabase::database();
//    if (!pingDatabase(db))
//        return m_changedPropertiesStore.enqueue(
//                    StoreItemHandle(
//                        std::bind(&TorrentExporter::correctTorrentStatusesOnExit, this),
//                        torrent));

    m_torrentsToCommit->insert(torrent->hash(), torrent);
    // Start or Restart Timer
    m_dbCommitTimer->start(COMMIT_INTERVAL_BASE);
}

void TorrentExporter::handleTorrentDeleted(BitTorrent::InfoHash infoHash) const
{
//    if (!QSqlDatabase::database().isOpen()) {
//        qWarning() << "No active database connection, torrent additions / removes will not be handled";
//        return;
//    }
    // BUG check if torrent contains previewable files or if torrent is in database silverqx

    qDebug() << "Remove torrent from db :"
             << static_cast<QString>(infoHash);

    m_torrentsRepository->removeTorrentFromDb(infoHash);

    if (m_qMediaHwnd == nullptr)
        return;

    ::PostMessage(m_qMediaHwnd, ::MSG_QBT_TORRENT_REMOVED, NULL, NULL);
}

void TorrentExporter::commitTorrentsTimerTimeout()
{
    if (!m_em.pingDatabase())
        // Try to commit later
        return deferCommitTimerTimeout();

    TorrentAddedService(m_em, m_torrentsToCommit).handleTorrentAdded();

    m_torrentsToCommit->clear();

    if (m_qMediaHwnd == nullptr)
        return;

    ::PostMessage(m_qMediaHwnd, ::MSG_QBT_TORRENTS_ADDED, NULL, NULL);
}

void TorrentExporter::handleTorrentsUpdated(
        const QVector<BitTorrent::TorrentHandle *> &torrents)
{
    const auto torrentHashes = TorrentUpdatedService(m_em).handleTorrentsUpdated(torrents);

    // Don't send message updates about torrents changed, when the qMedia is not in foreground
    if (m_qMediaHwnd == nullptr || m_qMediaWindowActive == false)
        return;

    // Serialize torrent hashes for WM_COPYDATA ( std::string is perfect for this 😂 )
    std::string torrentHashesData;
    for (const auto &infoHash : torrentHashes)
        torrentHashesData += static_cast<QString>(infoHash).toStdString();
    ::IpcSendStdString(m_qMediaHwnd, ::MSG_QBT_TORRENTS_CHANGED, torrentHashesData);
}

void TorrentExporter::handleTorrentStorageMoveFinished(
        const BitTorrent::TorrentHandle *const torrent, const QString &newPath)
{
    // Process only previewable torrents
    if (!ExUtils::torrentContainsPreviewableFiles(torrent))
        return;

    StorageMoveFinishedService(m_em).handleTorrentStorageMoveFinished(torrent, newPath);

    // Don't send message update about torrent moved, when the qMedia is not running
    if (m_qMediaHwnd == nullptr)
        return;

    // Inform qMedia about moved torrent
    ::IpcSendInfoHash(m_qMediaHwnd, ::MSG_QBT_TORRENT_MOVED, torrent->hash());
}
