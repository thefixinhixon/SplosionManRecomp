// art.cpp - see art.h.
#include "art.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QStandardPaths>

#include <cmath>

namespace art {
namespace {

// Banner shape: about the launcher header's (860x260 by default). Bump
// kArtVersion when the crop changes, so cached banners are rebuilt.
constexpr double kBannerAspect = 3.2;
constexpr int kArtVersion = 2;
constexpr double kPi = 3.14159265358979323846;

QString cacheDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("art"));
}

QString cached(const char *name) { return QDir(cacheDir()).filePath(QLatin1String(name)); }

// An explosion: a ring of uneven spikes around a hot core.
QPainterPath burst(QPointF center, double outer, double inner, int spikes, double turn)
{
    static const double jitter[] = {1.0, 0.82, 0.95, 0.74, 1.0, 0.88, 0.79, 0.97, 0.85, 0.92,
                                    0.76, 1.0, 0.9, 0.81, 0.96, 0.87};
    QPainterPath path;
    for (int i = 0; i < spikes * 2; ++i) {
        const double angle = turn + kPi * i / spikes;
        const double r = (i % 2 == 0) ? outer * jitter[(i / 2) % 16] : inner;
        const QPointF p(center.x() + r * std::cos(angle), center.y() + r * std::sin(angle));
        i == 0 ? path.moveTo(p) : path.lineTo(p);
    }
    path.closeSubpath();
    return path;
}

void paintBurst(QPainter &p, QPointF c, double size, double alpha = 1.0)
{
    p.save();
    p.setOpacity(alpha);
    p.setPen(Qt::NoPen);
    QRadialGradient outer(c, size);
    outer.setColorAt(0.0, QColor(0xff, 0xc8, 0x3d));
    outer.setColorAt(0.55, QColor(0xf0, 0x64, 0x14));
    outer.setColorAt(1.0, QColor(0xc4, 0x3a, 0x0a));
    p.setBrush(outer);
    p.drawPath(burst(c, size, size * 0.58, 13, -kPi / 2));
    QRadialGradient core(c, size * 0.45);
    core.setColorAt(0.0, QColor(0xff, 0xff, 0xf2));
    core.setColorAt(0.6, QColor(0xff, 0xe0, 0x7a));
    core.setColorAt(1.0, QColor(0xff, 0xb3, 0x2e, 0));
    p.setBrush(core);
    p.drawEllipse(c, size * 0.45, size * 0.45);
    p.restore();
}

} // namespace

