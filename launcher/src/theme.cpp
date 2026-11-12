// theme.cpp - see theme.h.
//
// The launcher uses Qt's Fusion style with its own light and dark palettes, so
// it looks the same on every desktop. "System" follows the desktop: Qt's color
// scheme hint, and on Linux the XDG portal's color-scheme setting, which
// GNOME, KDE and others publish and which also reaches an AppImage (Qt alone
// often cannot tell there).
#include "theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleHints>

#if defined(Q_OS_LINUX)
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#endif

namespace theme {
namespace {

#if defined(Q_OS_LINUX)
const QString kPortalService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPortalPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kPortalInterface = QStringLiteral("org.freedesktop.portal.Settings");
const QString kAppearance = QStringLiteral("org.freedesktop.appearance");
const QString kColorScheme = QStringLiteral("color-scheme");

int unwrapScheme(QVariant value)
{
    // Read() wraps the value in two variants, ReadOne() and the signal in one.
    for (int i = 0; i < 3 && value.userType() == qMetaTypeId<QDBusVariant>(); ++i)
        value = value.value<QDBusVariant>().variant();
    bool ok = false;
    const uint scheme = value.toUInt(&ok);
    return ok && scheme <= 2 ? int(scheme) : -1;
}
#endif

struct Colors {
    QColor window, base, alternate, text, muted, button, border, accentText, tooltip;
};

Colors colorsFor(bool dark)
{
    if (dark)
        return {QColor(0x1d, 0x1a, 0x18), QColor(0x27, 0x23, 0x20), QColor(0x2e, 0x29, 0x26),
                QColor(0xee, 0xe8, 0xe3), QColor(0xa8, 0x9e, 0x96), QColor(0x33, 0x2d, 0x29),
                QColor(0x45, 0x3d, 0x37), QColor(0xff, 0x8f, 0x4a), QColor(0x2e, 0x29, 0x26)};
    return {QColor(0xf7, 0xf3, 0xef), QColor(0xff, 0xff, 0xff), QColor(0xf2, 0xec, 0xe6),
            QColor(0x24, 0x1f, 0x1b), QColor(0x6e, 0x64, 0x5c), QColor(0xec, 0xe5, 0xde),
            QColor(0xd4, 0xca, 0xc1), QColor(0xc2, 0x4a, 0x06), QColor(0xff, 0xfb, 0xf6)};
}

QPalette paletteFor(bool dark)
{
    const Colors c = colorsFor(dark);
    QPalette p;
    p.setColor(QPalette::Window, c.window);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.base);
    p.setColor(QPalette::AlternateBase, c.alternate);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::Button, c.button);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, accent());
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, c.tooltip);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::PlaceholderText, c.muted);
    p.setColor(QPalette::Link, c.accentText);
    p.setColor(QPalette::Light, c.button.lighter(dark ? 130 : 108));
    p.setColor(QPalette::Midlight, c.button.lighter(dark ? 115 : 104));
    p.setColor(QPalette::Mid, c.border);
    p.setColor(QPalette::Dark, c.border.darker(dark ? 140 : 120));
    p.setColor(QPalette::Shadow, QColor(0, 0, 0, dark ? 160 : 60));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, c.muted);
    p.setColor(QPalette::Disabled, QPalette::Button, c.alternate);
    return p;
}

QString styleSheetFor(bool dark)
{
    const Colors c = colorsFor(dark);
    const QColor a = accent();
    return QStringLiteral(R"(
QPushButton#play {
    background-color: %1; color: white; border: none; border-radius: 10px;
    font-size: 22px; font-weight: 800; padding: 14px 28px;
}
QPushButton#play:hover { background-color: %2; }
QPushButton#play:pressed { background-color: %3; }
QPushButton#play:disabled { background-color: %4; color: %5; }
QLabel[role="heading"] { font-size: 15px; font-weight: 700; color: %6; padding-top: 12px; }
QLabel[role="help"] { color: %5; }
QLabel[role="status"] { font-size: 17px; font-weight: 700; }
QLabel[role="footer"] { color: %5; font-size: 11px; }
QTabWidget::pane { border: none; }
QTabBar::tab {
    padding: 9px 20px; border: none; border-bottom: 3px solid transparent;
    color: %5; font-weight: 600; background: transparent;
}
QTabBar::tab:selected { color: %7; border-bottom: 3px solid %1; }
QTabBar::tab:hover:!selected { color: %7; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: none; }
QFrame#card { background-color: %8; border: 1px solid %9; border-radius: 10px; }
)")
        .arg(a.name(), a.lighter(112).name(), a.darker(115).name(), c.alternate.name(),
             c.muted.name(), c.accentText.name(), c.text.name(), c.base.name(), c.border.name());
}

} // namespace

QColor accent() { return QColor(0xf0, 0x64, 0x14); }

#if defined(Q_OS_LINUX)
// Reads the portal's color-scheme once and follows changes to it.
class PortalListener : public QObject {
    Q_OBJECT
public:
    explicit PortalListener(Manager *manager) : QObject(manager), manager_(manager)
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected())
            return;
        bus.connect(kPortalService, kPortalPath, kPortalInterface, QStringLiteral("SettingChanged"),
                    this, SLOT(onSettingChanged(QString, QString, QDBusVariant)));
        for (const QString method : {QStringLiteral("ReadOne"), QStringLiteral("Read")}) {
            QDBusMessage call = QDBusMessage::createMethodCall(kPortalService, kPortalPath,
                                                               kPortalInterface, method);
            call << kAppearance << kColorScheme;
            const QDBusMessage reply = bus.call(call, QDBus::Block, 500);
            if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
                manager_->setPortalScheme(unwrapScheme(reply.arguments().constFirst()));
                return;
            }
        }
    }

private slots:
    void onSettingChanged(const QString &group, const QString &key, const QDBusVariant &value)
    {
        if (group == kAppearance && key == kColorScheme)
            manager_->setPortalScheme(unwrapScheme(value.variant()));
    }

private:
    Manager *manager_;
};
#endif

Manager::Manager(QObject *parent) : QObject(parent)
{
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (mode_ == QLatin1String("system"))
            apply();
    });
#if defined(Q_OS_LINUX)
    new PortalListener(this);
#endif
}

void Manager::setMode(const QString &mode)
{
    mode_ = mode;
    apply();
}

void Manager::setPortalScheme(int scheme)
{
    if (scheme == portalScheme_)
        return;
    portalScheme_ = scheme;
    if (applied_ && mode_ == QLatin1String("system"))
        apply();
}

bool Manager::systemPrefersDark() const
{
    if (portalScheme_ == 1)
        return true;
    if (portalScheme_ == 2)
        return false;
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

void Manager::apply()
{
    const bool dark = mode_ == QLatin1String("dark")    ? true
                      : mode_ == QLatin1String("light") ? false
                                                        : systemPrefersDark();
    const bool first = !applied_;
    applied_ = true;
    if (!first && dark == dark_)
        return;
    dark_ = dark;
    QApplication::setPalette(paletteFor(dark));
    qApp->setStyleSheet(styleSheetFor(dark));
    emit changed(dark);
}

} // namespace theme

#if defined(Q_OS_LINUX)
#include "theme.moc"
#endif
