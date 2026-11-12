// main.cpp - SplosionManRecomp launcher.
//
//   SplosionManRecomp                    open the launcher
//   SplosionManRecomp --print-command    print the game command Play would run, and exit
//   SplosionManRecomp --render-icon <file.png> <size>
//                                        write the app icon (used by the build)
#include "art.h"
#include "gamedata.h"
#include "launch.h"
#include "mainwindow.h"
#include "options.h"
#include "theme.h"

#include <QApplication>
#include <QImage>
#include <QStyleFactory>
#include <QTextStream>

#ifndef SMR_VERSION
#define SMR_VERSION "dev"
#endif

namespace {

void setIdentity()
{
    QCoreApplication::setOrganizationName(QStringLiteral("SplosionManRecomp"));
    QCoreApplication::setApplicationName(QStringLiteral("SplosionManRecomp"));
    QCoreApplication::setApplicationVersion(QStringLiteral(SMR_VERSION));
}

bool hasArg(int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc; ++i)
        if (qstrcmp(argv[i], name) == 0)
            return true;
    return false;
}

int printCommand(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    setIdentity();
    opts::Settings settings;
    settings.load();
    settings.gameRoot = gamedata::locate(settings.gameRoot);
    const launch::Plan plan = launch::build(settings);
    QTextStream out(stdout);
    if (!plan.ok) {
        QTextStream(stderr) << plan.error << "\n";
        return 1;
    }
    out << plan.program << "\n";
    for (const QString &arg : plan.args)
        out << arg << "\n";
    return 0;
}

int renderIcon(int argc, char **argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    const QStringList args = app.arguments();
    const int at = args.indexOf(QStringLiteral("--render-icon"));
    if (at < 0 || at + 2 >= args.size())
        return 2;
    return art::placeholderIcon(args.at(at + 2).toInt()).save(args.at(at + 1)) ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    if (hasArg(argc, argv, "--print-command"))
        return printCommand(argc, argv);
    if (hasArg(argc, argv, "--render-icon"))
        return renderIcon(argc, argv);

    QApplication app(argc, argv);
    setIdentity();
    QGuiApplication::setApplicationDisplayName(QStringLiteral("SplosionManRecomp"));
    // Matches the installed .desktop file, so Wayland shows the right icon.
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.thefixinhixon.SplosionManRecomp"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    opts::Settings settings;
    settings.load();
    const QString found = gamedata::locate(settings.gameRoot);
    if (found != settings.gameRoot && !found.isEmpty()) {
        settings.gameRoot = found;
        settings.save();
    }

    theme::Manager theme;
    theme.setMode(settings.appearance);

    MainWindow window(settings, theme);
    window.show();
    return app.exec();
}
