#ifndef EXPORTERUTILS_H
#define EXPORTERUTILS_H

namespace BitTorrent
{
    class TorrentHandle;
}

namespace Export::ExUtils
{
    /*! Obtain progress value as QString. */
    QString progressString(qreal progress);

    /*! Find out if torrent contains previewable files. */
    bool torrentContainsPreviewableFiles(const BitTorrent::TorrentHandle *const torrent);
}

#endif // EXPORTERUTILS_H
