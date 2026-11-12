// mainwindow.cpp - see mainwindow.h.
#include "mainwindow.h"

#include "art.h"
#include "gamedata.h"
#include "launch.h"
#include "stfs.h"
#include "theme.h"

#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

#include <atomic>
#include <memory>

namespace {

QLabel *makeLabel(const QString &text, const char *role, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("role", QString::fromLatin1(role));
    label->setWordWrap(true);
    return label;
}

QString suggestedImportParent()
{
    const QString games = QDir::home().filePath(QStringLiteral("Games"));
    return QDir(games).exists() ? games : QDir::homePath();
}

} // namespace

// --- Banner -------------------------------------------------------------------

Banner::Banner(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void Banner::setDark(bool dark)
{
    dark_ = dark;
    update();
}

void Banner::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QPixmap art = art::gameBanner();
    if (art.isNull()) {
        p.drawPixmap(0, 0, art::placeholderBanner(size(), dark_));
    } else {
        // Cover the widget, keeping the image's top (the logo) in view.
        const QSize scaled = art.size().scaled(size(), Qt::KeepAspectRatioByExpanding);
        const QRect target((width() - scaled.width()) / 2, 0, scaled.width(), scaled.height());
        p.drawPixmap(target, art);
    }
    // Fade into the window below.
    QLinearGradient fade(0, height() - 22, 0, height());
    QColor window = palette().color(QPalette::Window);
    window.setAlpha(0);
    fade.setColorAt(0, window);
    window.setAlpha(255);
    fade.setColorAt(1, window);
    p.fillRect(0, height() - 22, width(), 22, fade);
}

// --- MainWindow -----------------------------------------------------------------

MainWindow::MainWindow(opts::Settings &settings, theme::Manager &theme)
    : settings_(settings), theme_(theme)
{
    setWindowTitle(QStringLiteral("SplosionManRecomp"));
    setWindowIcon(art::icon());
    resize(860, 760);
    setMinimumSize(640, 560);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    banner_ = new Banner(central);
    banner_->setFixedHeight(260);
    layout->addWidget(banner_);

    auto *tabs = new QTabWidget(central);
    tabs->setDocumentMode(true);
    tabs->addTab(buildPlayPage(), QStringLiteral("Play"));
    tabs->addTab(buildSettingsPage(), QStringLiteral("Settings"));
    layout->addWidget(tabs, 1);
    setCentralWidget(central);

    banner_->setDark(theme_.isDark());
    connect(&theme_, &theme::Manager::changed, this, [this](bool dark) {
        banner_->setDark(dark);
        update();
    });
    refreshGame();
}

