#include "torrentexporter.h"

#include <QDebug>
#include <QtSql/QSqlDriver>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlRecord>

#include <qt_windows.h>
#include <Psapi.h>

#include <regex>

#include "mysql.h"

#include "app/application.h"
#include "base/bittorrent/infohash.h"
#include "base/bittorrent/session.h"
#include "base/bittorrent/torrenthandle.h"
#include "base/utils/fs.h"
#include "base/utils/misc.h"
#include "base/utils/string.h"
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

#ifdef QT_DEBUG
    Q_DECL_UNUSED
    const auto parseExecutedQuery = [](const QSqlQuery &query)
    {
        auto executedQuery = query.executedQuery();
        const auto boundValues = query.boundValues().values();
        for (int i = 0; i < boundValues.size(); ++i) {
            const auto boundValueRaw = boundValues.at(i);
            // Support for string quoting
            const auto boundValue = (boundValueRaw.type() == QVariant::Type::String)
                                    ? QStringLiteral("\"%1\"").arg(boundValueRaw.toString())
                                    : boundValueRaw.toString();
            executedQuery.replace(executedQuery.indexOf('?'), 1, boundValue);
        }

        return executedQuery;
    };
#endif

#ifndef PARSE_EXECUTED_QUERY
#  ifdef QT_DEBUG
#    define PARSE_EXECUTED_QUERY(query) parseExecutedQuery(query)
#  else
#    define PARSE_EXECUTED_QUERY(query) \
        "PARSE_EXECUTED_QUERY() macro enabled only in QT_DEBUG mode."
#  endif
#endif

#ifdef QT_DEBUG
#  ifndef Q_CC_MSVC
    Q_NORETURN
#  endif
    Q_DECL_UNUSED
    const auto logExecutedQuery = [](const QSqlQuery &query)
    {
        qDebug().noquote() << QStringLiteral("Executed Query :")
                           << parseExecutedQuery(query);
    };
#endif

#ifndef LOG_EXECUTED_QUERY
#  ifdef QT_DEBUG
#    define LOG_EXECUTED_QUERY(query) logExecutedQuery(query)
#  else
#    define LOG_EXECUTED_QUERY(query) qt_noop()
#  endif
#endif

    /*! Find out if torrent contains previewable files. */
    const auto torrentContainsPreviewableFiles =
            [](const BitTorrent::TorrentHandle *const torrent)
    {
        if (!torrent->hasMetadata())
            return false;

        for (int i = 0; i < torrent->filesCount(); ++i)
            if (Utils::Misc::isPreviewable(Utils::Fs::fileExtension(torrent->fileName(i))))
                return true;

        return false;
    };

    /*! Obtain progress value as QString. */
    const auto progressString = [](qreal progress) -> QString
    {
        progress *= 1000;
        return (static_cast<int>(progress) == 1000)
                ? QString::fromLatin1("1000")
                : Utils::String::fromDouble(progress, 0);
    };

    /*! Filter out non previewable torrents. */
    const auto filterPreviewableTorrents =
            [](const QVector<BitTorrent::TorrentHandle *> &torrents)
            -> TorrentExporter::TorrentHandleByInfoHashHash
    {
        TorrentExporter::TorrentHandleByInfoHashHash result;
        result.reserve(torrents.size());
        std::for_each(torrents.constBegin(), torrents.constEnd(),
                     [&result](const auto torrent)
        {
            if (torrentContainsPreviewableFiles(torrent))
                result.insert(torrent->hash(), torrent);
        });
        return result;
    };

    /*! Get the count of all previewable files in torrents hash. */
    const auto countPreviewableFiles =
            [](const TorrentExporter::TorrentHandleByIdHash &torrents) -> int
    {
        int previewableFilesCount = 0;
        for (const auto *const torrent : torrents)
            for (int i = 0; i < torrent->filesCount() ; ++i)
                if (Utils::Misc::isPreviewable(Utils::Fs::fileExtension(torrent->fileName(i))))
                    ++previewableFilesCount;
        return previewableFilesCount;
    };

    struct StatusProperties
    {
        TorrentStatus state;
        QString text;
    };

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

    /*! Map our custom TorrentStatus to the text representation. */
    inline const auto statusTextHash = []() -> const QHash<TorrentStatus, QString> &
    {
        static const QHash<TorrentStatus, QString> cached
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
    };

    /*! Map qBittorent TorrentState to our custom TorrentStatus used in qMedia. */
    inline const auto statusHash =
            []() -> const QHash<BitTorrent::TorrentState, StatusProperties> &
    {
        using TorrentState = BitTorrent::TorrentState;
        static const QHash<TorrentState, StatusProperties> cached
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
    };
}

TorrentExporter *TorrentExporter::m_instance = nullptr;
bool TorrentExporter::m_dbDisconnectedShowed = false;
bool TorrentExporter::m_dbConnectedShowed    = false;

