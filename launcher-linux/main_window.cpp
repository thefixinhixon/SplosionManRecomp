#include "main_window.h"

#include "import_service.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressDialog>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace splosion::launcher {
namespace {

QComboBox* valueCombo(
    std::initializer_list<std::pair<QString, QVariant>> entries) {
  auto* result = new QComboBox;
  for (const auto& [label, value] : entries) {
    result->addItem(label, value);
  }
  return result;
}

void selectValue(QComboBox* combo, const QVariant& value) {
  const int index = combo->findData(value);
  combo->setCurrentIndex(index >= 0 ? index : 0);
}

QGroupBox* group(const QString& title, QLayout* layout) {
  auto* box = new QGroupBox(title);
  box->setLayout(layout);
  return box;
}

}  

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
#if defined(__aarch64__)
  setWindowTitle(QStringLiteral("'Splosion Man — ARM64"));
#elif defined(__x86_64__)
  setWindowTitle(QStringLiteral("'Splosion Man — x86_64 Linux"));
#else
  setWindowTitle(QStringLiteral("'Splosion Man"));
#endif
  resize(920, 650);
  setMinimumSize(760, 560);

  auto* tabs = new QTabWidget;
  tabs->addTab(createPlayPage(), tr("PLAY"));
  tabs->addTab(createGraphicsPage(), tr("GRAPHICS"));
  tabs->addTab(createAudioPage(), tr("AUDIO"));
  tabs->addTab(createLocalePage(), tr("LANGUAGE"));
  setCentralWidget(tabs);

  setStyleSheet(QStringLiteral(R"(
    QMainWindow, QWidget { background: #160808; color: #f0e4dc; }
    QTabWidget::pane, QGroupBox { border: 1px solid #603020; }
    QTabBar::tab { background: #25100c; padding: 10px 18px; }
    QTabBar::tab:selected { background: #7a250c; }
    QPushButton { background: #8f2d0d; border: 1px solid #c65322;
                  padding: 8px 14px; border-radius: 3px; }
    QPushButton:hover { background: #ad3a12; }
    QPushButton:disabled { background: #3b2822; color: #89766e; }
    QComboBox, QSpinBox { background: #251712; padding: 5px; }
    QGroupBox { margin-top: 12px; padding-top: 12px; font-weight: bold; }
    QGroupBox::title { subcontrol-origin: margin; left: 10px; }
  )"));

  dataRoot_ = GameConfig::selectedDataRoot();
  gameRoot_ = GameConfig::selectedGameRoot();

  // Portable defaults so most users never see a setup prompt: launcher data
  // lives in the per-user application data folder, and the game folder is
  // auto-detected when it sits in a conventional location. Both remain
  // changeable from the Play page and are remembered once chosen.
  if (dataRoot_.isEmpty()) {
    const QString fallback =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!fallback.isEmpty() && QDir().mkpath(fallback)) {
      dataRoot_ = QDir::cleanPath(fallback);
      GameConfig::setSelectedDataRoot(dataRoot_);
    }
  }
  // A remembered game folder is only trusted if it still holds game data
  // and is not inside the system temp folder (a stray default.xex there
  // once hijacked auto-detection); otherwise fall through to detection.
  if (!gameRoot_.isEmpty()) {
    const QString clean = QDir::cleanPath(gameRoot_);
    const QString tempRoot = QDir::cleanPath(QDir::tempPath());
    if (clean == tempRoot || clean.startsWith(tempRoot + QLatin1Char('/')) ||
        !ImportService::hasValidGame(clean)) {
      gameRoot_.clear();
    }
  }
  if (gameRoot_.isEmpty()) {
    const QString detected = detectGameRoot();
    if (!detected.isEmpty()) {
      gameRoot_ = detected;
      GameConfig::setSelectedGameRoot(gameRoot_);
    }
  }

  if (!dataRoot_.isEmpty()) {
    const QString testFile =
        QDir(dataRoot_).filePath(QStringLiteral(".__splosion_write_test__"));
    QFile f(testFile);
    if (!f.open(QIODevice::WriteOnly)) {
      dataRoot_.clear();
    } else {
      f.close();
      QFile::remove(testFile);
    }
  }

  if (!dataRoot_.isEmpty()) {
    loadSettings();
  } else {
    applySettings(GameSettings{});
  }
  updateStatus();

  QTimer::singleShot(0, this, [this] {
    if (dataRoot_.isEmpty() || gameRoot_.isEmpty()) {
      runFirstSetup();
    }
  });
}

QWidget* MainWindow::createPlayPage() {
  auto* page = new QWidget;
  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(36, 32, 36, 32);

  auto* title = new QLabel(tr("'SPLOSION MAN"));
  QFont titleFont = title->font();
  titleFont.setPointSize(26);
  titleFont.setBold(true);
  title->setFont(titleFont);
  title->setAlignment(Qt::AlignCenter);
  layout->addWidget(title);

  statusLabel_ = new QLabel;
  statusLabel_->setWordWrap(true);
  statusLabel_->setAlignment(Qt::AlignCenter);
  layout->addWidget(statusLabel_);

  dataRootLabel_ = new QLabel;
  dataRootLabel_->setWordWrap(true);
  dataRootLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  dataRootLabel_->setAlignment(Qt::AlignCenter);
  layout->addWidget(dataRootLabel_);

  gameRootLabel_ = new QLabel;
  gameRootLabel_->setWordWrap(true);
  gameRootLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  gameRootLabel_->setAlignment(Qt::AlignCenter);
  layout->addWidget(gameRootLabel_);
  layout->addStretch();

  playButton_ = new QPushButton(tr("PLAY"));
  playButton_->setMinimumSize(230, 80);
  QFont playFont = playButton_->font();
  playFont.setPointSize(24);
  playFont.setBold(true);
  playButton_->setFont(playFont);
  connect(playButton_, &QPushButton::clicked, this, &MainWindow::play);
  layout->addWidget(playButton_, 0, Qt::AlignCenter);

  auto* actions = new QHBoxLayout;
  auto* dataRootButton = new QPushButton(tr("Change save data"));
  auto* gameRootButton = new QPushButton(tr("Change game folder"));
  importIsoButton_ = new QPushButton(tr("Import ISO"));
  auto* logsButton = new QPushButton(tr("Open logs"));
  connect(dataRootButton, &QPushButton::clicked, this,
          &MainWindow::chooseDataRoot);
  connect(gameRootButton, &QPushButton::clicked, this,
          &MainWindow::chooseGameRoot);
  connect(importIsoButton_, &QPushButton::clicked, this, &MainWindow::importIso);
  connect(logsButton, &QPushButton::clicked, this, &MainWindow::openLogs);
  actions->addStretch();
  actions->addWidget(dataRootButton);
  actions->addWidget(gameRootButton);
  actions->addWidget(importIsoButton_);
  actions->addWidget(logsButton);
  actions->addStretch();
  layout->addLayout(actions);

  auto* version =
#if defined(__aarch64__)
      new QLabel(tr("Launcher ARM64 %1").arg(QStringLiteral(SPLOSION_LAUNCHER_VERSION)));
#elif defined(__x86_64__)
      new QLabel(tr("Launcher x86_64 Linux %1").arg(QStringLiteral(SPLOSION_LAUNCHER_VERSION)));
#else
      new QLabel(tr("Launcher %1").arg(QStringLiteral(SPLOSION_LAUNCHER_VERSION)));
#endif
  version->setAlignment(Qt::AlignRight);
  layout->addWidget(version);
  return page;
}

QWidget* MainWindow::createGraphicsPage() {
  auto* page = new QWidget;
  auto* outer = new QVBoxLayout(page);
  auto* form = new QFormLayout;

  resolutionScale_ =
      valueCombo({{tr("720p (1×)"), 1}, {tr("1440p (2×)"), 2},
                  {tr("2160p (3×)"), 3}});
  antiAliasing_ =
      valueCombo({{tr("Off"), QStringLiteral("none")},
                  {QStringLiteral("FXAA"), QStringLiteral("fxaa")},
                  {tr("FXAA Extreme"), QStringLiteral("fxaa_extreme")}});
  upscaling_ =
      valueCombo({{tr("Bilinear (default)"), QStringLiteral("bilinear")},
                  {QStringLiteral("AMD FSR"), QStringLiteral("fsr")}});
  anisotropic_ =
      valueCombo({{tr("Default"), -1}, {QStringLiteral("2×"), 2},
                  {QStringLiteral("4×"), 4}, {QStringLiteral("8×"), 8},
                  {QStringLiteral("16×"), 16}});
  pipelineThreads_ = new QSpinBox;
  pipelineThreads_->setRange(-1, 16);
  pipelineThreads_->setSpecialValueText(tr("Automatic"));

  form->addRow(tr("Resolution scale:"), resolutionScale_);
  form->addRow(tr("Anti-aliasing:"), antiAliasing_);
  form->addRow(tr("Upscaling:"), upscaling_);
  form->addRow(tr("Anisotropic filtering:"), anisotropic_);
  form->addRow(tr("Vulkan pipeline threads:"), pipelineThreads_);
  outer->addWidget(group(tr("Graphics quality"), form));

  auto* flags = new QVBoxLayout;
  fullscreen_ = new QCheckBox(tr("Fullscreen"));
  vsync_ = new QCheckBox(tr("Vertical sync"));
  nativeMsaa_ = new QCheckBox(tr("Native 2× MSAA"));
  asyncShaders_ = new QCheckBox(tr("Async shader compilation"));
  flags->addWidget(fullscreen_);
  flags->addWidget(vsync_);
  flags->addWidget(nativeMsaa_);
  flags->addWidget(asyncShaders_);
  auto* frameLimitRow = new QHBoxLayout;
  frameLimitRow->addWidget(new QLabel(tr("Frame rate limit:")));
  frameLimit_ = new QSpinBox;
  frameLimit_->setRange(0, 240);
  frameLimit_->setSpecialValueText(tr("Unlimited"));
  frameLimit_->setSuffix(tr(" FPS"));
  frameLimitRow->addWidget(frameLimit_);
  frameLimitRow->addStretch();
  flags->addLayout(frameLimitRow);
  outer->addWidget(group(tr("Presentation and performance"), flags));

  auto* note = new QLabel(tr(
#if defined(Q_PROCESSOR_ARM_64)
      "The Vulkan FBO backend and disabled sparse memory are applied "
      "automatically for ARM64/Turnip."
#else
      "Running on x86_64: default Vulkan render target path and sparse "
      "shared memory are used. ARM64/Turnip builds force the FBO backend "
      "and disable sparse memory automatically."
#endif
      ));
  note->setWordWrap(true);
  outer->addWidget(note);
  outer->addStretch();

  auto* buttons = new QHBoxLayout;
  auto* reset = new QPushButton(tr("Recommended values"));
  auto* save = new QPushButton(tr("Save"));
  connect(reset, &QPushButton::clicked, this, &MainWindow::resetSettings);
  connect(save, &QPushButton::clicked, this, &MainWindow::saveSettings);
  buttons->addStretch();
  buttons->addWidget(reset);
  buttons->addWidget(save);
  outer->addLayout(buttons);
  return page;
}

QWidget* MainWindow::createAudioPage() {
  auto* page = new QWidget;
  auto* outer = new QVBoxLayout(page);

  auto* flags = new QVBoxLayout;
  forceStereo_ = new QCheckBox(tr("Force stereo output"));
  xmaWorker_ = new QCheckBox(tr("Use dedicated XMA worker"));
  frontOnly_ = new QCheckBox(tr("Use front channels only"));
  flags->addWidget(forceStereo_);
  flags->addWidget(xmaWorker_);
  flags->addWidget(frontOnly_);
  outer->addWidget(group(tr("Processing"), flags));

  auto* form = new QFormLayout;
  queuedFrames_ = new QSpinBox;
  queuedFrames_->setRange(16, 128);
  highpassHz_ = new QSpinBox;
  highpassHz_->setRange(0, 500);
  highpassHz_->setSuffix(QStringLiteral(" Hz"));
  outputGain_ = new QSlider(Qt::Horizontal);
  outputGain_->setRange(10, 100);
  outputGainLabel_ = new QLabel;
  auto* gainRow = new QHBoxLayout;
  gainRow->addWidget(outputGain_);
  gainRow->addWidget(outputGainLabel_);
  connect(outputGain_, &QSlider::valueChanged, this, [this](int value) {
    outputGainLabel_->setText(QStringLiteral("%1%").arg(value));
  });
  form->addRow(tr("Queued frames:"), queuedFrames_);
  form->addRow(tr("Output gain:"), gainRow);
  form->addRow(tr("High-pass filter:"), highpassHz_);
  outer->addWidget(group(tr("Output"), form));

  auto* note = new QLabel(tr(
      "Disabling the XMA worker serializes decoding. That is the "
      "recommended value for this ARM64 build."));
  note->setWordWrap(true);
  outer->addWidget(note);
  outer->addStretch();

  auto* save = new QPushButton(tr("Save"));
  connect(save, &QPushButton::clicked, this, &MainWindow::saveSettings);
  outer->addWidget(save, 0, Qt::AlignRight);
  return page;
}

QWidget* MainWindow::createLocalePage() {
  auto* page = new QWidget;
  auto* outer = new QVBoxLayout(page);
  auto* form = new QFormLayout;

  language_ = new QComboBox;
  const struct {
    const char* label;
    int language;
    int country;
  } locales[] = {
      {"English", 1, 103}, {"日本語", 2, 53},   {"Deutsch", 3, 24},
      {"Français", 4, 34}, {"Español", 5, 31}, {"Italiano", 6, 50},
      {"한국어", 7, 56},   {"中文", 8, 20},     {"Português", 9, 84},
      {"Polski", 11, 82},  {"Русский", 12, 88},
  };
  for (const auto& locale : locales) {
    language_->addItem(QString::fromUtf8(locale.label), locale.language);
    language_->setItemData(language_->count() - 1, locale.country,
                           Qt::UserRole + 1);
  }
  country_ = new QSpinBox;
  country_->setRange(0, 255);
  logging_ = new QCheckBox(tr("Enable diagnostic logs"));
  connect(language_, &QComboBox::currentIndexChanged, this, [this](int index) {
    country_->setValue(language_->itemData(index, Qt::UserRole + 1).toInt());
  });

  form->addRow(tr("Language (Language ID):"), language_);
  form->addRow(tr("Country (Country ID):"), country_);
  form->addRow(QString(), logging_);
  outer->addWidget(group(tr("Xbox 360 system"), form));

  auto* note = new QLabel(tr(
      "The game must contain files for the selected language. Spanish uses "
      "Language ID 5 and Country ID 31."));
  note->setWordWrap(true);
  outer->addWidget(note);
  outer->addStretch();

  auto* save = new QPushButton(tr("Save"));
  connect(save, &QPushButton::clicked, this, &MainWindow::saveSettings);
  outer->addWidget(save, 0, Qt::AlignRight);
  return page;
}

QWidget* MainWindow::createSettingsPage() { return new QWidget; }

bool MainWindow::setDataRoot(const QString& root) {
  dataRoot_ = QDir::cleanPath(QDir(root).absolutePath());
  if (dataRoot_.isEmpty()) {
    return false;
  }

  const QString configDir = QDir(dataRoot_).filePath(QStringLiteral("config"));
  const QString savesDir = QDir(dataRoot_).filePath(QStringLiteral("saves"));
  const QString cacheDir = QDir(dataRoot_).filePath(QStringLiteral("cache"));
  const QString logsDir = QDir(dataRoot_).filePath(QStringLiteral("logs"));

  if (!QDir().mkpath(configDir) || !QDir().mkpath(savesDir) ||
      !QDir().mkpath(cacheDir) || !QDir().mkpath(logsDir)) {
    QMessageBox::critical(this, tr("Path is not writable"),
                          tr("Could not create folders inside: %1")
                              .arg(dataRoot_));
    dataRoot_.clear();
    return false;
  }

  GameConfig::setSelectedDataRoot(dataRoot_);
  loadSettings();
  updateStatus();
  return true;
}

void MainWindow::setGameRoot(const QString& root) {
  gameRoot_ = QDir::cleanPath(QDir(root).absolutePath());
  GameConfig::setSelectedGameRoot(gameRoot_);
  updateStatus();
}

bool MainWindow::runFirstSetup() {
  QMessageBox::information(
      this, tr("First launch"),
      tr("Select the save data folder first, then the game folder. "
         "The AppImage will remember both paths."));
  const QString selectedDataRoot = QFileDialog::getExistingDirectory(
      this, tr("Select save data folder"),
      dataRoot_.isEmpty() ? QDir::homePath() : dataRoot_);
  if (selectedDataRoot.isEmpty()) {
    return false;
  }
  if (!setDataRoot(selectedDataRoot)) {
    return false;
  }

  const QString selectedGameRoot = QFileDialog::getExistingDirectory(
      this, tr("Select game folder"),
      gameRoot_.isEmpty() ? QDir::homePath() : gameRoot_);
  if (selectedGameRoot.isEmpty()) {
    return false;
  }
  setGameRoot(selectedGameRoot);

  if (!ImportService::hasValidGame(gameRoot_)) {
    QMessageBox::information(
        this, tr("Game not imported"),
        tr("The game folder does not contain default.xex yet. Use "
           "\"Import ISO\" or select a folder that already has it."));
  }
  return true;
}

void MainWindow::chooseDataRoot() {
  const QString selected = QFileDialog::getExistingDirectory(
      this, tr("Select save data folder"),
      dataRoot_.isEmpty() ? QDir::homePath() : dataRoot_);
  if (!selected.isEmpty()) {
    setDataRoot(selected);
  }
}

void MainWindow::chooseGameRoot() {
  const QString selected = QFileDialog::getExistingDirectory(
      this, tr("Select game folder"),
      gameRoot_.isEmpty() ? QDir::homePath() : gameRoot_);
  if (!selected.isEmpty()) {
    setGameRoot(selected);
  }
}

bool MainWindow::confirmReplaceGame() {
  if (!ImportService::hasValidGame(gameRoot_)) {
    return true;
  }
  return QMessageBox::question(
             this, tr("Replace files"),
             tr("This folder already contains a valid game. Do you want "
                "to replace it?"),
             QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
}

bool MainWindow::performImport(const QString& isoPath) {
  if (!confirmReplaceGame()) {
    return false;
  }

  QProgressDialog progress(tr("Preparing import…"), tr("Cancel"), 0,
                           100, this);
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);
  ProgressCallback callback =
      [&progress](qint64 completed, qint64 total, const QString& detail) {
        if (total > 0) {
          progress.setRange(0, 1000);
          progress.setValue(
              static_cast<int>((completed * 1000) / qMax<qint64>(1, total)));
        } else {
          progress.setRange(0, 0);
        }
        if (!detail.isEmpty()) {
          progress.setLabelText(detail);
        }
        QApplication::processEvents();
        return !progress.wasCanceled();
      };

  const ImportResult result = ImportService::extractIso(
      isoPath, gameRoot_, ImportService::findExtractor(), callback);
  progress.close();
  if (!result.success && !result.cancelled) {
    QMessageBox::critical(this, tr("Import error"), result.error);
  } else if (result.success) {
    QMessageBox::information(this, tr("Import complete"),
                             tr("Game files are ready."));
  }
  updateStatus();
  return result.success;
}

void MainWindow::importIso() {
  if (dataRoot_.isEmpty() || gameRoot_.isEmpty()) {
    runFirstSetup();
    if (dataRoot_.isEmpty() || gameRoot_.isEmpty()) {
      return;
    }
  }
  const QString iso = QFileDialog::getOpenFileName(
      this, tr("Select ISO"), QDir::homePath(),
      tr("Xbox 360 ISO (*.iso);;All files (*)"));
  if (iso.isEmpty()) {
    return;
  }
  performImport(iso);
}

void MainWindow::loadSettings() {
  if (dataRoot_.isEmpty()) {
    applySettings(GameSettings{});
    return;
  }
  applySettings(GameConfig::load(dataRoot_));
}

void MainWindow::applySettings(const GameSettings& value) {
  selectValue(resolutionScale_, value.resolutionScale);
  selectValue(antiAliasing_, value.antiAliasing);
  selectValue(upscaling_, value.upscaling);
  selectValue(anisotropic_, value.anisotropic);
  fullscreen_->setChecked(value.fullscreen);
  vsync_->setChecked(value.vsync);
  frameLimit_->setValue(value.frameLimit);
  nativeMsaa_->setChecked(value.native2xMsaa);
  asyncShaders_->setChecked(value.asyncShaders);
  pipelineThreads_->setValue(value.pipelineThreads);

  forceStereo_->setChecked(value.forceStereo);
  queuedFrames_->setValue(value.audioQueuedFrames);
  xmaWorker_->setChecked(value.xmaWorker);
  frontOnly_->setChecked(value.frontOnly);
  outputGain_->setValue(qRound(value.outputGain * 100.0));
  highpassHz_->setValue(value.highpassHz);

  selectValue(language_, value.languageId);
  country_->setValue(value.countryId);
  logging_->setChecked(value.logging);
}

GameSettings MainWindow::collectSettings() const {
  GameSettings value;
  value.resolutionScale = resolutionScale_->currentData().toInt();
  value.antiAliasing = antiAliasing_->currentData().toString();
  value.upscaling = upscaling_->currentData().toString();
  value.anisotropic = anisotropic_->currentData().toInt();
  value.fullscreen = fullscreen_->isChecked();
  value.vsync = vsync_->isChecked();
  value.frameLimit = frameLimit_->value();
  value.native2xMsaa = nativeMsaa_->isChecked();
  value.asyncShaders = asyncShaders_->isChecked();
  value.pipelineThreads = pipelineThreads_->value();
  value.forceStereo = forceStereo_->isChecked();
  value.audioQueuedFrames = queuedFrames_->value();
  value.xmaWorker = xmaWorker_->isChecked();
  value.frontOnly = frontOnly_->isChecked();
  value.outputGain = outputGain_->value() / 100.0;
  value.highpassHz = highpassHz_->value();
  value.languageId = language_->currentData().toInt();
  value.countryId = country_->value();
  value.logging = logging_->isChecked();
  return value;
}

void MainWindow::saveSettings() {
  if (dataRoot_.isEmpty()) {
    QMessageBox::warning(this, tr("No folder selected"),
                         tr("Select the save data folder first."));
    return;
  }
  QString error;
  if (!GameConfig::save(dataRoot_, collectSettings(), &error)) {
    QMessageBox::critical(this, tr("Error"), error);
    return;
  }
  QMessageBox::information(this, tr("Settings"),
                           tr("Settings saved."));
}

void MainWindow::resetSettings() { applySettings(GameSettings{}); }

QString MainWindow::detectGameRoot() const {
  QStringList candidates;
  // Real install locations first: a Games folder at home or on a mounted
  // drive outranks transient spots (the AppImage's own folder, the CWD),
  // which can contain stray files that merely look like game data.
  const QString home = QDir::homePath();
  candidates << QDir(home).filePath(QStringLiteral("Games/splosionman"))
             << QDir(home).filePath(QStringLiteral("Games/SplosionMan"))
             << QDir(home).filePath(QStringLiteral("games/splosionman"))
             << QDir(home).filePath(QStringLiteral("splosionman"));
  // External/second drives are commonly mounted under /mnt/<drive> or
  // /media/<user>/<drive>; check for a Games folder on each, one level deep.
  QStringList mountRoots{QStringLiteral("/mnt")};
  mountRoots << QDir(QStringLiteral("/media")).filePath(QDir::home().dirName());
  for (const QString& mountRoot : mountRoots) {
    const QDir mounts(mountRoot);
    for (const QString& entry :
         mounts.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      const QDir drive = mounts.filePath(entry);
      candidates << drive.filePath(QStringLiteral("Games/splosionman"))
                   << drive.filePath(QStringLiteral("Games/SplosionMan"))
                   << drive.filePath(QStringLiteral("splosionman"));
    }
  }
  const QDir app(QCoreApplication::applicationDirPath());
  // The game folder may sit next to the launcher/AppImage, in a subfolder,
  // or a few levels up (e.g. <games>/SplosionMan/AppImage + extracted files).
  candidates << app.absolutePath() << app.filePath(QStringLiteral("splosionman"))
             << app.filePath(QStringLiteral("game"))
             << app.absoluteFilePath(QStringLiteral(".."))
             << app.absoluteFilePath(QStringLiteral("../.."))
             << app.absoluteFilePath(QStringLiteral("../../splosionman"));
  // Inside an AppImage, $APPIMAGE points at the bundle the user launched;
  // its folder (and a "splosionman" subfolder) are natural install spots.
  const QString appImage = qEnvironmentVariable("APPIMAGE");
  if (!appImage.isEmpty()) {
    const QDir bundleDir = QFileInfo(appImage).dir();
    candidates << bundleDir.absolutePath()
               << bundleDir.filePath(QStringLiteral("splosionman"));
  }
  candidates << QDir::currentPath()
             << QDir::current().filePath(QStringLiteral("splosionman"));
  const QString tempRoot = QDir::cleanPath(QDir::tempPath());
  for (const QString& candidate : candidates) {
    const QString clean = QDir::cleanPath(QDir(candidate).absolutePath());
    // Never treat the system temp folder (or anything inside it) as a game
    // install: stray test files there are not a real game folder.
    if (clean == tempRoot || clean.startsWith(tempRoot + QLatin1Char('/'))) {
      continue;
    }
    if (ImportService::hasValidGame(clean)) {
      return clean;
    }
  }
  return {};
}

QString MainWindow::gameExecutable() const {
  const QDir app(QCoreApplication::applicationDirPath());
  const QStringList candidates{
      app.filePath(QStringLiteral("splosionman")),
      app.filePath(QStringLiteral("../bin/splosionman")),
      app.filePath(QStringLiteral("../splosionman")),
  };
  for (const QString& candidate : candidates) {
    const QFileInfo file(candidate);
    if (file.isFile() && file.isExecutable()) {
      return file.absoluteFilePath();
    }
  }
  return {};
}

void MainWindow::play() {
  if (dataRoot_.isEmpty() || gameRoot_.isEmpty()) {
    QMessageBox::warning(this, tr("Incomplete setup"),
                         tr("Select the save data folder and the game "
                            "folder."));
    return;
  }
  if (!ImportService::hasValidGame(gameRoot_)) {
    QMessageBox::warning(this, tr("Game not found"),
                         tr("Import the ISO first, or select a folder that "
                            "already contains default.xex."));
    return;
  }
  QString error;
  const GameSettings settings = collectSettings();
  if (!GameConfig::save(dataRoot_, settings, &error)) {
    QMessageBox::critical(this, tr("Error"), error);
    return;
  }

  const QString executable = gameExecutable();
  if (executable.isEmpty()) {
    QMessageBox::critical(this, tr("Executable not found"),
                          tr("splosionman was not found next to the launcher."));
    return;
  }

  const QString logsDir = QDir(dataRoot_).filePath(QStringLiteral("logs"));
  QDir().mkpath(logsDir);

  // Launch the game as a managed child process (not detached) so its
  // console output is captured to the logs folder. A silent instant-death
  // is the most common "nothing happens" failure mode.
  if (gameProcess_ != nullptr) {
    gameProcess_->deleteLater();
    gameProcess_ = nullptr;
  }
  gameProcess_ = new QProcess(this);
  gameProcess_->setProgram(executable);
  gameProcess_->setArguments(GameConfig::commandLine(dataRoot_, gameRoot_, settings));
  gameProcess_->setWorkingDirectory(dataRoot_);
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  const QDir app(QCoreApplication::applicationDirPath());
  QStringList libraryPaths{app.absolutePath(),
                           app.absoluteFilePath(QStringLiteral("../lib"))};
  if (environment.contains(QStringLiteral("LD_LIBRARY_PATH"))) {
    libraryPaths << environment.value(QStringLiteral("LD_LIBRARY_PATH"));
  }
  environment.insert(QStringLiteral("LD_LIBRARY_PATH"),
                     libraryPaths.join(QLatin1Char(':')));
  environment.insert(QStringLiteral("DISABLE_LSFG"), QStringLiteral("1"));
  environment.insert(QStringLiteral("XDG_CACHE_HOME"),
                     QDir(dataRoot_).filePath(QStringLiteral("cache")));
  gameProcess_->setProcessEnvironment(environment);
  const QString stdoutLog =
      QDir(logsDir).filePath(QStringLiteral("game-stdout.log"));
  const QString stderrLog =
      QDir(logsDir).filePath(QStringLiteral("game-stderr.log"));
  gameProcess_->setStandardOutputFile(stdoutLog, QIODevice::Truncate);
  gameProcess_->setStandardErrorFile(stderrLog, QIODevice::Truncate);

  // Record the exact command line for diagnostics.
  {
    QFile cmdLog(QDir(logsDir).filePath(QStringLiteral("last-command.txt")));
    if (cmdLog.open(QIODevice::WriteOnly | QIODevice::Text)) {
      QTextStream out(&cmdLog);
      out << executable;
      for (const QString& arg : gameProcess_->arguments()) {
        out << ' ' << arg;
      }
      out << '\n';
    }
  }

  connect(gameProcess_,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          this, &MainWindow::onGameFinished);
  connect(gameProcess_, &QProcess::errorOccurred,
          this, &MainWindow::onGameError);

  statusLabel_->setText(tr("Starting game…"));
  gameStartMsecs_ = QDateTime::currentMSecsSinceEpoch();
  gameProcess_->start();
  if (!gameProcess_->waitForStarted(10000)) {
    QMessageBox::critical(
        this, tr("Could not start"),
        tr("The game did not start:\n%1\n\nSee %2")
            .arg(gameProcess_->errorString(), stderrLog));
    gameProcess_->deleteLater();
    gameProcess_ = nullptr;
    updateStatus();
  }
}

void MainWindow::onGameFinished(int exitCode, QProcess::ExitStatus exitStatus) {
  const qint64 lifetime = QDateTime::currentMSecsSinceEpoch() - gameStartMsecs_;
  updateStatus();
  // A game that dies within ~15s almost certainly failed to initialize.
  // Surface that instead of silently returning to the launcher.
  if (lifetime < 15000 && gameProcess_ != nullptr) {
    const QString logsDir = QDir(dataRoot_).filePath(QStringLiteral("logs"));
    const QString stderrLog =
        QDir(logsDir).filePath(QStringLiteral("game-stderr.log"));
    QString detail;
    QFile logFile(stderrLog);
    if (logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
      const QStringList lines =
          QString::fromUtf8(logFile.readAll()).split('\n');
      // Show the last few non-empty lines.
      QStringList tail;
      for (int i = lines.size() - 1; i >= 0 && tail.size() < 8; --i) {
        if (!lines[i].trimmed().isEmpty()) tail.prepend(lines[i].trimmed());
      }
      detail = tail.join('\n');
    }
    QMessageBox::warning(
        this, tr("The game exited unexpectedly"),
        tr("The game closed after %1 ms (exit code %2, %3).\n\n%4\n\nFull output:\n%5")
            .arg(lifetime)
            .arg(exitCode)
            .arg(exitStatus == QProcess::CrashExit ? tr("crashed")
                                                   : tr("exited normally"))
            .arg(detail.isEmpty() ? tr("(no output captured)") : detail)
            .arg(stderrLog));
  }
  if (gameProcess_ != nullptr) {
    gameProcess_->deleteLater();
    gameProcess_ = nullptr;
  }
}

void MainWindow::onGameError(QProcess::ProcessError error) {
  if (error == QProcess::FailedToStart && gameProcess_ != nullptr) {
    QMessageBox::critical(
        this, tr("Could not start"),
        tr("Failed to launch the game:\n%1").arg(gameProcess_->errorString()));
  }
}

void MainWindow::openLogs() {
  if (dataRoot_.isEmpty()) {
    return;
  }
  const QString path = QDir(dataRoot_).filePath(QStringLiteral("logs"));
  QDir().mkpath(path);
  // System tools (xdg-open -> kde-open on KDE) must resolve the *system*
  // Qt, not the one bundled in the AppImage. Launch with LD_LIBRARY_PATH
  // stripped so the loader falls back to the default system paths.
  QProcess opener;
  opener.setProgram(QStringLiteral("xdg-open"));
  opener.setArguments({QDir::toNativeSeparators(path)});
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  environment.remove(QStringLiteral("LD_LIBRARY_PATH"));
  opener.setProcessEnvironment(environment);
  if (!opener.startDetached()) {
    QMessageBox::warning(
        this, tr("Could not open logs"),
        tr("Could not launch the file manager:\n%1").arg(opener.errorString()));
  }
}

void MainWindow::updateStatus() {
  const bool valid = ImportService::hasValidGame(gameRoot_);
  playButton_->setEnabled(valid);
  statusLabel_->setText(
      valid ? tr("Game files detected. Ready to play.")
            : tr("default.xex was not found in the game folder."));
  dataRootLabel_->setText(
      dataRoot_.isEmpty()
          ? tr("No save data folder selected.")
          : tr("Save data: %1").arg(dataRoot_));
  gameRootLabel_->setText(
      gameRoot_.isEmpty()
          ? tr("No game folder selected.")
          : tr("Game: %1").arg(gameRoot_));
  importIsoButton_->setEnabled(!gameRoot_.isEmpty());
}

}  