QWidget *MainWindow::buildPlayPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 18, 28, 16);
    layout->setSpacing(12);

    auto *card = new QFrame(page);
    card->setObjectName(QStringLiteral("card"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(18, 14, 18, 14);
    status_ = makeLabel(QString(), "status", card);
    statusDetail_ = makeLabel(QString(), "help", card);
    statusDetail_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cardLayout->addWidget(status_);
    cardLayout->addWidget(statusDetail_);
    layout->addWidget(card);

    play_ = new QPushButton(QStringLiteral("PLAY"), page);
    play_->setObjectName(QStringLiteral("play"));
    play_->setCursor(Qt::PointingHandCursor);
    play_->setMinimumHeight(60);
    connect(play_, &QPushButton::clicked, this, &MainWindow::onPlay);
    layout->addWidget(play_);

    auto *buttons = new QHBoxLayout();
    auto *import = new QPushButton(QStringLiteral("Import XBLA package..."), page);
    auto *choose = new QPushButton(QStringLiteral("Choose game folder..."), page);
    auto *importDlc = new QPushButton(QStringLiteral("Import DLC..."), page);
    openLogs_ = new QPushButton(QStringLiteral("Open logs folder"), page);
    connect(import, &QPushButton::clicked, this, &MainWindow::onImport);
    connect(importDlc, &QPushButton::clicked, this, &MainWindow::onImportDlc);
    connect(choose, &QPushButton::clicked, this, &MainWindow::onChooseFolder);
    connect(openLogs_, &QPushButton::clicked, this, &MainWindow::onOpenLogs);
    buttons->addWidget(import);
    buttons->addWidget(choose);
    buttons->addWidget(importDlc);
    buttons->addWidget(openLogs_);
    layout->addLayout(buttons);

    layout->addStretch(1);
    layout->addWidget(makeLabel(
        QStringLiteral("SplosionManRecomp %1. An unofficial fan project, not affiliated with "
                       "Twisted Pixel Games or Microsoft. No game data is included: you need "
                       "your own copy of the game.")
            .arg(QCoreApplication::applicationVersion()),
        "footer", page));
    return page;
}

QWidget *MainWindow::buildSettingsPage()
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *page = new QWidget(scroll);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 8, 28, 20);
    layout->setSpacing(4);

    layout->addWidget(makeLabel(
        QStringLiteral("These are the same options as the in-game menu (F1, or click both "
                       "sticks). A change made here replaces one made earlier in that menu."),
        "help", page));

    QString section;
    QGridLayout *grid = nullptr;
    int row = 0;
    auto startSection = [&](const QString &title) {
        layout->addWidget(makeLabel(title, "heading", page));
        grid = new QGridLayout();
        grid->setHorizontalSpacing(18);
        grid->setVerticalSpacing(2);
        grid->setColumnStretch(0, 1);
        layout->addLayout(grid);
        row = 0;
    };

    for (const opts::Option &option : opts::all()) {
        if (option.section != section) {
            section = option.section;
            startSection(section);
        }
        auto *label = new QLabel(option.label, page);
        auto *combo = new QComboBox(page);
        combo->setMinimumWidth(220);
        for (const opts::Choice &choice : option.choices)
            combo->addItem(choice.label, choice.id);
        auto *help = makeLabel(option.help, "help", page);
        help->setContentsMargins(0, 0, 0, 8);
        grid->addWidget(label, row, 0);
        grid->addWidget(combo, row, 1);
        grid->addWidget(help, row + 1, 0, 1, 2);
        row += 2;
        combos_.insert(option.key, combo);
        rows_.insert(option.key, {label, combo, help});
        connect(combo, &QComboBox::activated, this, [this, key = option.key, combo] {
            settings_.setChoiceId(key, combo->currentData().toString());
            settings_.save();
            refreshVisibility();
        });
    }

    // Folders.
    startSection(QStringLiteral("Saves"));
    saveFolder_ = new QLabel(page);
    saveFolder_->setWordWrap(true);
    saveFolder_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *changeSaves = new QPushButton(QStringLiteral("Change..."), page);
    auto *defaultSaves = new QPushButton(QStringLiteral("Use default"), page);
    connect(changeSaves, &QPushButton::clicked, this, &MainWindow::onChooseSaveFolder);
    connect(defaultSaves, &QPushButton::clicked, this, [this] {
        settings_.saveRoot.clear();
        settings_.save();
        refreshGame();
    });
    auto *saveButtons = new QHBoxLayout();
    saveButtons->addWidget(changeSaves);
    saveButtons->addWidget(defaultSaves);
    grid->addWidget(saveFolder_, row, 0);
    grid->addLayout(saveButtons, row, 1);
    grid->addWidget(makeLabel(QStringLiteral("Where the game keeps your save data and its "
                                             "shader cache. Moving it does not move existing "
                                             "saves."),
                              "help", page),
                    row + 1, 0, 1, 2);

    // Launcher.
    startSection(QStringLiteral("Launcher"));
    appearance_ = new QComboBox(page);
    appearance_->addItem(QStringLiteral("Same as system"), QStringLiteral("system"));
    appearance_->addItem(QStringLiteral("Light"), QStringLiteral("light"));
    appearance_->addItem(QStringLiteral("Dark"), QStringLiteral("dark"));
    appearance_->setMinimumWidth(220);
    connect(appearance_, &QComboBox::activated, this, [this] {
        settings_.appearance = appearance_->currentData().toString();
        settings_.save();
        theme_.setMode(settings_.appearance);
    });
    grid->addWidget(new QLabel(QStringLiteral("Appearance"), page), 0, 0);
    grid->addWidget(appearance_, 0, 1);
    grid->addWidget(makeLabel(QStringLiteral("Light or dark launcher. Same as system follows "
                                             "your desktop setting."),
                              "help", page),
                    1, 0, 1, 2);

    auto *reset = new QPushButton(QStringLiteral("Reset game options to defaults"), page);
    connect(reset, &QPushButton::clicked, this, [this] {
        settings_.resetToDefaults();
        settings_.save();
        loadChoicesIntoControls();
    });
    layout->addSpacing(14);
    layout->addWidget(reset, 0, Qt::AlignLeft);
    layout->addStretch(1);

    scroll->setWidget(page);
    loadChoicesIntoControls();
    return scroll;
}

