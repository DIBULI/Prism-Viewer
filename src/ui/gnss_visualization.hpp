#pragma once
#include "prism/usb/gnss_plot.hpp"
#include <QWidget>
#include <QElapsedTimer>
class QLabel;class QTableWidget;class QComboBox;class QLineEdit;
namespace prism_viewer::ui {
class GnssPlot;
class GnssVisualization final:public QWidget {
 public:
  explicit GnssVisualization(QWidget* parent=nullptr);
  void apply(const prism::GnssObservations&, bool redraw=true);
  void setPlaybackMode();
  void setPlaybackAge(uint64_t milliseconds);
  void unavailable(const QString&);
  void reset();
 private:
  void refresh();
  prism::gnss_plot::Model model_;
  QElapsedTimer age_;QString error_,table_fingerprint_;
  bool playback_=false;
  uint64_t playback_age_=0;
  QLabel *status_,*gnss_,*rtk_;
  QTableWidget* table_;QLineEdit* filter_;
  GnssPlot *sky_,*gnss_plot_,*rtk_plot_;
};
}
