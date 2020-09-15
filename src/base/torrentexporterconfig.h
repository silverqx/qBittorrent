#ifndef TORRENTEXPORTERCONFIG_H
#define TORRENTEXPORTERCONFIG_H

// Log debug messages about changed torrents, messages are logged every second.
#ifndef LOG_CHANGED_TORRENTS
#  define LOG_CHANGED_TORRENTS 0
#endif

// LOG_CHANGED_TORRENTS can be enable only in QT_DEBUG mode
#if !defined(QT_DEBUG) && (LOG_CHANGED_TORRENTS == 1)
#  error LOG_CHANGED_TORRENTS has to be 0, if QT_DEBUG is not defined.
#endif

#endif // TORRENTEXPORTERCONFIG_H
