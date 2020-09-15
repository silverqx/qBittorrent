#ifndef TORRENTEXPORTERCOMMON_H
#define TORRENTEXPORTERCOMMON_H

#include <qt_windows.h>

enum UserMessages
{
    MSG_QBITTORRENT_UP            = WM_USER + 1,
    MSG_QBITTORRENT_DOWN          = WM_USER + 2,
    MSG_QBT_TORRENTS_CHANGED      = WM_USER + 3,
    MSG_QBT_TORRENT_REMOVED       = WM_USER + 4,
    MSG_QBT_TORRENTS_ADDED        = WM_USER + 5,
    MSG_QBT_TORRENT_MOVED         = WM_USER + 6,

    MSG_QMEDIA_UP                 = WM_USER + 100,
    MSG_QMEDIA_DOWN               = WM_USER + 101,
    MSG_QMD_DELETE_TORRENT        = WM_USER + 102,
    MSG_QMD_APPLICATION_ACTIVE    = WM_USER + 103,
    MSG_QMD_APPLICATION_DEACTIVE  = WM_USER + 104,
    MSG_QMD_PAUSE_TORRENT         = WM_USER + 105,
    MSG_QMD_RESUME_TORRENT        = WM_USER + 106,
    MSG_QMD_FORCE_RESUME_TORRENT  = WM_USER + 107,
    MSG_QMD_FORCE_RECHECK_TORRENT = WM_USER + 108,
};

#endif // TORRENTEXPORTERCOMMON_H
