#include "ui/main_window.hpp"
#include "update/cleanup.hpp"
#include <QCoreApplication>
#include <QDir>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
  if (argc == 4 && std::string_view(argv[1]) == "--cleanup-update-backup") {
    QCoreApplication app(argc, argv);
    QDir root(QCoreApplication::applicationDirPath());
#ifdef Q_OS_MACOS
    root.cdUp(); root.cdUp();
#elif !defined(Q_OS_WIN)
    root.cdUp();
#endif
    const auto args = app.arguments();
    return prism_viewer::update::cleanupPreviousPackage(root.absolutePath(), args[2], args[3]);
  }
  // No display, device connection or event loop is needed to inspect an artifact.
  if (argc == 2 && std::string_view(argv[1]) == "--build-info") {
    std::cout << "{\"version\":\"" PRISM_VIEWER_VERSION
                 "\",\"revision\":\"" PRISM_VIEWER_REVISION
                 "\",\"build_epoch\":" << PRISM_VIEWER_BUILD_EPOCH << "}\n";
    return 0;
  }
  return prism_viewer::runViewerApplication(argc, argv);
}
