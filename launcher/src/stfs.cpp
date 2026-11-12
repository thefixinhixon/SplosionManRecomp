// stfs.cpp - see stfs.h.
//
// STFS in brief: a 0x1000-aligned header, then 0x1000-byte blocks. Data blocks
// are numbered without the hash tables that are interleaved with them: one
// level-0 table in front of every 170 data blocks, one level-1 table for every
// 170 * 170, one level-2 table above that. Read-only packages (LIVE, PIRS)
// store one copy of each table, others two. A level-0 entry also names the
// next block of the file, for files whose blocks are not consecutive. Integers
// are big-endian except the block numbers and counts in the volume descriptor
// and file table, which are little-endian.
#include "stfs.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>

#include <vector>

namespace {

constexpr qint64 kBlockSize = 0x1000;
constexpr quint32 kPerTable = 0xAA;      // hash entries per table (170)
constexpr quint32 kPerLevel1 = 0x70E4;   // data blocks under one level-1 table (170 * 170)
constexpr int kEntrySize = 0x40;

quint32 be32(const uchar *p) { return quint32(p[0]) << 24 | quint32(p[1]) << 16 | quint32(p[2]) << 8 | p[3]; }
quint32 be24(const uchar *p) { return quint32(p[0]) << 16 | quint32(p[1]) << 8 | p[2]; }
quint32 le24(const uchar *p) { return quint32(p[2]) << 16 | quint32(p[1]) << 8 | p[0]; }
quint16 le16(const uchar *p) { return quint16(p[1] << 8 | p[0]); }
quint16 be16(const uchar *p) { return quint16(p[0] << 8 | p[1]); }

// A file or folder name from the package, checked before it becomes part of a
// path on disk: no separators, no "." or "..", nothing Windows rejects.
bool safeName(const QString &name)
{
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        return false;
    for (const QChar ch : name)
        if (ch < QChar(0x20) || QStringLiteral("/\\:*?\"<>|").contains(ch))
            return false;
    return !name.endsWith(QLatin1Char(' ')) && !name.endsWith(QLatin1Char('.'));
}

} // namespace

bool StfsPackage::open(const QString &fileName, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    entries_.clear();
    file_.close();
    file_.setFileName(fileName);
    if (!file_.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("Cannot open the file: %1").arg(file_.errorString()));

    const QByteArray header = file_.read(0x971A);
    if (header.size() < 0x971A)
        return fail(QStringLiteral("The file is too small to be an Xbox 360 package."));
    const auto *h = reinterpret_cast<const uchar *>(header.constData());
    const QByteArray magic = header.left(4);
    if (magic != "LIVE" && magic != "PIRS" && magic != "CON ")
        return fail(QStringLiteral("This is not an Xbox 360 package (no LIVE, PIRS or CON header)."));

    const quint32 headerSize = be32(h + 0x340);
    titleId_ = be32(h + 0x360);
    contentType_ = be32(h + 0x344);
    {
        QString name;
        for (int i = 0; i < 0x80; i += 2) {
            const quint16 ch = be16(h + 0x411 + i);
            if (!ch)
                break;
            name += QChar(ch);
        }
        displayName_ = name.trimmed();
    }

    // Volume descriptor (STFS) at 0x379.
    const uchar *vd = h + 0x379;
    if (vd[0] != 0x24)
        return fail(QStringLiteral("The package does not use the STFS file system."));
    tableShift_ = (vd[2] & 1) ? 0 : 1;
    const quint32 tableBlockCount = le16(vd + 3);
    quint32 tableBlock = le24(vd + 5);
    const quint32 allocatedBlocks = be32(vd + 0x1C);
    dataStart_ = (qint64(headerSize) + kBlockSize - 1) & ~(kBlockSize - 1);

    // File table: a chain of blocks, 64 entries each. path indicator = index
    // of the parent folder's entry, 0xFFFF for the root.
    struct Raw {
        QString name;
        quint8 flags = 0;
        quint32 blocks = 0, start = 0, size = 0;
        quint16 parent = 0xFFFF;
        bool used = false;
    };
    std::vector<Raw> raw;
    std::vector<char> block(kBlockSize);
    for (quint32 i = 0; i < tableBlockCount; ++i) {
        if ((allocatedBlocks && tableBlock >= allocatedBlocks) || !readBlock(tableBlock, block.data()))
            return fail(QStringLiteral("The package's file table is damaged."));
        for (int e = 0; e < kBlockSize / kEntrySize; ++e) {
            const auto *p = reinterpret_cast<const uchar *>(block.data()) + e * kEntrySize;
            Raw r;
            const int nameLength = p[0x28] & 0x3F;
            r.used = nameLength > 0;
            r.name = QString::fromLatin1(reinterpret_cast<const char *>(p), qMin(nameLength, 0x28));
            r.flags = p[0x28];
            r.blocks = le24(p + 0x29);
            r.start = le24(p + 0x2F);
            r.parent = be16(p + 0x32);
            r.size = be32(p + 0x34);
            raw.push_back(r);
        }
        if (i + 1 < tableBlockCount && !nextBlock(tableBlock, &tableBlock))
            return fail(QStringLiteral("The package's file table is damaged."));
    }

    // Build relative paths; parents always come before their children.
    QHash<quint32, QString> folderPaths;
    for (quint32 i = 0; i < raw.size(); ++i) {
        const Raw &r = raw[i];
        if (!r.used)
            continue;
        if (!safeName(r.name))
            return fail(QStringLiteral("The package contains an unsafe file name: %1").arg(r.name));
        QString path = r.name;
        if (r.parent != 0xFFFF) {
            if (!folderPaths.contains(r.parent))
                return fail(QStringLiteral("The package's file table is damaged."));
            path = folderPaths.value(r.parent) + QLatin1Char('/') + r.name;
        }
        Entry entry;
        entry.path = path;
        entry.isDirectory = r.flags & 0x80;
        entry.consecutive = r.flags & 0x40;
        entry.startBlock = r.start;
        entry.blockCount = r.blocks;
        entry.size = r.size;
        if (entry.isDirectory)
            folderPaths.insert(i, path);
        else if (qint64(entry.blockCount) * kBlockSize < entry.size)
            return fail(QStringLiteral("The package's file table is damaged."));
        entries_ << entry;
    }
    if (entries_.isEmpty())
        return fail(QStringLiteral("The package contains no files."));
    return true;
}

