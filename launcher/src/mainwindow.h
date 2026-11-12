// mainwindow.h - the launcher window.
#pragma once

#include "options.h"

#include <QHash>
#include <QMainWindow>
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;

namespace theme {
class Manager;
}

// The banner across the top: the game's own art once known, else the
// placeholder; scaled to fill the width.
class Banner : public QWidget {
public:
    explicit Banner(QWidget *parent = nullptr);
    void setDark(bool dark);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool dark_ = false;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(opts::Settings &settings, theme::Manager &theme);

private:
    QWidget *buildPlayPage();
    QWidget *buildSettingsPage();
    void refreshGame();
    void refreshVisibility();
    void loadChoicesIntoControls();
    void setGameFolder(const QString &folder);

    void onPlay();
    void onImport();
    void onChooseFolder();
    void onOpenLogs();
    void onChooseSaveFolder();

    opts::Settings &settings_;
    theme::Manager &theme_;
    Banner *banner_ = nullptr;
    QLabel *status_ = nullptr;
    QLabel *statusDetail_ = nullptr;
    QPushButton *play_ = nullptr;
    QPushButton *openLogs_ = nullptr;
    QLabel *saveFolder_ = nullptr;
    QComboBox *appearance_ = nullptr;
    QHash<QString, QComboBox *> combos_;
    QHash<QString, QList<QWidget *>> rows_;
};
