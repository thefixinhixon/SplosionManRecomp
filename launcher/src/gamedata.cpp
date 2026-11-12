// gamedata.cpp - see gamedata.h.
#include "gamedata.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

namespace gamedata {
namespace {

quint32 be32(const uchar *p)
{
    return quint32(p[0]) << 24 | quint32(p[1]) << 16 | quint32(p[2]) << 8 | p[3];
}

bool holdsGame(const QString &dir)
{
    const QString xex = QDir(dir).filePath(QStringLiteral("default.xex"));
    if (!QFile::exists(xex))
        return false;
    const quint32 id = xexTitleId(xex);
    return id == 0 || id == kTitleId; // unreadable headers get the benefit of the doubt
}

} // namespace

quint32 xexTitleId(const QString &xexPath)
{
    QFile f(xexPath);
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    const QByteArray head = f.read(0x10000);
    const auto *p = reinterpret_cast<const uchar *>(head.constData());
    if (head.size() < 0x18 || !head.startsWith("XEX2"))
        return 0;
    // Optional headers: (key, value) pairs from 0x18. Key 0x00040006 is the
    // execution info; its value is the offset of a struct with the title ID
    // at +0x0C.
    const quint32 count = be32(p + 0x14);
    for (quint32 i = 0; i < count && 0x18 + (i + 1) * 8 <= quint32(head.size()); ++i) {
        const uchar *entry = p + 0x18 + i * 8;
        if (be32(entry) != 0x00040006)
            continue;
        const quint32 offset = be32(entry + 4);
        if (offset + 0x10 > quint32(head.size()))
            return 0;
        return be32(p + offset + 0x0C);
    }
    return 0;
}

QString dataDir(const QString &gameFolder)
{
    if (gameFolder.isEmpty())
        return {};
    const QString sub = QDir(gameFolder).filePath(QStringLiteral("gamedata"));
    if (holdsGame(sub))
        return QDir::cleanPath(sub);
    if (holdsGame(gameFolder))
        return QDir::cleanPath(gameFolder);
    return {};
}

QString locate(const QString &stored)
{
    if (!dataDir(stored).isEmpty())
        return QDir::cleanPath(stored);
    // Next to the launcher: its own folder (Windows) or the folder holding
    // the AppImage file (the launcher itself runs from a temporary mount).
    QStringList starts{QCoreApplication::applicationDirPath()};
    if (const QString appImage = qEnvironmentVariable("APPIMAGE"); !appImage.isEmpty())
        starts.prepend(QFileInfo(appImage).absolutePath());
    for (const QString &start : starts) {
        QDir dir(start);
        for (int i = 0; i < 4; ++i) {
            if (!dataDir(dir.absolutePath()).isEmpty())
                return dir.absolutePath();
            if (!dir.cdUp())
                break;
        }
    }
    return {};
}

QString logDir(const QString &gameFolder)
{
    return QDir(gameFolder).filePath(QStringLiteral("logs"));
}

} // namespace gamedata
