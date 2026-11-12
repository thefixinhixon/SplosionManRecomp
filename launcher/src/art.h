// art.h - the launcher's banner and icon.
//
// The launcher ships no game art. Once the game data is known it makes its
// banner from the game's own Background.png (the Xbox dashboard art in every
// XBLA package) and its icon from Title Image.png, and keeps them in its data
// folder. Until then it shows a placeholder drawn here, from shapes only.
#pragma once

#include <QIcon>
#include <QPixmap>
#include <QString>

namespace art {

// Builds (or rebuilds, if the source changed) the cached art from a game
// data folder. Returns true when game art is available afterwards.
bool refresh(const QString &dataDir);

// The cached game banner (null when there is none yet) and icon (placeholder
// when there is none yet).
QPixmap gameBanner();
QIcon icon();

// The placeholder banner at the given size, for a light or dark window.
QPixmap placeholderBanner(const QSize &size, bool dark);

// The placeholder app icon (also used for the AppImage and the Windows exe).
QImage placeholderIcon(int size);

} // namespace art
