#include "exutils.h"

#include "base/bittorrent/torrenthandle.h"
#include "base/utils/fs.h"
#include "base/utils/misc.h"
#include "base/utils/string.h"

QString Export::ExUtils::progressString(qreal progress)
{
    progress *= 1000;
    return (static_cast<int>(progress) == 1000)
            ? QString::fromLatin1("1000")
            : Utils::String::fromDouble(progress, 0);
}

bool Export::ExUtils::torrentContainsPreviewableFiles(
        const BitTorrent::TorrentHandle *const torrent)
{
    if (!torrent->hasMetadata())
        return false;

    for (int i = 0; i < torrent->filesCount(); ++i)
        if (Utils::Misc::isPreviewable(Utils::Fs::fileExtension(torrent->fileName(i))))
            return true;

    return false;
}