bool refresh(const QString &dataDir)
{
    if (dataDir.isEmpty())
        return !gameBanner().isNull();
    const QString background = QDir(dataDir).filePath(QStringLiteral("Background.png"));
    const QString title = QDir(dataDir).filePath(QStringLiteral("Title Image.png"));
    if (!QFileInfo::exists(background))
        return !gameBanner().isNull();

    // Rebuild only when the source differs from the one the cache was made from.
    const QString stamp = QStringLiteral("v%1\n%2\n%3\n")
                              .arg(kArtVersion)
                              .arg(QFileInfo(background).canonicalFilePath())
                              .arg(QFileInfo(background).lastModified().toSecsSinceEpoch());
    QFile stampFile(cached("source.txt"));
    if (stampFile.open(QIODevice::ReadOnly) && QString::fromUtf8(stampFile.readAll()) == stamp &&
        QFileInfo::exists(cached("banner.png")))
        return true;
    stampFile.close();

    const QImage source(background);
    if (source.isNull())
        return false;
    QDir().mkpath(cacheDir());
    // The dashboard background is 1920x1320: the logo top right, 'Splosion
    // Man on the left, a scientist below. Keep the full width and the band
    // with the logo and the character's head.
    const int height = qMin(source.height(), int(source.width() / kBannerAspect));
    const int top = qMin(source.height() - height, int(source.height() * 0.07));
    source.copy(0, top, source.width(), height)
        .scaledToWidth(1600, Qt::SmoothTransformation)
        .save(cached("banner.png"));
    const QImage titleImage(title);
    if (!titleImage.isNull())
        titleImage.save(cached("icon.png"));
    else
        QFile::remove(cached("icon.png"));
    if (stampFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        stampFile.write(stamp.toUtf8());
    return true;
}

QPixmap gameBanner()
{
    static QString loadedFrom;
    static QPixmap pixmap;
    const QString file = cached("banner.png");
    const QString key = QStringLiteral("%1@%2").arg(file).arg(
        QFileInfo(file).lastModified().toMSecsSinceEpoch());
    if (key != loadedFrom) {
        pixmap = QFileInfo::exists(file) ? QPixmap(file) : QPixmap();
        loadedFrom = key;
    }
    return pixmap;
}

QIcon icon()
{
    const QString file = cached("icon.png");
    if (QFileInfo::exists(file)) {
        const QImage small(file);
        if (!small.isNull()) {
            QIcon result;
            result.addPixmap(QPixmap::fromImage(small));
            result.addPixmap(QPixmap::fromImage(small.scaled(256, 256, Qt::KeepAspectRatio,
                                                             Qt::SmoothTransformation)));
            return result;
        }
    }
    QIcon result;
    for (int size : {16, 32, 48, 64, 128, 256})
        result.addPixmap(QPixmap::fromImage(placeholderIcon(size)));
    return result;
}

QPixmap placeholderBanner(const QSize &size, bool dark)
{
    QPixmap pixmap(size);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient bg(0, 0, size.width(), size.height());
    bg.setColorAt(0.0, dark ? QColor(0x2a, 0x18, 0x10) : QColor(0xff, 0xf0, 0xe2));
    bg.setColorAt(1.0, dark ? QColor(0x12, 0x0d, 0x0b) : QColor(0xff, 0xd8, 0xbb));
    p.fillRect(pixmap.rect(), bg);

    // Debris: small dots scattered around the explosion, same every time.
    const QPointF center(size.width() * 0.76, size.height() * 0.5);
    const double scale = size.height() / 240.0;
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 26; ++i) {
        const double angle = i * 2.399963; // golden angle
        const double dist = (70 + (i * 37) % 90) * scale;
        const double r = (2.5 + (i * 7) % 6) * scale;
        p.setBrush(dark ? QColor(0x6b, 0x5a, 0x50, 170) : QColor(0x8a, 0x6a, 0x58, 120));
        p.drawEllipse(center + QPointF(std::cos(angle) * dist, std::sin(angle) * dist * 0.8), r, r);
    }
    paintBurst(p, center, 92 * scale);
    paintBurst(p, center + QPointF(-120 * scale, 58 * scale), 30 * scale, 0.85);
    paintBurst(p, center + QPointF(108 * scale, -70 * scale), 22 * scale, 0.8);

    // The text stays left of the explosion; shrink it to fit wider fonts.
    const QRectF textBox(size.width() * 0.06, 0, size.width() * 0.5, size.height() * 0.56);
    const QString name = QStringLiteral("SplosionManRecomp");
    QFont title = p.font();
    title.setWeight(QFont::Black);
    for (int px = int(38 * scale); px > 10; --px) {
        title.setPixelSize(px);
        if (QFontMetricsF(title).horizontalAdvance(name) <= textBox.width())
            break;
    }
    p.setFont(title);
    p.setPen(dark ? QColor(0xf5, 0xee, 0xe8) : QColor(0x2a, 0x1c, 0x14));
    p.drawText(textBox, Qt::AlignLeft | Qt::AlignBottom, name);
    QFont sub = p.font();
    sub.setPixelSize(int(15 * scale));
    sub.setWeight(QFont::Normal);
    p.setFont(sub);
    p.setPen(dark ? QColor(0xc9, 0xb8, 0xac) : QColor(0x6b, 0x4f, 0x3e));
    p.drawText(QRectF(textBox.left(), size.height() * 0.6, textBox.width(), size.height() * 0.3),
               Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
               QStringLiteral("Native PC port of 'Splosion Man"));
    return pixmap;
}

QImage placeholderIcon(int size)
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    const double s = size / 256.0;
    p.setPen(Qt::NoPen);
    QLinearGradient tile(0, 0, 0, size);
    tile.setColorAt(0.0, QColor(0x33, 0x22, 0x1a));
    tile.setColorAt(1.0, QColor(0x17, 0x10, 0x0d));
    p.setBrush(tile);
    p.drawRoundedRect(QRectF(8 * s, 8 * s, 240 * s, 240 * s), 52 * s, 52 * s);
    paintBurst(p, QPointF(128 * s, 128 * s), 104 * s);
    return image;
}

} // namespace art
