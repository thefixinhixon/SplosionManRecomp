// theme.h - light and dark appearance, following the system or the user.
#pragma once

#include <QColor>
#include <QObject>
#include <QString>

namespace theme {

QColor accent();

class Manager : public QObject {
    Q_OBJECT
public:
    explicit Manager(QObject *parent = nullptr);

    // "system", "light" or "dark".
    void setMode(const QString &mode);
    bool isDark() const { return dark_; }

    // The desktop's color-scheme setting from the XDG portal (Linux):
    // 0 no preference, 1 dark, 2 light.
    void setPortalScheme(int scheme);

signals:
    void changed(bool dark);

private:
    bool systemPrefersDark() const;
    void apply();

    QString mode_ = QStringLiteral("system");
    int portalScheme_ = -1; // -1: unknown
    bool dark_ = false;
    bool applied_ = false;
};

} // namespace theme
