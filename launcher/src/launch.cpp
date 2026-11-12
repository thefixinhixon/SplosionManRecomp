// launch.cpp - see launch.h.
#include "launch.h"

#include "gamedata.h"
#include "options.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

namespace launch {

QStringList programFiles()
{
#if defined(Q_OS_WIN)
    return {QStringLiteral("splosionman.exe"), QStringLiteral("rexruntime.dll"),
            QStringLiteral("rexgpu-xenos.dll"), QStringLiteral("amd_fidelityfx_dx12.dll")};
#else
    return {QStringLiteral("splosionman"), QStringLiteral("librexruntime.so"),
            QStringLiteral("librexgpu-xenos.so")};
#endif
}

namespace {

// The first folder holding the whole program set: the launcher's own folder
// (release layout), then the game folder.
QString programDir(const QString &gameFolder, QStringList *searched)
{
    QStringList candidates{QCoreApplication::applicationDirPath()};
    if (!gameFolder.isEmpty())
        candidates << gameFolder;
    for (const QString &dir : candidates) {
        const QString clean = QDir::cleanPath(dir);
        if (searched->contains(clean))
            continue;
        searched->append(clean);
        bool complete = true;
        for (const QString &file : programFiles())
            complete = complete && QFileInfo(QDir(clean).filePath(file)).isFile();
        if (complete)
            return clean;
    }
    return {};
}

} // namespace

Plan build(const opts::Settings &settings)
{
    Plan plan;
    const QString gameFolder = settings.gameRoot;
    const QString dataDir = gamedata::dataDir(gameFolder);
    if (dataDir.isEmpty()) {
        plan.error = QStringLiteral("The game data was not found. Import your XBLA package or "
                                    "choose the folder that holds it.");
        return plan;
    }
    QStringList searched;
    const QString dir = programDir(gameFolder, &searched);
    if (dir.isEmpty()) {
        plan.error = QStringLiteral("The game's program files (%1) were not found together in "
                                    "one folder. Looked in:\n%2")
                         .arg(programFiles().join(QStringLiteral(", ")),
                              searched.join(QLatin1Char('\n')));
        return plan;
    }

    plan.program = QDir(dir).filePath(programFiles().constFirst());
    plan.workDir = dir;
    plan.logDir = gamedata::logDir(gameFolder);

    // Fixed: the GPU backend, the full game (not the trial), and on Linux two
    // Vulkan defaults that do not suit this game (sparse shared memory shows
    // a black screen on AMD RADV; skipping frames with unfinished shaders
    // flickers).
    plan.args << QStringLiteral("--gpu_plugin=xenos") << QStringLiteral("--license_mask=1");
#if !defined(Q_OS_WIN)
    plan.args << QStringLiteral("--vulkan_sparse_shared_memory=false")
              << QStringLiteral("--vulkan_async_skip_incomplete_frames=false");
#endif
    plan.args << QStringLiteral("--game_data_root=") + dataDir;
    // The default log location is next to the game program, which is
    // read-only inside an AppImage.
    plan.args << QStringLiteral("--log_file=") +
                     QDir(plan.logDir).filePath(QStringLiteral("game.log"));
    if (!settings.saveRoot.isEmpty())
        plan.args << QStringLiteral("--user_data_root=") + settings.saveRoot;
    plan.args << settings.gameArgs();
    plan.ok = true;
    return plan;
}

QString commandLine(const Plan &plan)
{
    QStringList parts{plan.program};
    parts << plan.args;
    for (QString &part : parts)
        if (part.contains(QLatin1Char(' ')))
            part = QLatin1Char('"') + part + QLatin1Char('"');
    return parts.join(QLatin1Char(' '));
}

bool start(const Plan &plan, QString *error)
{
    if (!plan.ok) {
        *error = plan.error;
        return false;
    }
    if (!QDir().mkpath(plan.logDir)) {
        *error = QStringLiteral("Cannot create the log folder %1.").arg(plan.logDir);
        return false;
    }
    // Written first, so it exists even if the game fails to start.
    QFile record(QDir(plan.logDir).filePath(QStringLiteral("last-command.txt")));
    if (record.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        record.write(commandLine(plan).toUtf8() + '\n');
    if (!QProcess::startDetached(plan.program, plan.args, plan.workDir)) {
        *error = QStringLiteral("The game could not be started:\n%1").arg(plan.program);
        return false;
    }
    return true;
}

} // namespace launch
