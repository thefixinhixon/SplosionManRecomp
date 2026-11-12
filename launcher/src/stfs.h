// stfs.h - read Xbox 360 STFS packages (LIVE / PIRS / CON), the format XBLA
// games are distributed in, and extract their files.
#pragma once

#include <QFile>
#include <QList>
#include <QString>

#include <functional>

class StfsPackage {
public:
    struct Entry {
        QString path;        // relative, '/'-separated
        bool isDirectory = false;
        bool consecutive = false;
        quint32 startBlock = 0;
        quint32 blockCount = 0;
        quint32 size = 0;
    };

    bool open(const QString &fileName, QString *error);
    quint32 titleId() const { return titleId_; }
    // 0x000D0000 Xbox Live Arcade game, 0x00000002 downloadable content,
    // 0x000B0000 title update.
    quint32 contentType() const { return contentType_; }
    QString displayName() const { return displayName_; }
    QString fileName() const { return file_.fileName(); }
    const QList<Entry> &entries() const { return entries_; }
    quint64 totalBytes() const;

    // Extracts every file below destDir (created if needed). progress gets the
    // bytes written so far and returns false to cancel.
    bool extractAll(const QString &destDir, const std::function<bool(quint64)> &progress,
                    QString *error);

    // Reads one file into memory (for small files such as images).
    bool readFile(const Entry &entry, QByteArray *out, QString *error);

private:
    qint64 blockOffset(quint32 block) const;
    bool nextBlock(quint32 block, quint32 *next);
    bool readBlock(quint32 block, char *buffer);

    QFile file_;
    quint32 titleId_ = 0;
    quint32 contentType_ = 0;
    QString displayName_;
    qint64 dataStart_ = 0;
    int tableShift_ = 0;  // 0: one copy of each hash table, 1: two copies
    QList<Entry> entries_;
};
