#pragma once

class QApplication;

namespace prism_viewer::ui {

// Keep Prism Viewer's light appearance deterministic even when the operating
// system uses a dark color scheme.
void applyLightApplicationTheme(QApplication& application);

}  // namespace prism_viewer::ui
