#include "torrentexporter.h"

#include <QDateTime>
#include <QDebug>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlRecord>

#include <qt_windows.h>
#include <Psapi.h>

#include <regex>

#include "app/application.h"
#include "base/bittorrent/infohash.h"
#include "base/bittorrent/session.h"
#include "base/bittorrent/torrenthandle.h"
#include "base/torrentexportercommon.h"
#include "base/utils/fs.h"
#include "base/utils/misc.h"
#include "base/utils/string.h"
#include "gui/mainwindow.h"

using namespace BitTorrent;

namespace {
    const int COMMIT_INTERVAL = 1000;

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
            qDebug() << "OpenProcess() in EnumWindows() failed : " << ::GetLastError();
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
        if (::wcsstr(moduleFileName, L"C:\\optx64\\qMedia") != &moduleFileName[0])
            return true;
#endif
        const QString moduleFileName = Utils::Fs::fileName(QString::fromWCharArray(moduleFilePath));
        // TODO create finally helper https://www.modernescpp.com/index.php/c-core-guidelines-when-you-can-t-throw-an-exception silverqx
        // Or https://www.codeproject.com/Tips/476970/finally-clause-in-Cplusplus
        ::CloseHandle(processHandle);
        if (moduleFileName != "qMedia.exe")
            return true;

        qDebug() << "HWND for qMedia window found :" << hwnd;
        l_torrentExporter->setQMediaHwnd(hwnd);

        return false;
    }

    bool torrentContainsPreviewableFiles(const BitTorrent::TorrentHandle *const torrent)
    {
        if (!torrent->hasMetadata())
            return false;

        for (int i = 0; i < torrent->filesCount(); ++i) {
            if (Utils::Misc::isPreviewable(Utils::Fs::fileExtension(torrent->fileName(i))))
                return true;
        }

        return false;
    }

    const auto progressString = [](qreal progress) -> QString
    {
        progress *= 1000;
        return (static_cast<int>(progress) == 1000)
                ? QString::fromLatin1("1000")
                : Utils::String::fromDouble(progress, 0);
    };

    /*! Filter out non previewable torrents. */
    QVector<TorrentHandle *> filterPreviewableTorrents(const QVector<TorrentHandle *> &torrents)
    {
        QVector<TorrentHandle *> result;
        result.reserve(torrents.size());
        std::copy_if(torrents.constBegin(), torrents.constEnd(), std::back_inserter(result),
                     [](const TorrentHandle *const torrent)
        {
            return torrentContainsPreviewableFiles(torrent);
        });
        return result;
    }

    /*! Get the count of all previewable files in torrents hash. */
    int countPreviewableFiles(const QHash<quint64, TorrentHandle *> &torrents)
    {
        int previewableFilesCount = 0;
        foreach (const TorrentHandle *const torrent, torrents)
            for (int i = 0; i < torrent->filesCount() ; ++i)
                if (Utils::Misc::isPreviewable(Utils::Fs::fileExtension(torrent->fileName(i))))
                    ++previewableFilesCount;
        return previewableFilesCount;
    }
}

TorrentExporter *TorrentExporter::m_instance = nullptr;

TorrentExporter::TorrentExporter()
{
    l_torrentExporter = this;

    m_dbCommitTimer = new QTimer();
    m_dbCommitTimer->setInterval(COMMIT_INTERVAL);
    m_dbCommitTimer->setSingleShot(true);
    connect(m_dbCommitTimer, &QTimer::timeout, this, &TorrentExporter::commitTorrentsTimerTimeout);

    m_torrentsToCommit = new QHash<InfoHash, TorrentHandle *>();

    connectToDb();

    connect(Session::instance(), &Session::torrentAdded, this, &TorrentExporter::handleTorrentAdded);
    connect(Session::instance(), &Session::torrentDeleted, this, &TorrentExporter::handleTorrentDeleted);
    connect(Session::instance(), &Session::torrentsUpdated, this, &TorrentExporter::handleTorrentsUpdated);

    // Find qMedia's main window HWND
    ::EnumWindows(EnumWindowsProc, NULL);
    // Send hwnd of MainWindow to qMedia, aka. inform that qBittorrent is running
    if (m_qMediaHwnd != nullptr)
        ::PostMessage(m_qMediaHwnd, MSG_QBITTORRENT_UP,
            (WPARAM) dynamic_cast<Application *>(qApp)->mainWindow()->winId(), NULL);
}

