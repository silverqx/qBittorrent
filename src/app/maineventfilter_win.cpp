#include "maineventfilter_win.h"

#include <QDebug>

#include <qt_windows.h>

#include "base/bittorrent/infohash.h"
#include "base/bittorrent/session.h"
#include "base/export/torrentexporter.h"
#include "commonglobal.h"

namespace
{
    BitTorrent::InfoHash getInfoHashFromWmCopyData(const COPYDATASTRUCT &copyData)
    {
        const int dataSize = static_cast<int>(copyData.cbData);
        const QByteArray infoHashArr =
                QByteArray(static_cast<const char *>(copyData.lpData), dataSize);

        return BitTorrent::InfoHash(QString::fromStdString(infoHashArr.toStdString()));
    };
}

MainEventFilter::MainEventFilter()
    : QAbstractNativeEventFilter()
{}

bool MainEventFilter::nativeEventFilter(const QByteArray &eventType, void *message, long *result)
{
    Q_UNUSED(result);

    if (eventType != QByteArrayLiteral("windows_generic_MSG"))
        return false;

    const MSG *msg = static_cast<const MSG *>(message);

    switch (msg->message) {
    case ::MSG_QMEDIA_UP:
        qDebug() << "IPC qMedia : qMedia started";
        Export::TorrentExporter::instance()->setQMediaHwnd(
                    reinterpret_cast<HWND>(msg->wParam));
        return true;
    case ::MSG_QMEDIA_DOWN:
        qDebug() << "IPC qMedia : qMedia closed";
        Export::TorrentExporter::instance()->setQMediaHwnd(nullptr);
        return true;
    case ::MSG_QMD_APPLICATION_ACTIVE:
        qDebug() << "IPC qMedia : qMedia window activated";
        Export::TorrentExporter::instance()->setQMediaWindowActive(true);
        return true;
    case ::MSG_QMD_APPLICATION_DEACTIVE:
        qDebug() << "IPC qMedia : qMedia window deactivated";
        Export::TorrentExporter::instance()->setQMediaWindowActive(false);
        return true;
    }

    // WM_COPYDATA section
    if (msg->message != WM_COPYDATA)
        return false;

    const COPYDATASTRUCT copyData = *reinterpret_cast<PCOPYDATASTRUCT>(msg->lParam);
    switch (static_cast<int>(msg->wParam)) {
    case ::MSG_QMD_DELETE_TORRENT: {
        const auto infoHash = getInfoHashFromWmCopyData(copyData);
        qDebug() << "IPC qMedia : Delete torrent copyData size :"
                 << static_cast<int>(copyData.cbData);
        qDebug() << "IPC qMedia : Delete torrent hash :" << infoHash;
        BitTorrent::Session::instance()->deleteTorrent(infoHash, TorrentAndFiles);
        return true;
    }
    case ::MSG_QMD_PAUSE_TORRENT: {
        const auto infoHash = getInfoHashFromWmCopyData(copyData);
        qDebug() << "IPC qMedia : Pause torrent copyData size :"
                 << static_cast<int>(copyData.cbData);
        qDebug() << "IPC qMedia : Pause torrent hash :" << infoHash;
        BitTorrent::Session::instance()->findTorrent(infoHash)->pause();
        return true;
    }
    case ::MSG_QMD_RESUME_TORRENT: {
        const auto infoHash = getInfoHashFromWmCopyData(copyData);
        qDebug() << "IPC qMedia : Resume torrent copyData size :"
                 << static_cast<int>(copyData.cbData);
        qDebug() << "IPC qMedia : Resume torrent hash :" << infoHash;
        BitTorrent::Session::instance()->findTorrent(infoHash)->resume();
        return true;
    }
    case ::MSG_QMD_FORCE_RESUME_TORRENT: {
        const auto infoHash = getInfoHashFromWmCopyData(copyData);
        qDebug() << "IPC qMedia : Force resume torrent copyData size :"
                 << static_cast<int>(copyData.cbData);
        qDebug() << "IPC qMedia : Force resume torrent hash :" << infoHash;
        BitTorrent::Session::instance()->findTorrent(infoHash)->resume(true);
        return true;
    }
    case ::MSG_QMD_FORCE_RECHECK_TORRENT: {
        const auto infoHash = getInfoHashFromWmCopyData(copyData);
        qDebug() << "IPC qMedia : Force recheck torrent copyData size :"
                 << static_cast<int>(copyData.cbData);
        qDebug() << "IPC qMedia : Force recheck torrent hash :" << infoHash;
        BitTorrent::Session::instance()->findTorrent(infoHash)->forceRecheck();
        return true;
    }
    }

    return false;
}
