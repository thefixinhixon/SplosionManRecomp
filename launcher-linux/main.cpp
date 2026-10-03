#include "main_window.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char** argv) {
  QApplication application(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("SplosionManRecomp"));
  QCoreApplication::setApplicationName(QStringLiteral("SplosionManLauncher"));
  QCoreApplication::setApplicationVersion(
      QStringLiteral(SPLOSION_LAUNCHER_VERSION));

  splosion::launcher::MainWindow window;
  window.show();
  return application.exec();
}
