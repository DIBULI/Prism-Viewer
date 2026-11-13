#include "ui/main_window.hpp"
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
  // No display, device connection or event loop is needed to inspect an artifact.
  if (argc == 2 && std::string_view(argv[1]) == "--build-info") {
    std::cout << "{\"version\":\"" PRISM_VIEWER_VERSION
                 "\",\"revision\":\"" PRISM_VIEWER_REVISION
                 "\",\"build_epoch\":" << PRISM_VIEWER_BUILD_EPOCH << "}\n";
    return 0;
  }
  return prism_viewer::runViewerApplication(argc, argv);
}
