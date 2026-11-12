// gamedata.h - where the game's data is, and whether a folder holds it.
//
// A game folder is the folder chosen or created by an import. The data is in
// its gamedata/ subfolder (the layout an import creates) or directly in it
// (an extracted folder the user picked).
#pragma once

#include <QString>
#include <QStringList>

namespace gamedata {

constexpr quint32 kTitleId = 0x5841098F; // 'Splosion Man

// The folder with default.xex inside gameFolder, or empty when there is none
// or it belongs to another game.
QString dataDir(const QString &gameFolder);

// The title ID in a XEX file's header (0 when it cannot be read).
quint32 xexTitleId(const QString &xexPath);

// A usable game folder: the stored one when it is still valid, else the
// launcher's own folder or one of its parents (a game folder next to the
// launcher). Empty when none is found.
QString locate(const QString &stored);

// Where the game's log and last-command.txt go: <game folder>/logs.
QString logDir(const QString &gameFolder);

// The game's save folder: the custom one, or the game's own default
// (Documents\splosionman on Windows, $XDG_DATA_HOME/splosionman on Linux).
QString saveRoot(const QString &custom);

// Where the game looks for downloadable content (marketplace content is
// shared by all profiles): <save folder>/0000000000000000/5841098F/00000002.
// Each package is a folder named after the package file.
QString dlcDir(const QString &saveRoot);

// The DLC package folders installed there.
QStringList installedDlc(const QString &saveRoot);

} // namespace gamedata