quint64 StfsPackage::totalBytes() const
{
    quint64 total = 0;
    for (const Entry &e : entries_)
        if (!e.isDirectory)
            total += e.size;
    return total;
}

// File offset of a data block: skip the hash tables stored in front of it.
qint64 StfsPackage::blockOffset(quint32 block) const
{
    qint64 n = block;
    qint64 physical = n + (((n + kPerTable) / kPerTable) << tableShift_);
    if (n >= kPerTable) {
        physical += ((n + kPerLevel1) / kPerLevel1) << tableShift_;
        if (n >= kPerLevel1)
            physical += qint64(1) << tableShift_;
    }
    return dataStart_ + physical * kBlockSize;
}

// The block after `block` in its file, from the level-0 hash table entry.
bool StfsPackage::nextBlock(quint32 block, quint32 *next)
{
    const qint64 step = kPerTable + (qint64(1) << tableShift_);
    qint64 table = 0;
    if (block >= kPerTable) {
        table = (block / kPerTable) * step;
        table += ((block / kPerLevel1) + 1) << tableShift_;
        if (block / kPerLevel1)
            table += qint64(1) << tableShift_;
    }
    uchar entry[0x18];
    if (!file_.seek(dataStart_ + table * kBlockSize + (block % kPerTable) * 0x18) ||
        file_.read(reinterpret_cast<char *>(entry), sizeof entry) != sizeof entry)
        return false;
    *next = be24(entry + 0x15);
    return true;
}

bool StfsPackage::readBlock(quint32 block, char *buffer)
{
    if (!file_.seek(blockOffset(block)))
        return false;
    const qint64 got = file_.read(buffer, kBlockSize);
    if (got < 0)
        return false;
    std::fill(buffer + got, buffer + kBlockSize, 0); // the last block may be cut short
    return got > 0;
}

bool StfsPackage::readFile(const Entry &entry, QByteArray *out, QString *error)
{
    out->clear();
    out->reserve(entry.size);
    std::vector<char> buffer(kBlockSize);
    quint32 block = entry.startBlock;
    quint32 left = entry.size;
    for (quint32 i = 0; left > 0; ++i) {
        if (i >= entry.blockCount || !readBlock(block, buffer.data())) {
            if (error)
                *error = QStringLiteral("Could not read %1 from the package.").arg(entry.path);
            return false;
        }
        const quint32 n = qMin<quint32>(left, kBlockSize);
        out->append(buffer.data(), n);
        left -= n;
        if (left > 0) {
            if (entry.consecutive)
                ++block;
            else if (!nextBlock(block, &block)) {
                if (error)
                    *error = QStringLiteral("Could not read %1 from the package.").arg(entry.path);
                return false;
            }
        }
    }
    return true;
}

bool StfsPackage::extractAll(const QString &destDir,
                             const std::function<bool(quint64)> &progress, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    const QDir dest(destDir);
    if (!dest.mkpath(QStringLiteral(".")))
        return fail(QStringLiteral("Cannot create the folder %1.").arg(destDir));

    quint64 written = 0, reported = 0;
    std::vector<char> buffer(kBlockSize);
    for (const Entry &entry : std::as_const(entries_)) {
        const QString target = dest.filePath(entry.path);
        if (entry.isDirectory) {
            if (!dest.mkpath(entry.path))
                return fail(QStringLiteral("Cannot create the folder %1.").arg(target));
            continue;
        }
        if (!QFileInfo(target).dir().exists() && !QDir().mkpath(QFileInfo(target).path()))
            return fail(QStringLiteral("Cannot create the folder %1.").arg(QFileInfo(target).path()));

        QSaveFile out(target);
        if (!out.open(QIODevice::WriteOnly))
            return fail(QStringLiteral("Cannot write %1: %2").arg(target, out.errorString()));
        quint32 block = entry.startBlock;
        quint32 left = entry.size;
        for (quint32 i = 0; left > 0; ++i) {
            if (i >= entry.blockCount || !readBlock(block, buffer.data()))
                return fail(QStringLiteral("Could not read %1 from the package.").arg(entry.path));
            const quint32 n = qMin<quint32>(left, kBlockSize);
            if (out.write(buffer.data(), n) != n)
                return fail(QStringLiteral("Cannot write %1: %2").arg(target, out.errorString()));
            left -= n;
            written += n;
            if (left > 0) {
                if (entry.consecutive)
                    ++block;
                else if (!nextBlock(block, &block))
                    return fail(QStringLiteral("Could not read %1 from the package.").arg(entry.path));
            }
            if (progress && written - reported >= (1 << 20)) {
                reported = written;
                if (!progress(written))
                    return fail(QStringLiteral("Cancelled."));
            }
        }
        if (!out.commit())
            return fail(QStringLiteral("Cannot write %1: %2").arg(target, out.errorString()));
    }
    if (progress)
        progress(written);
    return true;
}
