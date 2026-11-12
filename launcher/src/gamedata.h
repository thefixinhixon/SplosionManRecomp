// gamedata.h - where the game's data is, and whether a folder holds it.
//
// A game folder is the folder chosen or created by an import. The data is in
// its gamedata/ subfolder (the layout an import creates) or directly in it
// (an extracted folder the user picked).
#pragma once

#include <QString>

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

} // namespace gamedata
