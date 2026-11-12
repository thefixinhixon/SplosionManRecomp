// launch.h - the game's command line, and starting it.
#pragma once

#include <QString>
#include <QStringList>

namespace opts {
class Settings;
}

namespace launch {

struct Plan {
    bool ok = false;
    QString error;       // why the game cannot start (when !ok)
    QString program;     // the game executable
    QStringList args;
    QString workDir;     // the executable's folder
    QString logDir;      // game.log and last-command.txt
};

// The game's program files. They are one matched set from one build and must
// sit together in one folder.
QStringList programFiles();

Plan build(const opts::Settings &settings);

// The command as one line, for last-command.txt and --print-command.
QString commandLine(const Plan &plan);

// Writes last-command.txt and starts the game (the launcher does not wait).
bool start(const Plan &plan, QString *error);

} // namespace launch