void MainWindow::loadChoicesIntoControls()
{
    for (const opts::Option &option : opts::all()) {
        QComboBox *combo = combos_.value(option.key);
        combo->setCurrentIndex(qMax(0, combo->findData(settings_.choiceId(option.key))));
    }
    appearance_->setCurrentIndex(qMax(0, appearance_->findData(settings_.appearance)));
    refreshVisibility();
}

void MainWindow::refreshVisibility()
{
    for (const opts::Option &option : opts::all())
        for (QWidget *w : rows_.value(option.key))
            w->setVisible(settings_.isShown(option));
}

void MainWindow::refreshGame()
{
    const QString dataDir = gamedata::dataDir(settings_.gameRoot);
    art::refresh(dataDir);
    setWindowIcon(art::icon());
    QApplication::setWindowIcon(art::icon());
    banner_->update();

    if (dataDir.isEmpty()) {
        status_->setText(QStringLiteral("Game data needed"));
        statusDetail_->setText(QStringLiteral(
            "Import your XBLA package of 'Splosion Man (title ID 5841098F), or choose a folder "
            "that already holds the extracted game (the one with default.xex)."));
    } else {
        const launch::Plan plan = launch::build(settings_);
        status_->setText(plan.ok ? QStringLiteral("Ready to play")
                                 : QStringLiteral("Game program files missing"));
        statusDetail_->setText(plan.ok ? QStringLiteral("Game data: %1").arg(QDir::toNativeSeparators(dataDir))
                                       : plan.error);
    }
    // Installed DLC, by the name each package had when it was imported.
    QStringList dlc;
    for (const QString &folder : gamedata::installedDlc(gamedata::saveRoot(settings_.saveRoot)))
        dlc << opts::settingsFile()->value(QStringLiteral("dlc/") + folder, folder).toString();
    if (!dlc.isEmpty())
        statusDetail_->setText(statusDetail_->text() + QStringLiteral("\nDLC: ") +
                               dlc.join(QStringLiteral(", ")));
    play_->setEnabled(!dataDir.isEmpty());
    openLogs_->setEnabled(!settings_.gameRoot.isEmpty() && !dataDir.isEmpty());

    const QString saves = QDir::toNativeSeparators(gamedata::saveRoot(settings_.saveRoot));
    saveFolder_->setText(settings_.saveRoot.isEmpty() ? QStringLiteral("Default (%1)").arg(saves)
                                                      : saves);
}

void MainWindow::setGameFolder(const QString &folder)
{
    settings_.gameRoot = QDir::cleanPath(folder);
    settings_.save();
    refreshGame();
}

void MainWindow::onPlay()
{
    const launch::Plan plan = launch::build(settings_);
    QString error;
    if (!launch::start(plan, &error)) {
        QMessageBox::warning(this, QStringLiteral("Cannot start the game"), error);
        return;
    }
    status_->setText(QStringLiteral("Game started"));
    statusDetail_->setText(QStringLiteral("The exact command is in last-command.txt in the logs "
                                          "folder; the game writes game.log there."));
}

std::shared_ptr<StfsPackage> MainWindow::askForPackage(const QString &title)
{
    const QString path = QFileDialog::getOpenFileName(this, title, QDir::homePath());
    if (path.isEmpty())
        return {};
    auto package = std::make_shared<StfsPackage>();
    QString error;
    if (!package->open(path, &error)) {
        QMessageBox::warning(this, title,
                             QStringLiteral("This file cannot be read as an Xbox 360 package.\n\n%1")
                                 .arg(error));
        return {};
    }
    if (package->titleId() != gamedata::kTitleId) {
        QMessageBox::warning(this, title,
                             QStringLiteral("This package is for another game: \"%1\" (title ID %2).")
                                 .arg(package->displayName(),
                                      QString::number(package->titleId(), 16).toUpper()));
        return {};
    }
    if (package->contentType() == kTitleUpdate) {
        QMessageBox::information(
            this, title,
            QStringLiteral("\"%1\" is a title update. The game can't use title updates, and "
                           "doesn't need them: this port includes its own fixes.")
                .arg(package->displayName()));
        return {};
    }
    return package;
}

