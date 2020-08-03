#include "maineventfilter_win.h"

#include <QDebug>

#include <qt_windows.h>

#include "base/bittorrent/infohash.h"
#include "base/bittorrent/session.h"
#include "base/bittorrent/torrentexporter.h"
#include "base/torrentexportercommon.h"

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
    case MSG_QMEDIA_UP:
        qDebug() << "IPC qMedia : qMedia started";
        BitTorrent::TorrentExporter::instance()->setQMediaHwnd(reinterpret_cast<HWND>(msg->wParam));
        return true;
    case MSG_QMEDIA_DOWN:
        qDebug() << "IPC qMedia : qMedia closed";
        BitTorrent::TorrentExporter::instance()->setQMediaHwnd(nullptr);
        return true;
    }

    // WM_COPYDATA section
    if (msg->message != WM_COPYDATA)
        return false;

    const COPYDATASTRUCT copyData = *reinterpret_cast<PCOPYDATASTRUCT>(msg->lParam);
    switch (static_cast<int>(msg->wParam)) {
    case MSG_QMD_DELETE_TORRENT:
        const int dataSize = static_cast<int>(copyData.cbData);
        const QByteArray infoHashArr = QByteArray(static_cast<const char *>(copyData.lpData), dataSize);

        qDebug() << "IPC qMedia : Delete torrent copyData size :" << dataSize;
        qDebug() << "IPC qMedia : Delete torrent hash :" << infoHashArr;

        const auto infoHash = BitTorrent::InfoHash(QString::fromStdString(infoHashArr.toStdString()));
        BitTorrent::Session::instance()->deleteTorrent(infoHash, TorrentAndFiles);
        return true;
    }

    return false;
}
