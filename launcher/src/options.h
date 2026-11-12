// options.h - the launcher's settings: what they are, how they are stored and
// which game flags they turn into.
//
// The game options mirror the in-game F1 menu (src/options_menu.cpp): same
// choices, same flag values, same defaults. The game keeps changes made in
// that menu in recomp_options.toml and drops one as soon as the launcher
// passes a different value for it (src/recomp_options.h), so a value changed
// here always wins over an older choice made in the menu.
#pragma once

#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

class QSettings;

namespace opts {

// One selectable value: the flags it sets, as name/value pairs.
struct Choice {
    QString id;     // stored in the settings file; never shown
    QString label;  // shown in the launcher
    QList<QPair<QString, QString>> flags;
};

struct Option {
    QString key;       // settings-file key
    QString section;   // heading on the Settings page
    QString label;
    QString help;      // plain-language explanation under the control
    QList<Choice> choices;
    QString defaultId;
    // Shown only while another option has a given value (empty = always).
    QString shownWhenKey;
    QString shownWhenId;
};

// Every option, in display order.
const QList<Option> &all();
const Option *find(const QString &key);

// The user's current choices plus the launcher's own settings.
class Settings {
public:
    // Loads the settings file; on the first start, carries over the old
    // launcher's settings when they exist.
    void load();
    void save() const;
    void resetToDefaults();

    QString choiceId(const QString &key) const;
    void setChoiceId(const QString &key, const QString &id);
    bool isShown(const Option &option) const;

    // Game flags for every option, in display order.
    QStringList gameArgs() const;

    QString gameRoot;     // folder holding the game data (empty = not set)
    QString saveRoot;     // custom save folder (empty = the game's default)
    QString appearance = QStringLiteral("system"); // system / light / dark

private:
    QHash<QString, QString> choices_;
    void migrateFromOldLauncher(QSettings &out);
};

// The settings file (also used by main.cpp for the appearance).
QSettings *settingsFile();

} // namespace opts