void MainWindow::extract(std::shared_ptr<StfsPackage> package, const QString &destDir,
                         const QString &title, std::function<void()> onSuccess)
{
    auto *progress = new QProgressDialog(QStringLiteral("Extracting \"%1\"...")
                                             .arg(package->displayName()),
                                         QStringLiteral("Cancel"), 0, 1000, this);
    progress->setWindowTitle(title);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setValue(0);

    auto cancelled = std::make_shared<std::atomic_bool>(false);
    auto result = std::make_shared<QString>();
    auto ok = std::make_shared<bool>(false);
    connect(progress, &QProgressDialog::canceled, this, [cancelled] { *cancelled = true; });
    const quint64 total = qMax<quint64>(1, package->totalBytes());

    QThread *worker = QThread::create([package, destDir, progress, cancelled, result, ok, total] {
        QString error;
        *ok = package->extractAll(
            destDir,
            [progress, cancelled, total](quint64 done) {
                const int value = int(done * 1000 / total);
                QMetaObject::invokeMethod(progress, [progress, value] { progress->setValue(value); });
                return !cancelled->load();
            },
            &error);
        *result = error;
    });
    connect(worker, &QThread::finished, this, [this, worker, progress, ok, result, title, onSuccess] {
        progress->close();
        progress->deleteLater();
        worker->deleteLater();
        if (!*ok) {
            if (*result != QLatin1String("Cancelled."))
                QMessageBox::warning(this, title,
                                     QStringLiteral("The import failed.\n\n%1").arg(*result));
            return;
        }
        onSuccess();
    });
    worker->start();
}

void MainWindow::onImport()
{
    const QString title = QStringLiteral("Import XBLA package");
    auto package = askForPackage(QStringLiteral("Choose your 'Splosion Man XBLA package"));
    if (!package)
        return;
    if (package->contentType() == kDownloadableContent) {
        installDlc(package);
        return;
    }
    bool hasXex = false;
    for (const StfsPackage::Entry &entry : package->entries())
        hasXex = hasXex || entry.path.compare(QStringLiteral("default.xex"), Qt::CaseInsensitive) == 0;
    if (!hasXex) {
        QMessageBox::warning(this, title,
                             QStringLiteral("\"%1\" is not the game itself (it has no default.xex).")
                                 .arg(package->displayName()));
        return;
    }

    const QString parent = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Choose where to put the game"), suggestedImportParent());
    if (parent.isEmpty())
        return;
    const QString root = QDir(parent).filePath(QStringLiteral("SplosionMan"));
    extract(package, QDir(root).filePath(QStringLiteral("gamedata")), title,
            [this, root] { setGameFolder(root); });
}

void MainWindow::onImportDlc()
{
    auto package = askForPackage(QStringLiteral("Choose a 'Splosion Man DLC package"));
    if (!package)
        return;
    if (package->contentType() != kDownloadableContent) {
        QMessageBox::warning(this, QStringLiteral("Import DLC"),
                             QStringLiteral("\"%1\" is not downloadable content. To import the game "
                                            "itself, use Import XBLA package.")
                                 .arg(package->displayName()));
        return;
    }
    installDlc(package);
}

// Downloadable content goes where the game looks for it: a folder named after
// the package file, under <save folder>/0000000000000000/5841098F/00000002.
void MainWindow::installDlc(std::shared_ptr<StfsPackage> package)
{
    const QString name = QFileInfo(package->fileName()).fileName();
    const QString dest =
        QDir(gamedata::dlcDir(gamedata::saveRoot(settings_.saveRoot))).filePath(name);
    const QString displayName = package->displayName();
    extract(package, dest, QStringLiteral("Import DLC"), [this, name, displayName] {
        opts::settingsFile()->setValue(QStringLiteral("dlc/") + name, displayName);
        opts::settingsFile()->sync();
        refreshGame();
        QMessageBox::information(this, QStringLiteral("Import DLC"),
                                 QStringLiteral("\"%1\" is installed.").arg(displayName));
    });
}

void MainWindow::onChooseFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Choose the folder with the extracted game"),
        settings_.gameRoot.isEmpty() ? suggestedImportParent() : settings_.gameRoot);
    if (folder.isEmpty())
        return;
    if (gamedata::dataDir(folder).isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Choose game folder"),
                             QStringLiteral("That folder does not contain 'Splosion Man's "
                                            "default.xex, directly or in a gamedata folder."));
        return;
    }
    setGameFolder(folder);
}

void MainWindow::onOpenLogs()
{
    const QString dir = gamedata::logDir(settings_.gameRoot);
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::onChooseSaveFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Choose where the game keeps its saves"),
        settings_.saveRoot.isEmpty() ? QDir::homePath() : settings_.saveRoot);
    if (folder.isEmpty())
        return;
    settings_.saveRoot = QDir::cleanPath(folder);
    settings_.save();
    refreshGame();
}