TorrentExporter::TorrentExporter()
    : m_torrentsToCommit(
          QScopedPointer<TorrentHandleByInfoHashHash>(new TorrentHandleByInfoHashHash))
    , m_dbCommitTimer(new QTimer(this))
{
    l_torrentExporter = this;

    connectDatabase();

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
    correctTorrentStatusesOnExit();
    correctTorrentPeersOnExit();

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

void TorrentExporter::handleTorrentAdded(BitTorrent::TorrentHandle *const torrent) const
{
    if (!torrentContainsPreviewableFiles(torrent))
        return;

    qDebug("Add torrent(%s) to db : \"%s\"",
           qUtf8Printable(static_cast<QString>(torrent->hash())),
           qUtf8Printable(torrent->name()));

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

    qDebug() << "Remove torrent from db :"
             << static_cast<QString>(infoHash);

    removeTorrentFromDb(infoHash);
}

void TorrentExporter::commitTorrentsTimerTimeout()
{
    auto db = QSqlDatabase::database();
    if (!pingDatabase(db))
        // Try to commit later
        return deferCommitTimerTimeout();

    // Remove existing torrents from commit hash
    removeExistingTorrents();

    // Nothing to insert to DB
    if (m_torrentsToCommit->size() == 0)
        return;

    // Use transaction to guarantee data integrity.
    // I decided to use multi insert queries for better performance reasons, of course
    // it has its drawbacks.
    // If one of the insert queries fail, exception is throwed and data will be rollback-ed.
    // TODO use transaction with savepoints, like in a update strategy silverqx
    // Use transaction to guarantee data integrity
    db.transaction();
    try {
        // Multi insert for torrents
        insertTorrentsToDb();
        // Multi insert for previewable torrent files
        insertPreviewableFilesToDb();
        db.commit();
    }  catch (const ExporterError &e) {
        db.rollback();
        qCritical() << "Critical in commitTorrentsTimerTimeout() :"
                    << e.what();
        return;
    }

    m_torrentsToCommit->clear();

    if (m_qMediaHwnd == nullptr)
        return;

    ::PostMessage(m_qMediaHwnd, ::MSG_QBT_TORRENTS_ADDED, NULL, NULL);
}

void TorrentExporter::handleTorrentsUpdated(
        const QVector<BitTorrent::TorrentHandle *> &torrents)
{
    // Filter out non previewable torrents
    const auto previewableTorrents = filterPreviewableTorrents(torrents);
    // Nothing to update in DB
    if (previewableTorrents.size() == 0)
        return;

    // TODO record updated torrents, if db is disconnected, so we don't lose state silverqx

    auto db = QSqlDatabase::database();
    /* If the database disconnects, then the changed properties remain saved in the store
       and will be processed later, when the database connects back. */
    if (!pingDatabase(db))
        return;

    // Define hashes here and pass them to the fill method that fills them.
    // I tried std::optional() / std::tuple(), nothing worked good, this is the best solution.
    TorrentsChangedHash torrentsChangedProperties;
    TorrentsFilesChangedHash torrentsFilesChangedProperties;
    // Nothing to update
    if (!fillTorrentsChangedProperties(previewableTorrents,
                                       torrentsChangedProperties,
                                       torrentsFilesChangedProperties))
        return;

    // Use transaction to guarantee data integrity
    db.transaction();
    try {
        // Update torrents and previewable files with changed properties
        updateTorrentsInDb(torrentsChangedProperties, torrentsFilesChangedProperties);
        db.commit();
    }  catch (const ExporterError &e) {
        db.rollback();
        qCritical() << "Critical in handleTorrentsUpdated() :"
                    << e.what();
        return;
    }

    // Don't send message updates about torrents changed, when the qMedia is not in foreground
    if (m_qMediaHwnd == nullptr || m_qMediaWindowActive == false)
        return;

    // Serialize torrent hashes for WM_COPYDATA ( std::string is perfect for this 😂 )
    std::string torrentHashesData;
    for (const auto &torrent : previewableTorrents)
        torrentHashesData += static_cast<QString>(torrent->hash()).toStdString();
    ::IpcSendStdString(m_qMediaHwnd, ::MSG_QBT_TORRENTS_CHANGED, torrentHashesData);
}

void TorrentExporter::handleTorrentStorageMoveFinished(
        const BitTorrent::TorrentHandle *const torrent, const QString &newPath) const
{
    // Process only previewable torrents
    if (!torrentContainsPreviewableFiles(torrent))
        return;

    const auto infoHash = torrent->hash();
    const auto torrents =
            mapTorrentHandleById(TorrentHandleByInfoHashHash({{infoHash, torrent}}));
    const auto torrentsSize = torrents.size();
    const auto torrentName = torrent->name();
    // Torrent isn't in db
    if (torrentsSize == 0) {
        qDebug() << "Torrent isn't in db, in handleTorrentStorageMoveFinished(), this "
                    "should never have happen :/ :"
                 << torrentName;
        return;
    } else if (torrentsSize > 1) {
        qDebug() << "Wtf, 'torrentsSize > 1', more torrents selected :"
                 << torrentName;
        return;
    }

    const auto newPathFixed = Utils::Fs::toUniformPath(QDir::cleanPath(newPath));

    updateTorrentSaveDirInDb((torrents.constBegin()).key(), newPathFixed, torrentName);

    // Don't send message update about torrent moved, when the qMedia is not running
    if (m_qMediaHwnd == nullptr)
        return;

    // Inform qMedia about moved torrent
    ::IpcSendInfoHash(m_qMediaHwnd, ::MSG_QBT_TORRENT_MOVED, infoHash);
}

void TorrentExporter::connectDatabase() const
{
    auto db = QSqlDatabase::addDatabase("QMYSQL");
    db.setHostName("127.0.0.1");
#ifdef QT_DEBUG
    db.setDatabaseName("q_media_test");
#else
    db.setDatabaseName("q_media");
#endif
    db.setUserName("szachara");
    db.setPassword("99delfinu*");

    if (db.open())
        return;

    qDebug() << "Connect to DB failed :"
             << db.lastError().text();
}

void TorrentExporter::removeTorrentFromDb(const BitTorrent::InfoHash &infoHash) const
{
    QSqlQuery query;
    query.prepare(QStringLiteral("DELETE FROM torrents WHERE hash = :hash"));
    query.bindValue(":hash", static_cast<QString>(infoHash));

    const auto ok = query.exec();
    if (!ok) {
        qDebug("Delete query in removeTorrentFromDb() for torrent(%s) failed : \"%s\"",
               qUtf8Printable(static_cast<QString>(infoHash)),
               qUtf8Printable(query.lastError().text()));
        return;
    }

    if (m_qMediaHwnd == nullptr)
        return;

    ::PostMessage(m_qMediaHwnd, ::MSG_QBT_TORRENT_REMOVED, NULL, NULL);
}

void TorrentExporter::insertTorrentsToDb() const
{
    // Assemble query binding placeholders for multi insert on the base of count
    // of torrents to insert.
    auto placeholders = QStringLiteral("(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?), ")
                        .repeated(m_torrentsToCommit->size());
    // Will never be empty, is checked earlier
    placeholders.chop(2);

    const auto torrentsQueryString =
            QStringLiteral("INSERT INTO torrents (name, progress, eta, size, seeds, "
                           "total_seeds, leechers, total_leechers, remaining, added_on, "
                           "hash, status, savepath) "
                           "VALUES %1")
            .arg(placeholders);

    QSqlQuery torrentsQuery;
    torrentsQuery.prepare(torrentsQueryString);

    // Prepare query bindings for torrents
    const BitTorrent::TorrentHandle *torrent;
    auto itTorrents = m_torrentsToCommit->constBegin();
    while (itTorrents != m_torrentsToCommit->constEnd()) {
        torrent = itTorrents.value();
#ifdef QT_DEBUG
        const auto torrentName = torrent->name();
        Q_UNUSED(torrentName);
#endif

        torrentsQuery.addBindValue(torrent->name());
        torrentsQuery.addBindValue(progressString(torrent->progress()));
        torrentsQuery.addBindValue(torrent->eta());
        torrentsQuery.addBindValue(torrent->totalSize());
        torrentsQuery.addBindValue(torrent->seedsCount());
        torrentsQuery.addBindValue(torrent->totalSeedsCount());
        torrentsQuery.addBindValue(torrent->leechsCount());
        torrentsQuery.addBindValue(torrent->totalLeechersCount());
        torrentsQuery.addBindValue(torrent->incompletedSize());
        torrentsQuery.addBindValue(torrent->addedTime());
        torrentsQuery.addBindValue(QString(torrent->hash()));
        torrentsQuery.addBindValue(statusHash().value(torrent->state()).text);
        torrentsQuery.addBindValue(
                    Utils::Fs::toUniformPath(QDir::cleanPath(torrent->savePath(true))));

        ++itTorrents;
    }

    const auto ok = torrentsQuery.exec();
    if (ok)
        return;

    qDebug() << "Insert torrents in insertTorrentsToDb() failed :"
             << torrentsQuery.lastError().text();
    throw ExporterError("Insert torrents in insertTorrentsToDb() failed.");
}

void TorrentExporter::removeExistingTorrents()
{
    // Assemble query binding placeholders
    auto placeholders = QStringLiteral("?, ").repeated(m_torrentsToCommit->size());
    // Will never be empty, timer is triggered only after new torrent is added
    placeholders.chop(2);
    const auto queryString = QStringLiteral("SELECT hash FROM torrents WHERE hash IN (%1)")
                             .arg(placeholders);

    QSqlQuery query;
    query.setForwardOnly(true);
    query.prepare(queryString);

    // Prepare query bindings
    auto itTorrent = m_torrentsToCommit->constBegin();
    while (itTorrent != m_torrentsToCommit->constEnd()) {
        query.addBindValue(static_cast<QString>(itTorrent.key()));
        ++itTorrent;
    }

    const auto ok = query.exec();
    if (!ok)
        qDebug() << "Select torrents in removeDuplicitTorrents() failed :"
                 << query.lastError().text();

    // Any duplicit torrents in DB
    if (query.size() <= 0)
        return;

    // Remove duplicit torrents from commit hash
    while (query.next()) {
        BitTorrent::InfoHash hash(query.value(0).toString());
        if (m_torrentsToCommit->contains(hash))
            m_torrentsToCommit->remove(hash);
    }
}

void TorrentExporter::insertPreviewableFilesToDb() const
{
    const auto insertedTorrents = selectTorrentIdsToCommitByHashes(
        m_torrentsToCommit->keys()
    );

    if (insertedTorrents.size() == 0) {
        qDebug() << "Inserted torrents count is 0, this should never have happen :/";
        return;
    }

    // Assemble query binding placeholders for multi insert on the base of
    // count of the torrents to insert.
    // Everything will be inserted in one insert query.
    auto placeholders = QStringLiteral("(?, ?, ?, ?, ?), ")
                        .repeated(countPreviewableFiles(insertedTorrents));
    // Will never be empty, is checked above
    placeholders.chop(2);

    const auto previewableFilesQueryString =
        QStringLiteral("INSERT INTO torrents_previewable_files "
                       "(torrent_id, file_index, filepath, size, progress) "
                       "VALUES %1")
        .arg(placeholders);

    QSqlQuery previewableFilesQuery;
    previewableFilesQuery.prepare(previewableFilesQueryString);

    // Prepare query bindings for torrents_previewable_files
    const BitTorrent::TorrentHandle *torrent;
    QVector<qreal> filesProgress;
    auto itInsertedTorrents = insertedTorrents.constBegin();
    while (itInsertedTorrents != insertedTorrents.constEnd()) {
        torrent = itInsertedTorrents.value();
#ifdef QT_DEBUG
        const auto torrentName = torrent->name();
        Q_UNUSED(torrentName);
#endif

        filesProgress = torrent->filesProgress();

        for (int i = 0; i < torrent->filesCount(); ++i) {
            if (!Utils::Misc::isPreviewable(Utils::Fs::fileExtension(torrent->fileName(i))))
                continue;

            previewableFilesQuery.addBindValue(itInsertedTorrents.key());
            previewableFilesQuery.addBindValue(i);
            previewableFilesQuery.addBindValue(torrent->filePath(i));
            previewableFilesQuery.addBindValue(torrent->fileSize(i));
            previewableFilesQuery.addBindValue(progressString(filesProgress[i]));
        }

        ++itInsertedTorrents;
    }

    const auto ok = previewableFilesQuery.exec();
    if (ok)
        return;

    qDebug() << "Insert torrent files in insertPreviewableFilesToDb() failed :"
             << previewableFilesQuery.lastError().text();
    throw ExporterError("Insert torrent files in insertPreviewableFilesToDb() failed.");
}

TorrentExporter::TorrentHandleByIdHash
TorrentExporter::selectTorrentIdsToCommitByHashes(
        const QList<BitTorrent::InfoHash> &hashes) const
{
    // Prepare binding placeholders
    auto placeholders = QStringLiteral("?, ").repeated(hashes.size());
    placeholders.chop(2);

    const auto queryString = QStringLiteral("SELECT id, hash "
                                            "FROM torrents WHERE hash IN (%1)")
                             .arg(placeholders);

    QSqlQuery query;
    query.setForwardOnly(true);
    query.prepare(queryString);

    // Prepare query bindings
    auto itHashes = hashes.constBegin();
    while (itHashes != hashes.constEnd()) {
        query.addBindValue(static_cast<QString>(*itHashes));
        ++itHashes;
    }

    const auto ok = query.exec();
    if (!ok) {
        qDebug() << "Select query in selectTorrentIdsToCommitByHashes() failed :"
                 << query.lastError().text();
        return {};
    }

    // Create new QHash of selected torrents
    TorrentHandleByIdHash torrents;
    while (query.next()) {
        BitTorrent::InfoHash hash(query.value("hash").toString());
        if (m_torrentsToCommit->contains(hash))
            torrents.insert(
                query.value("id").toULongLong(),
                m_torrentsToCommit->value(hash)
            );
    }

    return torrents;
}

TorrentExporter::TorrentHandleByIdHash
TorrentExporter::mapTorrentHandleById(const TorrentHandleByInfoHashHash &torrents) const
{
    // Prepare binding placeholders
    auto placeholders = QStringLiteral("?, ").repeated(torrents.size());
    placeholders.chop(2);
    const auto queryString = QStringLiteral("SELECT id, hash FROM torrents WHERE hash IN (%1)")
                             .arg(placeholders);

    QSqlQuery query;
    query.setForwardOnly(true);
    query.prepare(queryString);

    // Prepare query bindings
    auto itTorrents = torrents.constBegin();
    while (itTorrents != torrents.constEnd()) {
        query.addBindValue(static_cast<QString>((*itTorrents)->hash()));
        ++itTorrents;
    }

    const auto ok = query.exec();
    if (!ok) {
        qDebug() << "Select query in mapTorrentHandleById() failed :"
                 << query.lastError().text();
        return {};
    }

    // Create new QHash of selected torrents
    TorrentHandleByIdHash torrentsHash;
    while (query.next()) {
        // Find torrent handle by info hash
        BitTorrent::InfoHash hash(query.value(1).toString());
        const auto itTorrentHandle =
                std::find_if(torrents.constBegin(), torrents.constEnd(),
                             [&hash](const BitTorrent::TorrentHandle *const torrent)
        {
            return (torrent->hash() == hash) ? true : false;
        });
        // Insert
        torrentsHash.insert(
            query.value("id").toULongLong(),
            *itTorrentHandle
        );
    }

    return torrentsHash;
}

std::tuple<const TorrentExporter::TorrentHandleByIdHash,
           const TorrentExporter::TorrentSqlRecordByIdHash>
TorrentExporter::selectTorrentsByHandles(
        const TorrentHandleByInfoHashHash &torrents, const QString &select
) const
{
    // Prepare binding placeholders
    auto placeholders = QStringLiteral("?, ").repeated(torrents.size());
    // Will never be empty, is checked earlier
    placeholders.chop(2);
    /* Qt don't know how to iterate result with json column, so I have to manually manage
       columns to select. */
    const auto selectManaged = select == '*'
                               ? QStringLiteral("id, name, progress, eta, size, seeds, "
                                                "total_seeds, leechers, total_leechers, "
                                                "remaining, added_on, hash, status, "
                                                "movie_detail_index, savepath")
                               : select;
    const auto queryString = QStringLiteral("SELECT %2 FROM torrents WHERE hash IN (%1)")
                             .arg(placeholders, selectManaged);

    QSqlQuery query;
    query.setForwardOnly(true);
    query.prepare(queryString);

    // Prepare query bindings
    auto itTorrents = torrents.constBegin();
    while (itTorrents != torrents.constEnd()) {
        query.addBindValue(static_cast<QString>(itTorrents.key()));
        ++itTorrents;
    }

    const auto ok = query.exec();
    if (!ok) {
        qDebug() << "Select query in selectTorrentsByHandles() failed :"
                 << query.lastError().text();
        return {};
    }

    // Create new QHash of selected torrents
    TorrentHandleByIdHash torrentsHash;
    TorrentSqlRecordByIdHash torrentRecords;
    while (query.next()) {
        // Find torrent handle by info hash
        BitTorrent::InfoHash hash(query.value("hash").toString());
        const auto itTorrentHandle = std::find_if(torrents.constBegin(), torrents.constEnd(),
                                                  [&hash](auto *const torrent)
        {
            return (torrent->hash() == hash) ? true : false;
        });
        // Insert
        const auto torrentId = query.value("id").toULongLong();
        torrentsHash.insert(
            torrentId,
            *itTorrentHandle
        );
        torrentRecords.insert(torrentId, query.record());
    }

    return {torrentsHash, torrentRecords};
}

TorrentExporter::TorrentFileSqlRecordByIdHash
TorrentExporter::selectTorrentsFilesByHandles(
        const TorrentHandleByIdHash &torrentsUpdated) const
{
    // Prepare binding placeholders
    auto placeholders = QStringLiteral("?, ").repeated(torrentsUpdated.size());
    // Will never be empty, is checked earlier
    placeholders.chop(2);
    const auto queryString =
            QStringLiteral("SELECT id, torrent_id, file_index, filepath, size, progress "
                           "FROM torrents_previewable_files "
                           "WHERE torrent_id IN (%1)")
            .arg(placeholders);

    QSqlQuery query;
    query.setForwardOnly(true);
    query.prepare(queryString);

    // Prepare query bindings
    auto itTorrents = torrentsUpdated.constBegin();
    while (itTorrents != torrentsUpdated.constEnd()) {
        query.addBindValue(itTorrents.key());
        ++itTorrents;
    }

    const auto ok = query.exec();
    if (!ok) {
        qDebug() << "Select query in selectTorrentsFilesByHandles() failed :"
                 << query.lastError().text();
        return {};
    }

    // Create new QHash of selected torrent files
    TorrentFileSqlRecordByIdHash torrentFilesInDb;
    while (query.next()) {
        const auto torrentId = query.value("torrent_id").toULongLong();
        QSharedPointer<QHash<TorrentFileIndex, QSqlRecord>> torrentFiles;
        // Obtain existing or create new hash if doesn't exist
        if (torrentFilesInDb.contains(torrentId))
            torrentFiles = torrentFilesInDb.value(torrentId);
        else
            torrentFiles.reset(new QHash<TorrentFileIndex, QSqlRecord>);
        torrentFiles->insert(
                    query.value("file_index").toInt(),
                    query.record());
        torrentFilesInDb.insert(torrentId, torrentFiles);
    }

    return torrentFilesInDb;
}

QHash<TorrentExporter::TorrentId, BitTorrent::InfoHash>
TorrentExporter::selectTorrentsByStatuses(const QList<TorrentStatus> &statuses) const
{
    // Prepare binding placeholders
    auto placeholders = QStringLiteral("?, ").repeated(statuses.size());
    placeholders.chop(2);

    QSqlQuery query;
    query.setForwardOnly(true);
    query.prepare(QStringLiteral("SELECT id, hash "
                                 "FROM torrents WHERE status IN (%1)")
                  .arg(placeholders));

    // Prepare query bindings
    auto itStatuses = statuses.constBegin();
    while (itStatuses != statuses.constEnd()) {
        query.addBindValue(statusTextHash().value(*itStatuses));
        ++itStatuses;
    }

    const auto ok = query.exec();
    if (!ok) {
        qDebug() << "Select query in selectTorrentsByStatuses() failed :"
                 << query.lastError().text();
        return {};
    }

    // Create new QHash of selected torrents
    QHash<TorrentId, BitTorrent::InfoHash> torrents;
    while (query.next()) {
        BitTorrent::InfoHash hash(query.value("hash").toString());
        torrents.insert(query.value("id").toULongLong(), hash);
    }

    return torrents;
}

void TorrentExporter::updateTorrentsInDb(
        const TorrentsChangedHash &torrentsChangedHash,
        const TorrentsFilesChangedHash &torrentsFilesChangedHash
) const
{
    // Count successful updates
    int successUpdates = 0;

    const auto rollbackSavePoint = [](const auto torrentId)
    {
        QSqlQuery rollbackSavePoint;
        const auto query = QStringLiteral("ROLLBACK TO SAVEPOINT torrent_%1")
                           .arg(torrentId);
        Q_ASSERT_X(rollbackSavePoint.exec(query),
                   "updateTorrentsInDb()",
                   query.toLatin1().constData());
    };

    auto itTorrentChanged = torrentsChangedHash.constBegin();
    while (itTorrentChanged != torrentsChangedHash.constEnd()) {
        const auto torrentId = itTorrentChanged.key();
        const auto changedProperties = itTorrentChanged.value();

        ++itTorrentChanged;

        // If something unexpected happens, only currently processed torrent will be
        // rollback-ed, so savepoints are ideal for this.
        QSqlQuery savePoint;
        const auto query = QStringLiteral("SAVEPOINT torrent_%1").arg(torrentId);
        Q_ASSERT_X(savePoint.exec(query),
                   "updateTorrentsInDb()",
                   query.toLatin1().constData());
        try {
            // Torrents update
            updateTorrentInDb(torrentId, changedProperties);
            // Previewable files update
            if (torrentsFilesChangedHash.contains(torrentId))
#if LOG_CHANGED_TORRENTS
                updatePreviewableFilesInDb(torrentsFilesChangedHash.value(torrentId), torrentId);
#else
                updatePreviewableFilesInDb(torrentsFilesChangedHash.value(torrentId));
#endif
        } catch (const ExporterError &e) {
            rollbackSavePoint(torrentId);
            qFatal("Critical in updateTorrentsInDb() : \"%s\"",
                   e.what());
        }

        ++successUpdates;
    }

    const auto torrentsChangedSize = torrentsChangedHash.size();
#if LOG_CHANGED_TORRENTS
    {
        // TODO add the same for files updates, to much work for now, fuck it 😂 silverqx
        const auto unsuccessUpdates = (torrentsChangedSize - successUpdates);
        if (unsuccessUpdates > 0)
            qWarning("%d torrents out of %d were not updated.",
                     unsuccessUpdates,
                     torrentsChangedSize);
    }
#endif

    if (successUpdates == 0)
        throw ExporterError(QStringLiteral("All '%1' updates was unsuccessful.")
                            .arg(torrentsChangedSize).toUtf8().constData());
}

void TorrentExporter::updateTorrentInDb(
        const TorrentId torrentId,
        const QSharedPointer<const TorrentChangedProperties> changedProperties
) const
{
    // Nothing to update
    if (changedProperties->isEmpty())
        return;

    // Prepare update SET column names and binding placeholders
    QString updateSetBindings = "";
    const auto changedPropertiesBegin = changedProperties->constBegin();
    const auto changedPropertiesEnd = changedProperties->constEnd();
    auto itChangedProperty = changedPropertiesBegin;
    while (itChangedProperty != changedPropertiesEnd) {
        updateSetBindings += QStringLiteral("%1 = ?, ").arg(itChangedProperty.key());
        ++itChangedProperty;
    }
    updateSetBindings.chop(2);

    // Create query
    const auto updateTorrentQuery = QStringLiteral("UPDATE torrents SET %1 WHERE id = ?")
                                    .arg(updateSetBindings);
    QSqlQuery query;
    query.prepare(updateTorrentQuery);

    // Prepare bindings
    itChangedProperty = changedPropertiesBegin;
    while (itChangedProperty != changedPropertiesEnd) {
        query.addBindValue(itChangedProperty.value());
        ++itChangedProperty;
    }
    query.addBindValue(torrentId);

    // Execute query
    const auto ok = query.exec();
    if (!ok)
        qDebug() << "Update query in updateTorrentInDb() failed :"
                 << query.lastError().text();

#if LOG_CHANGED_TORRENTS
    qDebug() << "Number of updated torrents :"
             << query.numRowsAffected();
    LOG_EXECUTED_QUERY(query);
#endif

    if (!ok)
        throw ExporterError("Update query in updateTorrentInDb() failed.");
}

#if LOG_CHANGED_TORRENTS
void TorrentExporter::updatePreviewableFilesInDb(
        const QSharedPointer<const TorrentFilesChangedHash> changedFilesProperties,
        const TorrentId torrentId) const
#else
void TorrentExporter::updatePreviewableFilesInDb(
        const QSharedPointer<const TorrentFilesChangedHash> changedFilesProperties) const
#endif
{
    // Handle nullptr
    if (changedFilesProperties.isNull())
        throw ExporterError("'changedFilesProperties == nullptr' in "
                            "updatePreviewableFilesInDb().");
    // Nothing to update
    const auto changedFilesPropertiesEmpty = changedFilesProperties->isEmpty();
    Q_ASSERT(!changedFilesPropertiesEmpty);
    if (changedFilesPropertiesEmpty)
        return;

    // Loop over all changed previewable files
    auto itChangedProperties = changedFilesProperties->constBegin();
    while (itChangedProperties != changedFilesProperties->constEnd()) {
        const auto torrentFileId = itChangedProperties.key();
        const auto changedProperties = itChangedProperties.value();

        // WANRNING should be incremented as soon as possible to avoid infinite loops, consider to change all while() to for(;;) to avoid this problem and also investigate alternatives silverqx
        ++itChangedProperties;

        // Prepare update SET column names and binding placeholders
        QString updateSetBindings = "";
        const auto changedPropertiesBegin = changedProperties->constBegin();
        const auto changedPropertiesEnd = changedProperties->constEnd();
        auto itChangedProperty = changedPropertiesBegin;
        while (itChangedProperty != changedPropertiesEnd) {
            updateSetBindings += QStringLiteral("%1 = ?, ").arg(itChangedProperty.key());
            ++itChangedProperty;
        }
        updateSetBindings.chop(2);

        // Create query
        const auto updateTorrentQuery =
                QStringLiteral("UPDATE torrents_previewable_files SET %1 "
                               "WHERE id = ?")
                .arg(updateSetBindings);

        QSqlQuery query;
        query.prepare(updateTorrentQuery);

        // Prepare bindings
        itChangedProperty = changedPropertiesBegin;
        while (itChangedProperty != changedPropertiesEnd) {
            query.addBindValue(itChangedProperty.value());
            ++itChangedProperty;
        }
        query.addBindValue(torrentFileId);

        // Execute query
        const auto ok = query.exec();
        if (!ok)
            qDebug() << "Update query in updatePreviewableFilesInDb() failed :"
                     << query.lastError().text();

#if LOG_CHANGED_TORRENTS
        qDebug("Number of updated torrent(ID%llu) files : %d",
               torrentId, query.numRowsAffected());
        LOG_EXECUTED_QUERY(query);
#endif

        if (!ok)
            throw ExporterError("Update query in updatePreviewableFilesInDb() failed.");
    }
}

void TorrentExporter::correctTorrentStatusesOnExit() const
{
    // Obtain torrents which has to be updated
    const auto statuses = QList<TorrentStatus> {TorrentStatus::Downloading};
    const auto torrents = selectTorrentsByStatuses(statuses);
    // Nothing to update
    if (torrents.isEmpty())
        return;

    // Prepare binding placeholders
    auto placeholders = QStringLiteral("?, ").repeated(torrents.size());
    placeholders.chop(2);

    QSqlQuery query;
    query.prepare(QStringLiteral("UPDATE torrents SET status = ? WHERE id IN (%1)")
                  .arg(placeholders));

    // Prepare bindings
    query.addBindValue(statusStalled.text);
    QHashIterator<TorrentId, BitTorrent::InfoHash> itTorrents(torrents);
    while (itTorrents.hasNext()) {
        itTorrents.next();
        query.addBindValue(itTorrents.key());
    }

    const auto ok = query.exec();
    if (!ok) {
        qDebug() << "Update in correctTorrentStatuses() failed :"
                 << query.lastError().text();
        return;
    }
}

void TorrentExporter::correctTorrentPeersOnExit() const
{
    QSqlQuery query;
    const auto ok = query.exec(QStringLiteral("UPDATE torrents "
                                              "SET seeds = 0, total_seeds = 0, "
                                              "leechers = 0, total_leechers = 0"));
    if (!ok) {
        qDebug() << "Update in correctTorrentPeersOnExit() failed :"
                 << query.lastError().text();
        return;
    }
}

void TorrentExporter::updateTorrentSaveDirInDb(
        const TorrentId torrentId, const QString &newPath,
        const QString &torrentName
) const
{
    qDebug("Updating savepath in db for torrent(ID%llu) : %s",
           torrentId, qUtf8Printable(torrentName));

    QSqlQuery query;
    query.prepare(QStringLiteral("UPDATE torrents SET savepath = ? WHERE id = ?"));

    query.addBindValue(newPath);
    query.addBindValue(torrentId);

    const auto ok = query.exec();
    if (!ok) {
        qDebug() << "Update savepath in updateTorrentSaveDirInDb() failed :"
                 << query.lastError().text();
        return;
    }
}

bool TorrentExporter::fillTorrentsChangedProperties(
        const TorrentHandleByInfoHashHash &torrents,
        TorrentsChangedHash &torrentsChangedProperties,
        TorrentsFilesChangedHash &torrentsFilesChangedProperties
) const
{
    // Create torrents hash keyed by torrent id, it's selected from db by torrent info hashes.
    // Needed because torrent id is needed in update query.
    // Also return actual QSqlRecords keyed by torrent id, which are needed to trace changed
    // properties.
    const auto [torrentsUpdated, torrentsInDb] =
            selectTorrentsByHandles(torrents, QStringLiteral("*"));
    // Nothing to update
    if (torrentsUpdated.size() == 0) {
        qDebug() << "Selected torrents by handles count is 0, in handleTorrentsUpdated(), "
                    "this should never have happen :/";
        return false;
    }

    // Create torrent files hash populated with the QSqlRecords keyed by torrent ids.
    // Needed to trace property changes in a torrent previewable files.
    const auto torrentsFilesInDb = selectTorrentsFilesByHandles(torrentsUpdated);

    // Find out changed properties in torrent and torrent files
    traceTorrentChangedProperties(torrentsUpdated, torrentsInDb,
                                  torrentsChangedProperties);
    traceTorrentFilesChangedProperties(torrentsUpdated, torrentsFilesInDb,
                                       torrentsFilesChangedProperties);
    // Nothing to update
    if (torrentsChangedProperties.isEmpty() && torrentsFilesChangedProperties.isEmpty())
        return false;

    return true;
}

void TorrentExporter::traceTorrentChangedProperties(
        const TorrentHandleByIdHash &torrentsUpdated,
        const TorrentSqlRecordByIdHash &torrentsInDb,
        TorrentsChangedHash &torrentsChangedProperties
) const
{
    auto itTorrentsHash = torrentsUpdated.constBegin();
    while (itTorrentsHash != torrentsUpdated.constEnd()) {
        /*! Torrent sent by qBittorrent as changed. */
        const auto torrentUpdated = itTorrentsHash.value();
        const auto torrentId = itTorrentsHash.key();
        /*! Torrent from db as QSqlRecord. */
        const auto torrentDb = torrentsInDb[torrentId];
        const auto torrentChangedProperties = QSharedPointer<QVariantHash>::create();

        // Determine if torrent properties was changed
        auto changed = false;

        const auto recordChangedProperty =
                [&torrentChangedProperties]
                (const auto &column, auto &wasChanged,
                const auto &updatedValue, const auto &dbValue)
        {
            if (updatedValue == dbValue)
                return;
            torrentChangedProperties->insert(column, updatedValue);
            wasChanged = true;
        };

        recordChangedProperty(QStringLiteral("name"), changed,
                              torrentUpdated->name(),
                              torrentDb.value("name").toString());
        recordChangedProperty(QStringLiteral("progress"), changed,
                              progressString(torrentUpdated->progress()).toInt(),
                              torrentDb.value("progress").toInt());
        recordChangedProperty(QStringLiteral("eta"), changed,
                              torrentUpdated->eta(),
                              torrentDb.value("eta").toLongLong());
        recordChangedProperty(QStringLiteral("size"), changed,
                              torrentUpdated->totalSize(),
                              torrentDb.value("size").toLongLong());
        recordChangedProperty(QStringLiteral("seeds"), changed,
                              torrentUpdated->seedsCount(),
                              torrentDb.value("seeds").toInt());
        recordChangedProperty(QStringLiteral("total_seeds"), changed,
                              torrentUpdated->totalSeedsCount(),
                              torrentDb.value("total_seeds").toInt());
        recordChangedProperty(QStringLiteral("leechers"), changed,
                              torrentUpdated->leechsCount(),
                              torrentDb.value("leechers").toInt());
        recordChangedProperty(QStringLiteral("total_leechers"), changed,
                              torrentUpdated->totalLeechersCount(),
                              torrentDb.value("total_leechers").toInt());
        recordChangedProperty(QStringLiteral("remaining"), changed,
                              torrentUpdated->incompletedSize(),
                              torrentDb.value("remaining").toLongLong());
        recordChangedProperty(QStringLiteral("status"), changed,
                              statusHash().value(torrentUpdated->state()).text,
                              torrentDb.value("status").toString());

        ++itTorrentsHash;

        // All properties are the same
        if (!changed)
            continue;

        torrentsChangedProperties.insert(torrentId, torrentChangedProperties);
    }
}

void TorrentExporter::traceTorrentFilesChangedProperties(
        const TorrentHandleByIdHash &torrentsUpdated,
        const TorrentFileSqlRecordByIdHash &torrentsFilesInDb,
        TorrentsFilesChangedHash &torrentsFilesChangedProperties
) const
{
    // TODO consider move to anonym. namespace, because duplicate code is also in traceTorrentChangedProperties() silverqx
    const auto recordChangedProperty =
            [](const auto &torrentFileChangedProperties, const auto &column,
            auto &wasChanged, const auto &updatedValue, const auto &dbValue)
    {
        if (updatedValue == dbValue)
            return;
        torrentFileChangedProperties->insert(column, updatedValue);
        wasChanged = true;
    };

    auto itTorrentsHash = torrentsUpdated.constBegin();
    while (itTorrentsHash != torrentsUpdated.constEnd()) {
        /*! Torrent sent by qBittorrent as changed. */
        const auto torrentUpdated = itTorrentsHash.value();
        const auto torrentId = itTorrentsHash.key();
        const auto filesProgress = torrentUpdated->filesProgress();
        // Skip if something unexpected happend
        const auto filesExist = torrentsFilesInDb.contains(torrentId);
        Q_ASSERT_X(filesExist, "traceTorrentFilesChangedProperties()",
                   "no files in torrentsFilesInDb");
        if (!filesExist)
            continue;
        /*! Torrent files from db as QSqlRecords QHash ( QSharedPointer ). */
        const auto torrentFilesInDb = torrentsFilesInDb.value(torrentId);
        const auto torrentFilesChangedProperties =
                QSharedPointer<TorrentFilesChangedHash>::create();

        for (const auto &torrentFileDb : qAsConst(*torrentFilesInDb)) {
            // Determine if torrent properties was changed
            auto changed = false;
            const auto torrentFileChangedProperties =
                    QSharedPointer<TorrentFileChangedProperties>::create();

            const auto fileIndex = torrentFileDb.value("file_index").toInt();

            recordChangedProperty(torrentFileChangedProperties,
                                  QStringLiteral("filepath"), changed,
                                  torrentUpdated->filePath(fileIndex),
                                  torrentFileDb.value("filepath").toString());
            recordChangedProperty(torrentFileChangedProperties,
                                  QStringLiteral("size"), changed,
                                  torrentUpdated->fileSize(fileIndex),
                                  torrentFileDb.value("size").toLongLong());
            recordChangedProperty(torrentFileChangedProperties,
                                  QStringLiteral("progress"), changed,
                                  progressString(filesProgress[fileIndex]).toInt(),
                                  torrentFileDb.value("progress").toInt());

            // All properties are the same
            if (!changed)
                continue;

            torrentFilesChangedProperties->insert(
                        torrentFileDb.value("id").toULongLong(),
                        torrentFileChangedProperties);
        }

        ++itTorrentsHash;

        // Nothing to update
        if (torrentFilesChangedProperties->isEmpty())
            continue;

        torrentsFilesChangedProperties.insert(torrentId, torrentFilesChangedProperties);
    }
}

void TorrentExporter::showDbDisconnected()
{
    if (m_dbDisconnectedShowed)
        return;
    m_dbDisconnectedShowed = true;

    // Reset connected flag
    m_dbConnectedShowed = false;

    qWarning() << "No active database connection, torrent additions / removes will "
                  "not be commited";
}

void TorrentExporter::showDbConnected()
{
    if (m_dbConnectedShowed)
        return;
    m_dbConnectedShowed = true;

    // Reset disconnected flag
    m_dbDisconnectedShowed = false;

    qInfo() << "Database connected";
}

bool TorrentExporter::pingDatabase(QSqlDatabase &db)
{
    const auto getMysqlHandle = [&db]() -> MYSQL *
    {
        auto driverHandle = db.driver()->handle();
        if (qstrcmp(driverHandle.typeName(), "MYSQL*") == 0)
            return *static_cast<MYSQL **>(driverHandle.data());
        return nullptr;
    };
    const auto mysqlPing = [getMysqlHandle]()
    {
        auto mysqlHandle = getMysqlHandle();
        if (mysqlHandle == nullptr)
            return false;
        auto ping = mysql_ping(mysqlHandle);
        auto errNo = mysql_errno(mysqlHandle);
        /* So strange logic, because I want interpret CR_COMMANDS_OUT_OF_SYNC errno as
           successful ping. */
        if ((ping != 0) && (errNo == CR_COMMANDS_OUT_OF_SYNC)) {
            // TODO log to file, how often this happen silverqx
            qWarning("mysql_ping() returned : CR_COMMANDS_OUT_OF_SYNC(%ud)", errNo);
            return true;
        }
        else if (ping == 0)
            return true;
        else if (ping != 0)
            return false;
        else {
            qWarning() << "Unknown behavior during mysql_ping(), this should never happen :/";
            return false;
        }
    };

    if (db.isOpen() && mysqlPing()) {
        showDbConnected();
        return true;
    }

    showDbDisconnected();
    // Database connection have to be closed manually
    // isOpen() check is called in MySQL driver
    db.close();
    return false;
}

uint Export::qHash(const TorrentStatus &torrentStatus, const uint seed)
{
    return ::qHash(static_cast<int>(torrentStatus), seed);
}
