#include "torrentexporter.h"

#include <QDateTime>
#include <QDebug>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlRecord>

#include "base/utils/fs.h"
#include "base/utils/misc.h"
#include "base/utils/string.h"
#include "base/bittorrent/infohash.h"
#include "base/bittorrent/session.h"
#include "base/bittorrent/torrenthandle.h"

using namespace BitTorrent;

namespace {
    const int COMMIT_INTERVAL = 1000;

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
}

TorrentExporter *TorrentExporter::m_instance = nullptr;

TorrentExporter::TorrentExporter()
{
    m_dbCommitTimer = new QTimer();
    m_dbCommitTimer->setInterval(COMMIT_INTERVAL);
    m_dbCommitTimer->setSingleShot(true);
    connect(m_dbCommitTimer, &QTimer::timeout, this, &TorrentExporter::commitTorrentsTimerTimeout);

    m_torrentsToCommit = new QHash<InfoHash, TorrentHandle *>();

    connectToDb();

    connect(Session::instance(), &Session::torrentAdded, this, &TorrentExporter::handleTorrentAdded);
    connect(Session::instance(), &Session::torrentDeleted, this, &TorrentExporter::handleTorrentDeleted);
}

TorrentExporter::~TorrentExporter()
{
    delete m_torrentsToCommit;
    delete m_dbCommitTimer;
}

void TorrentExporter::initInstance()
{
    if (!m_instance) {
        m_instance = new TorrentExporter();
    }
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
    if (!torrentContainsPreviewableFiles(torrent))
        return;

    m_torrentsToCommit->insert(torrent->hash(), torrent);
    // Start or Restart Timer
    m_dbCommitTimer->start();

    qDebug() << "|- Added : " << torrent->name();
}

void TorrentExporter::handleTorrentDeleted(InfoHash infoHash)
{
    removeTorrentFromDb(infoHash);
    qDebug() << "|- Deleted : " << infoHash;
}

void TorrentExporter::commitTorrentsTimerTimeout()
{
    removeDuplicitTorrents();

    // Multi insert for torrents
    insertTorrentsToDb();
    // Multi insert for previewable torrent files
    insertPreviewableFilesToDb();

    m_torrentsToCommit->clear();
}

void TorrentExporter::connectToDb() const
{
    QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL");
    db.setHostName("127.0.0.1");
    db.setDatabaseName("q_media");
    db.setUserName("szachara");
    db.setPassword("99delfinu*");

    const bool ok = db.open();
    if (!ok)
        qDebug() << "Connect to DB failed : " << db.lastError().text();
}

void TorrentExporter::removeTorrentFromDb(InfoHash infoHash) const
{
    QSqlQuery query;
    query.prepare("DELETE FROM torrents WHERE hash = :hash");
    query.bindValue(":hash", QString(infoHash));

    const bool ok = query.exec();
    if (!ok)
        qDebug() << "Delete failed : " << query.lastError().text();
}

void TorrentExporter::insertTorrentsToDb() const
{
    const int count = m_torrentsToCommit->size();
    // Nothing to insert to DB
    if (count == 0)
        return;

    // Assemble query bindings for multi insert on the base of count of inserted torrents
    QString torrentsBindings = "";
    int i = 0;
    while (i < count) {
        torrentsBindings += "(?, ?, ?, ?, ?), ";
        ++i;
    }
    torrentsBindings.chop(2);
    const QString torrentsQueryString =
        QString("INSERT INTO torrents (name, size, progress, added_on, hash) VALUES %1")
            .arg(torrentsBindings);

    QSqlQuery torrentsQuery;
    torrentsQuery.prepare(torrentsQueryString);

    // Prepare query bindings for torrents
    const TorrentHandle *torrent;
    QHash<InfoHash, TorrentHandle *>::const_iterator itTorrents = m_torrentsToCommit->constBegin();
    while (itTorrents != m_torrentsToCommit->constEnd()) {
        torrent = itTorrents.value();
        auto xx = torrent->name();
        torrentsQuery.addBindValue(torrent->name());
        torrentsQuery.addBindValue(torrent->totalSize());
        torrentsQuery.addBindValue(progressString(torrent->progress()));
        torrentsQuery.addBindValue(torrent->addedTime());
        torrentsQuery.addBindValue(QString(torrent->hash()));

        ++itTorrents;
    }

    const bool okTorrents = torrentsQuery.exec();
    if (!okTorrents)
        qDebug() << "Insert for a torrent failed : "
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
        qDebug() << "Select failed : " << query.lastError().text();

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
    QHash<quint64, TorrentHandle *> insertedTorrents = selectTorrentsByHashes(
        m_torrentsToCommit->keys()
    );

    if (insertedTorrents.size() == 0) {
        qDebug() << "Inserted torrents count is 0, this should never have happend :/";
        return;
    }

    // Assemble query binding for multi insert on the base of count of inserted torrents
    // Get the count of all previewable files, everything will be inserted in one insert query
    int previewableFilesCount = 0;
    foreach (const TorrentHandle *const torrent, insertedTorrents)
        for (int i = 0; i < torrent->filesCount() ; ++i)
            if (Utils::Misc::isPreviewable(Utils::Fs::fileExtension(torrent->fileName(i))))
                ++previewableFilesCount;

    QString previewableFilesBindings = "";
    int i = 0;
    while (i < previewableFilesCount) {
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
    QHash<quint64, TorrentHandle *>::const_iterator itPreviewableFiles =
        insertedTorrents.constBegin();
    while (itPreviewableFiles != insertedTorrents.constEnd()) {
        torrent = itPreviewableFiles.value();
        auto xx = torrent->name();

        filePaths = torrent->absoluteFilePaths();
        filesProgress = torrent->filesProgress();

        for (int i = 0; i < torrent->filesCount(); ++i) {
            if (!Utils::Misc::isPreviewable(Utils::Fs::fileExtension(torrent->fileName(i))))
                continue;

            previewableFilesQuery.addBindValue(itPreviewableFiles.key());
            previewableFilesQuery.addBindValue(filePaths[i]);
            previewableFilesQuery.addBindValue(torrent->fileSize(i));
            previewableFilesQuery.addBindValue(progressString(filesProgress[i]));
        }

        ++itPreviewableFiles;
    }

    const bool okFiles = previewableFilesQuery.exec();
    if (!okFiles)
        qDebug() << "Insert for a previewable files failed : "
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
    QList<InfoHash>::const_iterator it = hashes.constBegin();
    while (it != hashes.constEnd()) {
        query.addBindValue(QString(*it));
        ++it;
    }

    const bool ok = query.exec();
    if (!ok) {
        qDebug() << "Select of torrents by hashes failed : " << query.lastError().text();
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
