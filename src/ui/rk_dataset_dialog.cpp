#include "ui/rk_dataset_dialog.hpp"
#include "common/ui_text.hpp"
#include "dataset/rk_dataset_client.hpp"
#include "dataset/rosbag_exporter.hpp"
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonObject>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QThread>
#include <QtGui/QCloseEvent>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QVBoxLayout>
#include <atomic>
#include <algorithm>
#include <memory>

namespace prism_viewer::ui {
using common::uiText;
namespace {
struct Result {
  bool listing = false, cancelled = false;
  QJsonArray rows;
  QString path, bag, error;
};
class Dialog final : public QDialog {
  QLineEdit *address_, *folder_;
  QPushButton *refresh_, *browse_, *download_, *cancel_, *open_;
  QTableWidget* table_;
  QComboBox* format_;
  QLabel* status_;
  QProgressBar* progress_;
  QThread* thread_ = nullptr;
  std::atomic<bool> cancelled_{false};
  QJsonArray rows_;
  QString downloaded_;
  std::function<void(const QString&)> open_dataset_;
  bool busy() const { return thread_ != nullptr; }
  void controls() {
    address_->setEnabled(!busy()); folder_->setEnabled(!busy()); refresh_->setEnabled(!busy());
    browse_->setEnabled(!busy()); table_->setEnabled(!busy()); format_->setEnabled(!busy());
    download_->setEnabled(!busy() && table_->currentRow() >= 0);
    open_->setEnabled(!busy() && QFileInfo::exists(QDir(downloaded_).filePath(QStringLiteral("dataset.info"))) && !downloaded_.isEmpty());
    cancel_->setText(busy() ? uiText("Cancel operation", "取消操作") : uiText("Close", "关闭"));
  }
  void report(quint64 done, quint64 total, const QString& stage) {
    QMetaObject::invokeMethod(this, [this,done,total,stage] {
      progress_->setRange(0, total ? 1000 : 0);
      progress_->setValue(total ? static_cast<int>(std::min<quint64>(1000, done*1000/total)) : 0);
      status_->setText(stage);
    }, Qt::QueuedConnection);
  }
  void launch(std::function<void(Result&)> operation) {
    auto result = std::make_shared<Result>();
    cancelled_ = false; progress_->setRange(0,0);
    thread_ = QThread::create([operation=std::move(operation),result] {
      try { operation(*result); }
      catch (const dataset::RkDownloadCancelled&) { result->cancelled=true; }
      catch (const std::exception& error) { result->error=QString::fromUtf8(error.what()); }
    });
    connect(thread_, &QThread::finished, this, [this,result] {
      auto* finished = thread_; thread_=nullptr; finished->deleteLater();
      progress_->setRange(0,1000);
      if (!result->path.isEmpty()) downloaded_=result->path;
      if (result->cancelled) {
        status_->setText(uiText("Cancelled. RK files were not changed. Any unfinished download remains in a .partial directory.",
                               "已取消，RK 文件未改动。未完成下载保留在 .partial 目录中。"));
      } else if (!result->error.isEmpty()) {
        status_->setText(uiText("Operation failed: ", "操作失败：") + result->error +
                         (result->path.isEmpty() ? QString() : uiText("\nRaw dataset saved: ", "\n原始数据已保存：")+result->path));
      } else if (result->listing) {
        rows_=result->rows; table_->setRowCount(rows_.size());
        for (int row=0; row<rows_.size(); ++row) {
          auto entry=rows_.at(row).toObject(); auto m=entry.value(QStringLiteral("manifest")).toObject();
          const bool complete=m.value(QStringLiteral("complete")).toBool();
          QStringList text{entry.value(QStringLiteral("name")).toString(),
            complete ? uiText("Complete", "完整") : uiText("Incomplete", "未完成"),
            m.value(QStringLiteral("viewer_format")).toString().isEmpty() ? uiText("Legacy raw", "旧版原始格式") : QStringLiteral("Prism v6"),
            QString::number(m.value(QStringLiteral("frame_sets")).toDouble(), 'f', 0),
            QString::number(m.value(QStringLiteral("lidar_points")).toDouble(), 'f', 0)};
          for (int col=0; col<text.size(); ++col) table_->setItem(row,col,new QTableWidgetItem(text[col]));
        }
        if (!rows_.isEmpty()) table_->selectRow(0);
        status_->setText(uiText("Found %1 recordings. Stop RK capture before downloading.",
                               "找到 %1 个数据集。请先停止 RK 采集，再下载。").arg(rows_.size()));
      } else {
        progress_->setValue(1000);
        status_->setText(uiText("Raw dataset saved: ", "原始数据集：")+result->path+
          (result->bag.isEmpty() ? QString() : uiText("\nROS bag saved: ", "\nROS bag：")+result->bag)+
          uiText("\nRK originals were not deleted.", "\nRK 上的原文件未删除。"));
      }
      controls();
    });
    controls(); thread_->start();
  }
  void refresh() {
    try {
      const auto base=dataset::rkDatasetEndpoint(address_->text());
      QSettings(QStringLiteral("DIBULI"),QStringLiteral("PrismViewer")).setValue(QStringLiteral("rkDataset/address"),base.toString());
      status_->setText(uiText("Reading RK recordings...", "正在读取 RK 数据集列表……"));
      launch([this,base](Result& r) { r.listing=true; r.rows=dataset::listRkDatasets(base,[this]{return cancelled_.load();}); });
    } catch (const std::exception& error) { status_->setText(QString::fromUtf8(error.what())); }
  }
  void download() {
    const int row=table_->currentRow();
    if (row < 0 || row >= rows_.size()) return;
    try {
      const auto base=dataset::rkDatasetEndpoint(address_->text());
      const auto entry=rows_.at(row).toObject(), manifest=entry.value(QStringLiteral("manifest")).toObject();
      const QString name=entry.value(QStringLiteral("name")).toString(), parent=folder_->text();
      const int format=format_->currentIndex();
      if (format && (!manifest.value(QStringLiteral("complete")).toBool() ||
                     manifest.value(QStringLiteral("viewer_format")).toString() != QStringLiteral("prism-dataset-v6"))) {
        status_->setText(uiText("ROS export requires a complete Prism v6 recording. Legacy/incomplete files can still be downloaded as raw data.",
                               "ROS 导出需要完整的 Prism v6 数据集。旧格式或未完成的数据仍可下载原始文件。")); return;
      }
      QSettings(QStringLiteral("DIBULI"),QStringLiteral("PrismViewer")).setValue(QStringLiteral("rkDataset/folder"),parent);
      status_->setText(uiText("Downloading original RK chunks...", "正在下载 RK 原始 CHUNK……"));
      launch([this,base,name,parent,format](Result& r) {
        QElapsedTimer throttle; throttle.start();
        r.path=dataset::downloadRkDataset(base,name,parent,[this,&throttle](quint64 n,quint64 total,const QString& file) {
          if (throttle.elapsed() < 100 && n != total) return;
          throttle.restart(); report(n,total,uiText("Downloading %1\n%2 / %3 MiB", "正在下载 %1\n%2 / %3 MiB")
            .arg(file).arg(n/1048576.0,0,'f',1).arg(total/1048576.0,0,'f',1));
        },[this]{return cancelled_.load();});
        if (format) {
          const auto type=format==1 ? dataset::RosbagFormat::Ros1 : dataset::RosbagFormat::Ros2;
          const auto output=r.path+(format==1 ? QStringLiteral(".bag") : QStringLiteral(".rosbag2"));
          const auto exported=dataset::exportDatasetToRosbag(common::toFilesystemPath(r.path),common::toFilesystemPath(output),type,false,
            [this,&throttle](const dataset::RosbagExportProgress& p) {
              if (throttle.elapsed() < 100 && p.completed_records != p.total_records) return;
              throttle.restart(); report(p.completed_records,p.total_records,uiText("Exporting ROS bag... ", "正在导出 ROS bag…… ")+QString::fromStdString(p.stage));
            },[this]{return cancelled_.load();});
          if (exported.cancelled) throw dataset::RkDownloadCancelled();
          if (!exported.success) throw std::runtime_error(exported.error);
          r.bag=output;
        }
      });
    } catch (const std::exception& error) { status_->setText(QString::fromUtf8(error.what())); }
  }
 public:
  Dialog(QWidget* parent, std::function<void(const QString&)> open_dataset)
      : QDialog(parent), open_dataset_(std::move(open_dataset)) {
    setObjectName(QStringLiteral("rkDatasetDialog"));
    setWindowTitle(uiText("Download RK recordings", "从 RK 导出数据集")); resize(940,560);
    auto* layout=new QVBoxLayout(this);
    auto* note=new QLabel(uiText("Download recordings from the RK local disk. Original chunks are preserved; ROS conversion runs on this computer. No USB capture connection is needed.",
      "下载 RK 本地磁盘上的录制数据，保留原始 CHUNK；ROS 转换在当前电脑完成，不需要连接 USB 采集。"),this);
    note->setWordWrap(true); layout->addWidget(note);
    auto* connection=new QHBoxLayout();
    connection->addWidget(new QLabel(uiText("RK address", "RK 地址"),this));
    QSettings settings(QStringLiteral("DIBULI"),QStringLiteral("PrismViewer"));
    address_=new QLineEdit(settings.value(QStringLiteral("rkDataset/address"),QStringLiteral("http://10.42.200.1:80")).toString(),this);
    address_->setObjectName(QStringLiteral("rkDatasetAddress")); connection->addWidget(address_,1);
    refresh_=new QPushButton(uiText("Refresh", "刷新列表"),this); refresh_->setObjectName(QStringLiteral("rkDatasetRefresh")); connection->addWidget(refresh_);
    layout->addLayout(connection);
    table_=new QTableWidget(0,5,this); table_->setObjectName(QStringLiteral("rkDatasetTable"));
    table_->setHorizontalHeaderLabels({uiText("Dataset", "数据集"),uiText("State", "状态"),uiText("Format", "格式"),uiText("Frame sets", "帧集"),uiText("LiDAR points", "雷达点数")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    for(int col=1;col<5;++col) table_->horizontalHeader()->setSectionResizeMode(col,QHeaderView::ResizeToContents);
    layout->addWidget(table_,1);
    auto* destination=new QHBoxLayout(); destination->addWidget(new QLabel(uiText("Save to", "保存到"),this));
    folder_=new QLineEdit(settings.value(QStringLiteral("rkDataset/folder"),QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).toString(),this);
    folder_->setObjectName(QStringLiteral("rkDatasetFolder")); destination->addWidget(folder_,1);
    browse_=new QPushButton(uiText("Browse...", "选择目录……"),this); destination->addWidget(browse_); layout->addLayout(destination);
    auto* actions=new QHBoxLayout(); format_=new QComboBox(this); format_->setObjectName(QStringLiteral("rkDatasetFormat"));
    format_->addItems({uiText("Original dataset", "原始数据集"),QStringLiteral("ROS1 (.bag)"),QStringLiteral("ROS2 (SQLite3)")}); actions->addWidget(format_);
    download_=new QPushButton(uiText("Download / export", "下载 / 导出"),this); download_->setObjectName(QStringLiteral("rkDatasetDownload")); actions->addWidget(download_);
    open_=new QPushButton(uiText("Open downloaded dataset", "打开已下载数据集"),this); actions->addWidget(open_); actions->addStretch();
    cancel_=new QPushButton(uiText("Close", "关闭"),this); actions->addWidget(cancel_); layout->addLayout(actions);
    progress_=new QProgressBar(this); progress_->setRange(0,1000); progress_->setValue(0); layout->addWidget(progress_);
    status_=new QLabel(uiText("Use RK Wi-Fi or Ethernet IP, port 80. This LAN service has no authentication.",
                              "填写 RK 的 Wi-Fi 或有线 IP，端口 80。此局域网服务没有访问认证。"),this);
    status_->setWordWrap(true); status_->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(status_);
    status_->setTextFormat(Qt::PlainText);
    connect(refresh_,&QPushButton::clicked,this,[this]{refresh();});
    connect(download_,&QPushButton::clicked,this,[this]{download();});
    connect(cancel_,&QPushButton::clicked,this,[this]{reject();});
    connect(table_,&QTableWidget::itemSelectionChanged,this,[this]{controls();});
    connect(browse_,&QPushButton::clicked,this,[this]{ const auto path=QFileDialog::getExistingDirectory(this,uiText("Select destination", "选择保存目录"),folder_->text()); if(!path.isEmpty()) folder_->setText(path); });
    connect(open_,&QPushButton::clicked,this,[this]{if(!downloaded_.isEmpty()){open_dataset_(downloaded_);accept();}});
    controls();
  }
  void reject() override { if(busy()){cancelled_=true; status_->setText(uiText("Cancelling...", "正在取消……"));} else QDialog::reject(); }
  void closeEvent(QCloseEvent* event) override { if(busy()){reject();event->ignore();}else QDialog::closeEvent(event); }
};
}  // namespace
void showRkDatasetDialog(QWidget* parent, const std::function<void(const QString&)>& open_dataset) {
  Dialog dialog(parent,open_dataset); dialog.exec();
}
}  // namespace prism_viewer::ui
