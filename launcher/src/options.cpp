// options.cpp - see options.h.
#include "options.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QSettings>

#include <cmath>

namespace opts {
namespace {

Choice c(const QString &id, const QString &label, QList<QPair<QString, QString>> flags)
{
    return Choice{id, label, std::move(flags)};
}

QList<Choice> onOff(const QString &flag)
{
    return {c(QStringLiteral("off"), QStringLiteral("Off"), {{flag, QStringLiteral("false")}}),
            c(QStringLiteral("on"), QStringLiteral("On"), {{flag, QStringLiteral("true")}})};
}

QList<Option> buildOptions()
{
    QList<Option> list;
    const QString perf = QStringLiteral("Performance");
    const QString gfx = QStringLiteral("Graphics");
    const QString audio = QStringLiteral("Audio");
    const QString controls = QStringLiteral("Controls");
    const QString game = QStringLiteral("Game");

    // --- Performance ---------------------------------------------------------
    {
        QList<Choice> limits;
        for (int fps : {30, 60, 75, 90, 120, 144, 165, 180, 240, 360})
            limits << c(QString::number(fps), QStringLiteral("%1 FPS").arg(fps),
                        {{QStringLiteral("frame_limit"), QString::number(fps)}});
        limits << c(QStringLiteral("0"), QStringLiteral("Unlimited"),
                    {{QStringLiteral("frame_limit"), QStringLiteral("0")}});
        list << Option{QStringLiteral("frame_limit"), perf, QStringLiteral("Frame rate limit"),
                       QStringLiteral("Caps how fast the game runs. The game's physics use real "
                                      "elapsed time, so any rate plays at the correct speed. Set "
                                      "it to your monitor's refresh rate (or a little below it "
                                      "with VRR, G-Sync or FreeSync) for the smoothest result. "
                                      "The cap adds almost no input lag."),
                       limits, QStringLiteral("0"), {}, {}};
    }
    list << Option{QStringLiteral("vsync"), perf, QStringLiteral("VSync"),
                   QStringLiteral("Off: lowest latency, may tear. On - low latency: no tearing, "
                                  "and newer frames replace queued ones, so the display never "
                                  "shows a stale frame (recommended). On - standard: classic "
                                  "vsync; smoothest on fixed-refresh displays without VRR, but "
                                  "adds up to a few frames of input lag."),
                   {c(QStringLiteral("off"), QStringLiteral("Off"),
                      {{QStringLiteral("vsync"), QStringLiteral("false")}}),
                    c(QStringLiteral("low_latency"), QStringLiteral("On - low latency"),
                      {{QStringLiteral("vsync"), QStringLiteral("true")},
                       {QStringLiteral("vsync_low_latency"), QStringLiteral("true")}}),
                    c(QStringLiteral("standard"), QStringLiteral("On - standard"),
                      {{QStringLiteral("vsync"), QStringLiteral("true")},
                       {QStringLiteral("vsync_low_latency"), QStringLiteral("false")}})},
                   QStringLiteral("low_latency"), {}, {}};
    list << Option{QStringLiteral("frame_queue"), perf, QStringLiteral("Frame queue"),
                   QStringLiteral("How many frames the game may prepare ahead of the GPU. 1 "
                                  "frame: lowest input lag (recommended). 2 frames: smoother on "
                                  "slow GPUs, about one frame more lag. Unbounded: the original "
                                  "behaviour."),
                   {c(QStringLiteral("1"), QStringLiteral("1 frame (lowest lag)"),
                      {{QStringLiteral("gpu_max_frames_ahead"), QStringLiteral("1")}}),
                    c(QStringLiteral("2"), QStringLiteral("2 frames"),
                      {{QStringLiteral("gpu_max_frames_ahead"), QStringLiteral("2")}}),
                    c(QStringLiteral("0"), QStringLiteral("Unbounded"),
                      {{QStringLiteral("gpu_max_frames_ahead"), QStringLiteral("0")}})},
                   QStringLiteral("1"), {}, {}};
    list << Option{QStringLiteral("perf_overlay"), perf, QStringLiteral("Performance overlay"),
                   QStringLiteral("Shows the frame rate in the top-left corner. Detailed adds "
                                  "frame time, CPU time per frame, time spent waiting on the GPU "
                                  "and a frame-time graph."),
                   {c(QStringLiteral("0"), QStringLiteral("Off"),
                      {{QStringLiteral("perf_overlay"), QStringLiteral("0")}}),
                    c(QStringLiteral("1"), QStringLiteral("FPS"),
                      {{QStringLiteral("perf_overlay"), QStringLiteral("1")}}),
                    c(QStringLiteral("2"), QStringLiteral("Detailed"),
                      {{QStringLiteral("perf_overlay"), QStringLiteral("2")}})},
                   QStringLiteral("0"), {}, {}};
    list << Option{QStringLiteral("menu_pause_game"), perf,
                   QStringLiteral("Pause the game while the F1 menu is open"),
                   QStringLiteral("Freezes the game on its last frame while you change options "
                                  "in the in-game menu. It resumes exactly where it stopped."),
                   onOff(QStringLiteral("menu_pause_game")), QStringLiteral("on"), {}, {}};
    list << Option{QStringLiteral("display_mode"), perf, QStringLiteral("Display mode"),
                   QStringLiteral("Fullscreen or windowed. Alt+Enter also switches while "
                                  "playing."),
                   {c(QStringLiteral("windowed"), QStringLiteral("Windowed"),
                      {{QStringLiteral("fullscreen"), QStringLiteral("false")}}),
                    c(QStringLiteral("fullscreen"), QStringLiteral("Fullscreen"),
                      {{QStringLiteral("fullscreen"), QStringLiteral("true")}})},
                   QStringLiteral("fullscreen"), {}, {}};

    // --- Graphics ------------------------------------------------------------
    list << Option{QStringLiteral("upscaling"), gfx, QStringLiteral("Upscaling"),
                   QStringLiteral("How the game image is scaled to your screen. Bilinear: plain "
                                  "scaling. AMD FSR 1: edge-aware upscaling, sharpest at 1080p "
                                  "and above. AMD CAS: contrast-adaptive sharpening with plain "
                                  "scaling."),
                   {c(QStringLiteral("bilinear"), QStringLiteral("Bilinear"),
                      {{QStringLiteral("present_effect"), QStringLiteral("bilinear")}}),
                    c(QStringLiteral("fsr"), QStringLiteral("AMD FSR 1"),
                      {{QStringLiteral("present_effect"), QStringLiteral("fsr")}}),
                    c(QStringLiteral("cas"), QStringLiteral("AMD CAS"),
                      {{QStringLiteral("present_effect"), QStringLiteral("cas")}})},
                   QStringLiteral("fsr"), {}, {}};
    {
        const QString f = QStringLiteral("present_fsr_sharpness_reduction");
        list << Option{QStringLiteral("fsr_sharpness"), gfx, QStringLiteral("FSR sharpness"),
                       QStringLiteral("How strongly FSR sharpens the upscaled image."),
                       {c(QStringLiteral("off"), QStringLiteral("Off"), {{f, QStringLiteral("2.0")}}),
                        c(QStringLiteral("low"), QStringLiteral("Low"), {{f, QStringLiteral("1.0")}}),
                        c(QStringLiteral("medium"), QStringLiteral("Medium"), {{f, QStringLiteral("0.5")}}),
                        c(QStringLiteral("high"), QStringLiteral("High"), {{f, QStringLiteral("0.2")}}),
                        c(QStringLiteral("max"), QStringLiteral("Maximum"), {{f, QStringLiteral("0.0")}})},
                       QStringLiteral("high"), QStringLiteral("upscaling"), QStringLiteral("fsr")};
    }
    {
        const QString f = QStringLiteral("present_cas_additional_sharpness");
        list << Option{QStringLiteral("cas_sharpness"), gfx, QStringLiteral("CAS sharpness"),
                       QStringLiteral("Extra CAS sharpening on top of the base amount."),
                       {c(QStringLiteral("base"), QStringLiteral("Base"), {{f, QStringLiteral("0.0")}}),
                        c(QStringLiteral("low"), QStringLiteral("Low"), {{f, QStringLiteral("0.25")}}),
                        c(QStringLiteral("medium"), QStringLiteral("Medium"), {{f, QStringLiteral("0.5")}}),
                        c(QStringLiteral("high"), QStringLiteral("High"), {{f, QStringLiteral("0.75")}}),
                        c(QStringLiteral("max"), QStringLiteral("Maximum"), {{f, QStringLiteral("1.0")}})},
                       QStringLiteral("base"), QStringLiteral("upscaling"), QStringLiteral("cas")};
    }
    list << Option{QStringLiteral("anti_aliasing"), gfx, QStringLiteral("Anti-aliasing (post)"),
                   QStringLiteral("FXAA smooths jagged edges at a tiny GPU cost. Extreme is "
                                  "stronger and softer."),
                   {c(QStringLiteral("none"), QStringLiteral("Off"),
                      {{QStringLiteral("swap_post_effect"), QStringLiteral("none")}}),
                    c(QStringLiteral("fxaa"), QStringLiteral("FXAA"),
                      {{QStringLiteral("swap_post_effect"), QStringLiteral("fxaa")}}),
                    c(QStringLiteral("fxaa_extreme"), QStringLiteral("FXAA extreme"),
                      {{QStringLiteral("swap_post_effect"), QStringLiteral("fxaa_extreme")}})},
                   QStringLiteral("fxaa"), {}, {}};
    list << Option{QStringLiteral("resolution"), gfx, QStringLiteral("Internal resolution"),
                   QStringLiteral("Renders the game at a multiple of its native 720p. Higher is "
                                  "sharper and costs GPU time."),
                   {c(QStringLiteral("1"), QStringLiteral("1x (720p)"),
                      {{QStringLiteral("resolution_scale"), QStringLiteral("1")}}),
                    c(QStringLiteral("2"), QStringLiteral("2x (1440p)"),
                      {{QStringLiteral("resolution_scale"), QStringLiteral("2")}}),
                    c(QStringLiteral("3"), QStringLiteral("3x (2160p)"),
                      {{QStringLiteral("resolution_scale"), QStringLiteral("3")}})},
                   QStringLiteral("1"), {}, {}};
    list << Option{QStringLiteral("msaa"), gfx, QStringLiteral("Native 2x MSAA"),
                   QStringLiteral("Uses your GPU's 2x MSAA for the game's multisampled surfaces, "
                                  "as on the Xbox 360."),
                   onOff(QStringLiteral("native_2x_msaa")), QStringLiteral("off"), {}, {}};
    {
        const QString f = QStringLiteral("anisotropic_override");
        list << Option{QStringLiteral("anisotropic"), gfx, QStringLiteral("Anisotropic filtering"),
                       QStringLiteral("Keeps floor and wall textures sharp at shallow angles. Very "
                                      "cheap on modern GPUs."),
                       {c(QStringLiteral("game"), QStringLiteral("Game default"), {{f, QStringLiteral("-1")}}),
                        c(QStringLiteral("off"), QStringLiteral("Off"), {{f, QStringLiteral("0")}}),
                        c(QStringLiteral("2x"), QStringLiteral("2x"), {{f, QStringLiteral("2")}}),
                        c(QStringLiteral("4x"), QStringLiteral("4x"), {{f, QStringLiteral("3")}}),
                        c(QStringLiteral("8x"), QStringLiteral("8x"), {{f, QStringLiteral("4")}}),
                        c(QStringLiteral("16x"), QStringLiteral("16x"), {{f, QStringLiteral("5")}})},
                       QStringLiteral("16x"), {}, {}};
    }

    // --- Audio ---------------------------------------------------------------
    {
        QList<Choice> volume;
        for (int pct = 0; pct <= 200; pct += 10)
            volume << c(QString::number(pct), QStringLiteral("%1%").arg(pct),
                        {{QStringLiteral("audio_output_gain"),
                          QString::number(pct / 100.0, 'f', 2)}});
        list << Option{QStringLiteral("volume"), audio, QStringLiteral("Master volume"),
                       QStringLiteral("Overall output volume. Above 100% boosts quiet mixes but "
                                      "can clip."),
                       volume, QStringLiteral("100"), {}, {}};
    }
    list << Option{QStringLiteral("mute"), audio, QStringLiteral("Mute"),
                   QStringLiteral("Silences all game audio."),
                   onOff(QStringLiteral("audio_mute")), QStringLiteral("off"), {}, {}};
    list << Option{QStringLiteral("force_stereo"), audio, QStringLiteral("Force stereo"),
                   QStringLiteral("Folds the 5.1 mix down to stereo even on a surround device "
                                  "(useful for headphones on a surround-configured output)."),
                   onOff(QStringLiteral("audio_force_stereo")), QStringLiteral("off"), {}, {}};

    // --- Controls ------------------------------------------------------------
    {
        QList<Choice> rumble;
        for (int pct : {0, 25, 50, 75, 100})
            rumble << c(QString::number(pct),
                        pct ? QStringLiteral("%1%").arg(pct) : QStringLiteral("Off"),
                        {{QStringLiteral("vibration_strength"), QString::number(pct)}});
        list << Option{QStringLiteral("vibration"), controls, QStringLiteral("Vibration"),
                       QStringLiteral("Controller rumble strength."), rumble,
                       QStringLiteral("100"), {}, {}};
    }
    list << Option{QStringLiteral("keyboard"), controls, QStringLiteral("Keyboard as controller"),
                   QStringLiteral("Play with the keyboard: WASD move, Space or ; jump ('splode), "
                                  "Enter start, Tab back. Works alongside a controller."),
                   onOff(QStringLiteral("mnk_mode")), QStringLiteral("off"), {}, {}};
    list << Option{QStringLiteral("dpad_movement"), controls, QStringLiteral("D-pad moves"),
                   QStringLiteral("Move with the D-pad as well as the left stick. The original "
                                  "game ignores the D-pad while you play. The stick wins if you "
                                  "use both."),
                   onOff(QStringLiteral("dpad_movement")), QStringLiteral("on"), {}, {}};
    list << Option{QStringLiteral("mouse_cursor"), controls, QStringLiteral("Mouse cursor"),
                   QStringLiteral("Hide when idle: the cursor appears when you move the mouse and "
                                  "hides again after 2 seconds. Always hide: never shown over the "
                                  "game, except while the in-game menu is open."),
                   {c(QStringLiteral("show"), QStringLiteral("Always show"),
                      {{QStringLiteral("mouse_cursor"), QStringLiteral("0")}}),
                    c(QStringLiteral("idle"), QStringLiteral("Hide when idle"),
                      {{QStringLiteral("mouse_cursor"), QStringLiteral("1")}}),
                    c(QStringLiteral("hide"), QStringLiteral("Always hide"),
                      {{QStringLiteral("mouse_cursor"), QStringLiteral("2")}})},
                   QStringLiteral("idle"), {}, {}};
    list << Option{QStringLiteral("menu_hint"), controls,
                   QStringLiteral("Show the in-game menu hint"),
                   QStringLiteral("Shows \"F1 or L3 + R3: options\" for a few seconds when the "
                                  "game starts."),
                   onOff(QStringLiteral("show_menu_hint")), QStringLiteral("on"), {}, {}};

    // --- Game (launcher only) ------------------------------------------------
    {
        const QString f = QStringLiteral("user_language");
        const QList<QPair<int, QString>> languages = {
            {1, QStringLiteral("English")},  {2, QStringLiteral("Japanese")},
            {3, QStringLiteral("German")},   {4, QStringLiteral("French")},
            {5, QStringLiteral("Spanish")},  {6, QStringLiteral("Italian")},
            {7, QStringLiteral("Korean")},   {8, QStringLiteral("Chinese (Traditional)")},
            {9, QStringLiteral("Portuguese")}, {11, QStringLiteral("Polish")},
            {12, QStringLiteral("Russian")}};
        QList<Choice> langs;
        for (const auto &l : languages)
            langs << c(QString::number(l.first), l.second, {{f, QString::number(l.first)}});
        list << Option{QStringLiteral("language"), game, QStringLiteral("Language"),
                       QStringLiteral("The console language the game sees. If the game has no "
                                      "text in that language, it uses English."),
                       langs, QStringLiteral("1"), {}, {}};
    }
    list << Option{QStringLiteral("log_level"), game, QStringLiteral("Detailed logging"),
                   QStringLiteral("Writes much more detail to game.log. Turn it on when you "
                                  "report a bug, off for normal play."),
                   {c(QStringLiteral("off"), QStringLiteral("Off"),
                      {{QStringLiteral("log_level"), QStringLiteral("info")}}),
                    c(QStringLiteral("on"), QStringLiteral("On"),
                      {{QStringLiteral("log_level"), QStringLiteral("debug")}})},
                   QStringLiteral("off"), {}, {}};
    return list;
}

QString nearestId(const Option &o, double value, const QString &flag)
{
    QString best = o.defaultId;
    double bestDist = 1e9;
    for (const Choice &ch : o.choices)
        for (const auto &f : ch.flags)
            if (f.first == flag) {
                const double d = std::fabs(f.second.toDouble() - value);
                if (d < bestDist) {
                    bestDist = d;
                    best = ch.id;
                }
            }
    return best;
}

} // namespace

const QList<Option> &all()
{
    static const QList<Option> list = buildOptions();
    return list;
}

const Option *find(const QString &key)
{
    for (const Option &o : all())
        if (o.key == key)
            return &o;
    return nullptr;
}

QSettings *settingsFile()
{
    // ~/.config/SplosionManRecomp/launcher.ini, %APPDATA%\SplosionManRecomp\launcher.ini
    static QSettings file(QSettings::IniFormat, QSettings::UserScope,
                          QStringLiteral("SplosionManRecomp"), QStringLiteral("launcher"));
    return &file;
}

void Settings::load()
{
    QSettings &s = *settingsFile();
    if (!s.contains(QStringLiteral("launcher/version")))
        migrateFromOldLauncher(s);

    choices_.clear();
    s.beginGroup(QStringLiteral("game"));
    for (const Option &o : all()) {
        const QString id = s.value(o.key).toString();
        bool known = false;
        for (const Choice &ch : o.choices)
            known = known || ch.id == id;
        choices_[o.key] = known ? id : o.defaultId;
    }
    s.endGroup();
    gameRoot = s.value(QStringLiteral("folders/game")).toString();
    saveRoot = s.value(QStringLiteral("folders/saves")).toString();
    appearance = s.value(QStringLiteral("launcher/appearance"), QStringLiteral("system")).toString();
    if (appearance != QLatin1String("light") && appearance != QLatin1String("dark"))
        appearance = QStringLiteral("system");
}

void Settings::save() const
{
    QSettings &s = *settingsFile();
    s.setValue(QStringLiteral("launcher/version"), 1);
    s.beginGroup(QStringLiteral("game"));
    for (const Option &o : all())
        s.setValue(o.key, choiceId(o.key));
    s.endGroup();
    s.setValue(QStringLiteral("folders/game"), gameRoot);
    s.setValue(QStringLiteral("folders/saves"), saveRoot);
    s.setValue(QStringLiteral("launcher/appearance"), appearance);
    s.sync();
}

void Settings::resetToDefaults()
{
    for (const Option &o : all())
        choices_[o.key] = o.defaultId;
}

QString Settings::choiceId(const QString &key) const
{
    if (auto it = choices_.find(key); it != choices_.end())
        return *it;
    const Option *o = find(key);
    return o ? o->defaultId : QString();
}

void Settings::setChoiceId(const QString &key, const QString &id)
{
    choices_[key] = id;
}

bool Settings::isShown(const Option &option) const
{
    return option.shownWhenKey.isEmpty() || choiceId(option.shownWhenKey) == option.shownWhenId;
}

QStringList Settings::gameArgs() const
{
    QStringList args;
    for (const Option &o : all()) {
        const QString id = choiceId(o.key);
        for (const Choice &ch : o.choices) {
            if (ch.id != id)
                continue;
            for (const auto &f : ch.flags)
                args << QStringLiteral("--%1=%2").arg(f.first, f.second);
        }
    }
    return args;
}

// The previous launcher ("TP Splosionman Launcher", up to v2.2.2) kept its
// settings in HouseRecomp/TP Splosionman Launcher.ini. Its values are read
// once, on the first start of this launcher, for the options that still
// exist; the old file is left as it was.
void Settings::migrateFromOldLauncher(QSettings &out)
{
    QSettings old(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("HouseRecomp"),
                  QStringLiteral("TP Splosionman Launcher"));
    if (!QFileInfo::exists(old.fileName()))
        return;

    auto set = [&out](const QString &key, const QString &id) {
        out.setValue(QStringLiteral("game/") + key, id);
    };
    const QString root = old.value(QStringLiteral("detection/5841098F/root")).toString();
    if (!root.isEmpty())
        out.setValue(QStringLiteral("folders/game"), root);

    old.beginGroup(QStringLiteral("settings"));
    if (old.contains(QStringLiteral("resolution_scale")))
        set(QStringLiteral("resolution"),
            QString::number(qBound(1, old.value(QStringLiteral("resolution_scale")).toInt(), 3)));
    if (old.contains(QStringLiteral("upscaling")))
        set(QStringLiteral("upscaling"),
            old.value(QStringLiteral("upscaling")).toString().compare(QStringLiteral("FSR"),
                                                                      Qt::CaseInsensitive) == 0
                ? QStringLiteral("fsr")
                : QStringLiteral("bilinear"));
    if (old.contains(QStringLiteral("swap_post_effect")))
        set(QStringLiteral("anti_aliasing"), old.value(QStringLiteral("swap_post_effect")).toString());
    if (old.contains(QStringLiteral("anisotropic_override"))) {
        const int x = old.value(QStringLiteral("anisotropic_override")).toInt();
        set(QStringLiteral("anisotropic"),
            x <= 0 ? QStringLiteral("game")
            : x >= 16 ? QStringLiteral("16x")
            : x >= 8  ? QStringLiteral("8x")
            : x >= 4  ? QStringLiteral("4x")
                      : QStringLiteral("2x"));
    }
    if (old.contains(QStringLiteral("native_2x_msaa")))
        set(QStringLiteral("msaa"), old.value(QStringLiteral("native_2x_msaa")).toBool()
                                        ? QStringLiteral("on")
                                        : QStringLiteral("off"));
    if (old.contains(QStringLiteral("vsync")))
        set(QStringLiteral("vsync"), old.value(QStringLiteral("vsync")).toBool()
                                         ? QStringLiteral("low_latency")
                                         : QStringLiteral("off"));
    if (old.contains(QStringLiteral("frame_limit")))
        set(QStringLiteral("frame_limit"),
            nearestId(*find(QStringLiteral("frame_limit")),
                      old.value(QStringLiteral("frame_limit")).toInt(), QStringLiteral("frame_limit")));
    if (old.contains(QStringLiteral("windowed")))
        set(QStringLiteral("display_mode"), old.value(QStringLiteral("windowed")).toBool()
                                                ? QStringLiteral("windowed")
                                                : QStringLiteral("fullscreen"));
    if (old.contains(QStringLiteral("audio_gain")))
        set(QStringLiteral("volume"),
            nearestId(*find(QStringLiteral("volume")), old.value(QStringLiteral("audio_gain")).toDouble(),
                      QStringLiteral("audio_output_gain")));
    if (old.contains(QStringLiteral("audio_force_stereo")))
        set(QStringLiteral("force_stereo"), old.value(QStringLiteral("audio_force_stereo")).toBool()
                                                ? QStringLiteral("on")
                                                : QStringLiteral("off"));
    if (old.value(QStringLiteral("log_level")).toString() == QLatin1String("debug"))
        set(QStringLiteral("log_level"), QStringLiteral("on"));
    if (const int lang = old.value(QStringLiteral("user_language")).toInt(); lang > 0)
        set(QStringLiteral("language"), QString::number(lang));
    const QString saves = old.value(QStringLiteral("user_data_root")).toString();
    if (!saves.isEmpty())
        out.setValue(QStringLiteral("folders/saves"), saves);
    old.endGroup();
    out.setValue(QStringLiteral("launcher/version"), 1);
    out.sync();
}

} // namespace opts