TorrentExporter::~TorrentExporter()
{
    if (m_qMediaHwnd != nullptr)
        ::PostMessage(m_qMediaHwnd, MSG_QBITTORRENT_DOWN, NULL, NULL);

    delete m_torrentsToCommit;
    delete m_dbCommitTimer;
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

TorrentExporter* TorrentExporter::instance()
{
    return m_instance;
}

void TorrentExporter::handleTorrentAdded(TorrentHandle *const torrent)
{
    // TODO better handling, need to take into account also m_torrentsToCommit hash silverqx
//    if (!QSqlDatabase::database().isOpen()) {
//        qWarning() << "No active database connection, torrent additions / removes will not be handled";
//        return;
//    }
    if (!torrentContainsPreviewableFiles(torrent))
        return;

    m_torrentsToCommit->insert(torrent->hash(), torrent);
    // Start or Restart Timer
    m_dbCommitTimer->start();

    qDebug() << "|- Added :" << torrent->name();
}

void TorrentExporter::handleTorrentDeleted(InfoHash infoHash)
{
//    if (!QSqlDatabase::database().isOpen()) {
//        qWarning() << "No active database connection, torrent additions / removes will not be handled";
//        return;
//    }

    removeTorrentFromDb(infoHash);
    qDebug() << "|- Deleted :" << infoHash;
}

void TorrentExporter::commitTorrentsTimerTimeout()
{
    removeDuplicitTorrents();

    // Nothing to insert to DB
    if (m_torrentsToCommit->size() == 0)
        return;

    // Use transaction to guarantee data integrity
    m_db.transaction();
    try {
        // Multi insert for torrents
        insertTorrentsToDb();
        // Multi insert for previewable torrent files
        insertPreviewableFilesToDb();
        m_db.commit();
    }  catch (const ExporterError &) {
        m_db.rollback();
        return;
    }

    m_torrentsToCommit->clear();

    if (m_qMediaHwnd == nullptr)
        return;

    ::PostMessage(m_qMediaHwnd, MSG_QBT_TORRENTS_ADDED, NULL, NULL);
}

void TorrentExporter::handleTorrentsUpdated(const QVector<TorrentHandle *> &torrents)
{
    // TODO DB connection check also here silverqx
    // Filter out non previewable torrents
    auto previewableTorrents = filterPreviewableTorrents(torrents);
    // Nothing to update in DB
    if (previewableTorrents.size() == 0)
        return;

    // Use transaction to guarantee data integrity
    m_db.transaction();
    try {
        // Multi update of torrents and previewable files
        updateTorrentsInDb(previewableTorrents);
        updatePreviewableFilesInDb(previewableTorrents);
        m_db.commit();
    }  catch (const ExporterError &) {
        m_db.rollback();
        return;
    }

    // Don't send message updates about torrents changed, when the qMedia is not in foreground
    if (m_qMediaHwnd == nullptr || m_qMediaWindowActive == false)
        return;

    // Serialize torrent hashes for WM_COPYDATA ( std::string is perfect for this 😂 )
    std::string torrentHashesData;
    foreach (const auto torrent, previewableTorrents) {
        torrentHashesData += QString(torrent->hash()).toStdString();
    }
    // Create WM_COPYDATA struct
    COPYDATASTRUCT torrentInfoHashes;
    torrentInfoHashes.lpData = static_cast<LPVOID>(const_cast<char *>(torrentHashesData.data()));
    torrentInfoHashes.cbData = static_cast<DWORD>(torrentHashesData.size());
    torrentInfoHashes.dwData = NULL;
    ::SendMessage(m_qMediaHwnd, WM_COPYDATA, (WPARAM) MSG_QBT_TORRENTS_CHANGED, (LPARAM) (LPVOID) &torrentInfoHashes);
}

void TorrentExporter::connectToDb()
{
    QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL");
    db.setHostName("127.0.0.1");
#ifdef QT_DEBUG
    db.setDatabaseName("q_media_test");
#else
    db.setDatabaseName("q_media");
#endif
    db.setUserName("szachara");
    db.setPassword("99delfinu*");

    const bool ok = db.open();
    if (!ok) {
        qDebug() << "Connect to DB failed :" << db.lastError().text();
        return;
    }

    m_db = db;
}

void TorrentExporter::removeTorrentFromDb(InfoHash infoHash) const
{
    QSqlQuery query;
    query.prepare("DELETE FROM torrents WHERE hash = :hash");
    query.bindValue(":hash", QString(infoHash));

    const bool ok = query.exec();
    if (!ok) {
        qDebug() << "Delete for removeTorrentFromDb() failed :" << query.lastError().text();
        return;
    }

    if (m_qMediaHwnd == nullptr)
        return;

    ::PostMessage(m_qMediaHwnd, MSG_QBT_TORRENT_REMOVED, NULL, NULL);
}

void TorrentExporter::insertTorrentsToDb() const
{
    // Assemble query bindings for multi insert on the base of count of inserted torrents
    QString torrentsBindings = "";
    int i = 0;
    while (i < m_torrentsToCommit->size()) {
        torrentsBindings += "(?, ?, ?, ?, ?, ?, ?), ";
        ++i;
    }
    torrentsBindings.chop(2);
    const QString torrentsQueryString =
        QString("INSERT INTO torrents (name, progress, eta, size, remaining, added_on, hash) VALUES %1")
            .arg(torrentsBindings);

    QSqlQuery torrentsQuery;
    torrentsQuery.prepare(torrentsQueryString);

    // Prepare query bindings for torrents
    const TorrentHandle *torrent;
    QHash<InfoHash, TorrentHandle *>::const_iterator itTorrents = m_torrentsToCommit->constBegin();
    while (itTorrents != m_torrentsToCommit->constEnd()) {
        torrent = itTorrents.value();
#ifdef QT_DEBUG
        const auto torrentName = torrent->name();
#endif

        torrentsQuery.addBindValue(torrent->name());
        torrentsQuery.addBindValue(progressString(torrent->progress()));
        torrentsQuery.addBindValue(torrent->eta());
        torrentsQuery.addBindValue(torrent->totalSize());
        torrentsQuery.addBindValue(torrent->incompletedSize());
        torrentsQuery.addBindValue(torrent->addedTime());
        torrentsQuery.addBindValue(QString(torrent->hash()));

        ++itTorrents;
    }

    const bool okTorrents = torrentsQuery.exec();
    if (!okTorrents)
        qDebug() << "Insert for insertTorrentsToDb() failed :"
                 << torrentsQuery.lastError().text();
}

void TorrentExporter::removeDuplicitTorrents()
{
    // In DB Select torrents, which are going to be inserted
    QString queryBindings = "";
    const int count = m_torrentsToCommit->size();
    int i = 0;
    while (i < count) {
        queryBindings += "?, ";
        ++i;
    }
    queryBindings.chop(2);
    const QString queryString = QString("SELECT hash FROM torrents WHERE hash IN (%1)")
        .arg(queryBindings);

    QSqlQuery query;
    query.prepare(queryString);

    // Prepare query bindings
    QHash<InfoHash, TorrentHandle *>::const_iterator it = m_torrentsToCommit->constBegin();
    while (it != m_torrentsToCommit->constEnd()) {
        query.addBindValue(QString(it.key()));
        ++it;
    }

    const bool ok = query.exec();
    if (!ok)
        qDebug() << "Select for removeDuplicitTorrents() failed :" << query.lastError().text();

    // Any duplicit torrents in DB
    if (query.size() <= 0)
        return;

    // Remove duplicit torrents from hash
    while (query.next()) {
        InfoHash hash(query.value(0).toString());
        if (m_torrentsToCommit->contains(hash))
            m_torrentsToCommit->remove(hash);
    }
}

void TorrentExporter::insertPreviewableFilesToDb() const
{
    // TODO rewrite this with filterPreviewableTorrents() silverqx
    const QHash<quint64, TorrentHandle *> insertedTorrents = selectTorrentsByHashes(
        m_torrentsToCommit->keys()
    );

    if (insertedTorrents.size() == 0) {
        qDebug() << "Inserted torrents count is 0, this should never have happen :/";
        return;
    }

    // Assemble query binding for multi insert on the base of count of inserted torrents
    // Everything will be inserted in one insert query
    QString previewableFilesBindings = "";
    int i = 0;
    while (i < countPreviewableFiles(insertedTorrents)) {
        previewableFilesBindings += "(?, ?, ?, ?), ";
        ++i;
    }
    previewableFilesBindings.chop(2);
    const QString previewableFilesQueryString =
        QString("INSERT INTO torrents_previewable_files (torrent_id, filepath, size, progress) "
                "VALUES %1")
        .arg(previewableFilesBindings);

    QSqlQuery previewableFilesQuery;
    previewableFilesQuery.prepare(previewableFilesQueryString);

    // Prepare query bindings for torrents_previewable_files
    const TorrentHandle *torrent;
    QStringList filePaths;
    QVector<qreal> filesProgress;
    QHash<quint64, TorrentHandle *>::const_iterator itInsertedTorrents =
        insertedTorrents.constBegin();
    while (itInsertedTorrents != insertedTorrents.constEnd()) {
        torrent = itInsertedTorrents.value();
#ifdef QT_DEBUG
        const auto torrentName = torrent->name();
#endif

        filePaths = torrent->absoluteFilePaths();
        filesProgress = torrent->filesProgress();

        for (int i = 0; i < torrent->filesCount(); ++i) {
            if (!Utils::Misc::isPreviewable(Utils::Fs::fileExtension(torrent->fileName(i))))
                continue;

            previewableFilesQuery.addBindValue(itInsertedTorrents.key());
            previewableFilesQuery.addBindValue(filePaths[i]);
            previewableFilesQuery.addBindValue(torrent->fileSize(i));
            previewableFilesQuery.addBindValue(progressString(filesProgress[i]));
        }

        ++itInsertedTorrents;
    }

    const bool okFiles = previewableFilesQuery.exec();
    if (!okFiles)
        qDebug() << "Insert for a previewable files failed :"
                 << previewableFilesQuery.lastError().text();
}

QHash<quint64, TorrentHandle *> TorrentExporter::selectTorrentsByHashes(
    const QList<InfoHash> hashes
) const
{
    QString queryBindings = "";
    int i = 0;
    while (i < hashes.size()) {
        queryBindings += "?, ";
        ++i;
    }
    queryBindings.chop(2);
    const QString queryString = QString("SELECT id, hash FROM torrents WHERE hash IN (%1)")
        .arg(queryBindings);

    QSqlQuery query;
    query.prepare(queryString);

    // Prepare query bindings
    QList<InfoHash>::const_iterator itHashes = hashes.constBegin();
    while (itHashes != hashes.constEnd()) {
        query.addBindValue(QString(*itHashes));
        ++itHashes;
    }

    const bool ok = query.exec();
    if (!ok) {
        qDebug() << "Select for selectTorrentsByHashes() failed :"
                 << query.lastError().text();
        return {};
    }

    // Create new QHash of selected torrents
    QHash<quint64, TorrentHandle *> torrents;
    while (query.next()) {
        InfoHash hash(query.value(1).toString());
        if (m_torrentsToCommit->contains(hash))
            torrents.insert(
                query.value(0).toULongLong(),
                m_torrentsToCommit->value(hash)
            );
    }

    return torrents;
}

QHash<quint64, TorrentHandle *> TorrentExporter::selectTorrentsByHandles(
    const QVector<TorrentHandle *> &torrents
) const
{
    QString queryBindings = "";
    int i = 0;
    while (i < torrents.size()) {
        queryBindings += "?, ";
        ++i;
    }
    queryBindings.chop(2);
    const QString queryString = QString("SELECT id, hash FROM torrents WHERE hash IN (%1)")
        .arg(queryBindings);

    QSqlQuery query;
    query.prepare(queryString);

    // Prepare query bindings
    QVector<TorrentHandle *>::const_iterator itTorrents = torrents.constBegin();
    while (itTorrents != torrents.constEnd()) {
        query.addBindValue(QString((*itTorrents)->hash()));
        ++itTorrents;
    }

    const bool ok = query.exec();
    if (!ok) {
        qDebug() << "Select for selectTorrentsByHandles() failed :"
                 << query.lastError().text();
        return {};
    }

    // Create new QHash of selected torrents
    QHash<quint64, TorrentHandle *> torrentsHash;
    while (query.next()) {
        // Find torrent handle by info hash
        InfoHash hash(query.value(1).toString());
        const auto itTorrentHandle = std::find_if(torrents.constBegin(), torrents.constEnd(),
                                                  [&hash](const TorrentHandle *const torrent)
        {
            return (torrent->hash() == hash) ? true : false;
        });
        // Insert
        torrentsHash.insert(
            query.value(0).toULongLong(),
            *itTorrentHandle
        );
    }

    return torrentsHash;
}

void TorrentExporter::updateTorrentsInDb(const QVector<TorrentHandle *> &torrents) const
{
    // Assemble query bindings for multi update ( used insert with ON DUPLICATE KEY UPDATE )
    /* This technique can be used, because is guaranteed, that torrents in the torrents vector
       exist in DB. */
    QString torrentsBindings = "";
    int i = 0;
    while (i < torrents.size()) {
        torrentsBindings += "(?, ?, ?, ?, ?, ?, ?), ";
        ++i;
    }
    torrentsBindings.chop(2);
    const QString torrentsQueryString =
        QString("INSERT INTO torrents (name, progress, eta, size, remaining, added_on, hash) VALUES %1 "
                "ON DUPLICATE KEY UPDATE name=VALUES(name), progress=VALUES(progress), eta=VALUES(eta), "
                "size=VALUES(size), remaining=VALUES(remaining), added_on=VALUES(added_on), "
                "hash=VALUES(hash)")
            .arg(torrentsBindings);

    QSqlQuery torrentsQuery;
    torrentsQuery.prepare(torrentsQueryString);

    // Prepare query bindings for torrents
    const TorrentHandle *torrent;
    QVector<TorrentHandle *>::const_iterator itTorrents = torrents.constBegin();
    while (itTorrents != torrents.constEnd()) {
        torrent = *itTorrents;
#ifdef QT_DEBUG
        const auto torrentName = torrent->name();
#endif

        torrentsQuery.addBindValue(torrent->name());
        torrentsQuery.addBindValue(progressString(torrent->progress()));
        torrentsQuery.addBindValue(torrent->eta());
        torrentsQuery.addBindValue(torrent->totalSize());
        torrentsQuery.addBindValue(torrent->incompletedSize());
        torrentsQuery.addBindValue(torrent->addedTime());
        torrentsQuery.addBindValue(QString(torrent->hash()));

        ++itTorrents;
    }

    const bool okTorrents = torrentsQuery.exec();
    if (!okTorrents)
        qDebug() << "Update for updateTorrentsInDb() failed :"
                 << torrentsQuery.lastError().text();

    qDebug() << "Number of updated torrents :" << (torrentsQuery.numRowsAffected() / 2);

    if (!okTorrents)
        throw ExporterError("Update query for updateTorrentsInDb() failed.");
}

void TorrentExporter::updatePreviewableFilesInDb(const QVector<TorrentHandle *> &torrents) const
{
    // Create torrents hash keyed by torrent id, it's selected from db by torrent info hashes
    // Needed because torrent id is needed in update query
    const auto torrentsHash = selectTorrentsByHandles(torrents);
    if (torrentsHash.size() == 0) {
        qDebug() << "Selected torrents by hashes count is 0, this should never have happen :/";
        return;
    }

    QString previewableFilesBindings = "";
    int i = 0;
    while (i < countPreviewableFiles(torrentsHash)) {
        previewableFilesBindings += "(?, ?, ?, ?), ";
        ++i;
    }
    previewableFilesBindings.chop(2);
    const QString previewableFilesQueryString =
        QString("INSERT INTO torrents_previewable_files (torrent_id, filepath, size, progress) "
                "VALUES %1 AS new "
                "ON DUPLICATE KEY UPDATE torrent_id=new.torrent_id, filepath=new.filepath, "
                "size=new.size, progress=new.progress")
        .arg(previewableFilesBindings);

    QSqlQuery previewableFilesQuery;
    previewableFilesQuery.prepare(previewableFilesQueryString);

    // Prepare query bindings for torrents_previewable_files
    const TorrentHandle *torrent;
    QStringList filePaths;
    QVector<qreal> filesProgress;
    QHash<quint64, TorrentHandle *>::const_iterator itTorrents =
        torrentsHash.constBegin();
    while (itTorrents != torrentsHash.constEnd()) {
        torrent = itTorrents.value();
#ifdef QT_DEBUG
        const auto torrentName = torrent->name();
#endif

        filePaths = torrent->absoluteFilePaths();
        filesProgress = torrent->filesProgress();

        for (int i = 0; i < torrent->filesCount(); ++i) {
            if (!Utils::Misc::isPreviewable(Utils::Fs::fileExtension(torrent->fileName(i))))
                continue;

            previewableFilesQuery.addBindValue(itTorrents.key());
            previewableFilesQuery.addBindValue(filePaths[i]);
            previewableFilesQuery.addBindValue(torrent->fileSize(i));
            previewableFilesQuery.addBindValue(progressString(filesProgress[i]));
        }

        ++itTorrents;
    }

    const bool okFiles = previewableFilesQuery.exec();
    if (!okFiles) {
        qDebug() << "Update for a previewable files failed :"
                 << previewableFilesQuery.lastError().text();
        throw ExporterError("Update query for updatePreviewableFilesInDb() failed.");
    }
}

void TorrentExporter::setQMediaHwnd(const HWND hwnd)
{
    m_qMediaHwnd = hwnd;
}
