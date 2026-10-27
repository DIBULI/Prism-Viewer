#include "gnss_visualization.hpp"
#include "trajectory_view.hpp"
#include "common/ui_text.hpp"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QLineEdit>
#include <QTimer>
#include <QSplitter>
#include <QSignalBlocker>
#include <functional>
using prism_viewer::common::uiText;
namespace prism_viewer::ui {
namespace {
constexpr double pi=3.14159265358979323846;
QColor color(const std::string& s){return s=="GPS"?QColor("#2475ce"):s=="BeiDou"?QColor("#d68112"):s=="Galileo"?QColor("#7c57c4"):s=="GLONASS"?QColor("#169677"):QColor("#65768b");}
QColor quality(int q){return q==4?QColor("#169677"):q==5?QColor("#d68112"):QColor("#2475ce");}
}
class GnssPlot:public QWidget {
 public:
  int kind;prism::gnss_plot::Model* model;uint64_t now=0;QString selected;
  bool top=false;double yaw=-25,pitch=35,zoom=1;QPoint previous;
  TrajectoryView view;
  std::vector<std::pair<QString,QPointF>> hits;
  std::function<void(QString)> onSelected;
  GnssPlot(int k,prism::gnss_plot::Model* m,QWidget* p):QWidget(p),kind(k),model(m){setMinimumSize(280,260);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);}
  QPointF project(double e,double n,double u)const {
    double y=yaw*pi/180,p=(top?90:pitch)*pi/180;
    double x=std::cos(y)*e-std::sin(y)*n,z=std::sin(y)*e+std::cos(y)*n;
    return {x,-(std::sin(p)*z+std::cos(p)*u)};
  }
  QRectF trackArea()const{return QRectF(rect()).adjusted(36,40,-36,-86);}
  void fitTrack(){view.reset();update();}
  void prepareView(){
    const auto& track=kind==1?model->gnss_track:model->rtk_track;
    double minx=1e100,maxx=-1e100,miny=1e100,maxy=-1e100;bool found=false;
    for(const auto& q:track){
      if(!top&&!q.height_valid)continue;
      const auto v=project(q.e,q.n,top?0:q.u);
      if(!std::isfinite(v.x())||!std::isfinite(v.y()))continue;
      found=true;minx=std::min(minx,v.x());maxx=std::max(maxx,v.x());miny=std::min(miny,v.y());maxy=std::max(maxy,v.y());
    }
    view.fit(found?QRectF(QPointF(minx,miny),QPointF(maxx,maxy)):QRectF(-.5,-.5,1,1),trackArea());
  }
  void zoomTrack(double factor,const QPointF& anchor){prepareView();view.zoom(factor,anchor,trackArea());update();}
  void drawRuler(QPainter& p){
    const double meters=view.rulerMeters(std::min(100.0,trackArea().width()*.45));
    const double length=meters*view.pixels_per_meter;
    const double x=36,y=height()-51;
    p.setPen(QPen(QColor("#334e68"),2));p.drawLine(QPointF(x,y),QPointF(x+length,y));
    for(double f:{0.,.5,1.})p.drawLine(QPointF(x+length*f,y-5),QPointF(x+length*f,y+5));
    p.setPen(QColor("#334e68"));
    p.drawText(QPointF(x-3,y+21),"0");
    const auto label=TrajectoryView::distance(meters);
    p.drawText(QRectF(x+length-45,y+7,90,20),Qt::AlignHCenter|Qt::AlignVCenter,label);
    const auto mode=view.automatic?uiText("Auto fit","自动适应"):uiText("Manual zoom","手动缩放");
    const auto note=top?uiText("Planar scale · not measurement accuracy","平面比例尺 · 非测量精度"):
        uiText("Projected scale · not 3D distance or accuracy","投影比例尺 · 非三维距离或精度");
    p.drawText(QRectF(12,height()-23,width()-24,20),Qt::AlignCenter,note);
    const auto& track=kind==1?model->gnss_track:model->rtk_track;
    p.drawText(QRectF(x+length+55,y-10,width()-x-length-67,32),Qt::AlignRight|Qt::AlignVCenter,
        uiText("%1 · %2 points","%1 · %2 点").arg(mode).arg(track.size()));
    setAccessibleDescription(uiText("Scale: %1; %2; %3","刻度：%1；%2；%3").arg(label,mode,note));
  }
  void paintEvent(QPaintEvent*)override {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),QColor("#f8fbff"));
    QRectF area=QRectF(rect()).adjusted(36,40,-36,-40);QPointF center=area.center();
    double scale=std::min(area.width(),area.height())*.43*zoom;hits.clear();
    if(kind==0){
      p.setPen(QPen(QColor("#cdd9e8"),1));
      for(int el:{0,30,60}){QPolygonF ring;for(int az=0;az<=360;az+=5){double a=az*pi/180,r=std::cos(el*pi/180);ring<<center+scale*project(r*std::sin(a),r*std::cos(a),std::sin(el*pi/180));}p.drawPolyline(ring);}
      for(int az:{0,90,180,270}){double a=az*pi/180;auto edge=center+scale*project(std::sin(a),std::cos(a),0);p.drawLine(center,edge);p.drawText(edge+QPointF(4,-4),az==0?"N":az==90?"E":az==180?"S":"W");}
      for(const auto& s:model->satellites(now)){if(!s.azimuth||!s.elevation)continue;double a=*s.azimuth*pi/180,e=*s.elevation*pi/180;
        auto v=center+scale*project(std::cos(e)*std::sin(a),std::cos(e)*std::cos(a),std::sin(e));auto id=QString::fromStdString(s.id);
        p.setBrush(color(s.system));p.setPen(QPen(selected==id?QColor("#152b44"):Qt::white,selected==id?3:1));p.drawEllipse(v,s.used?7:5,s.used?7:5);
        p.setPen(QColor("#27435c"));p.drawText(v+QPointF(9,-4),id);hits.emplace_back(id,v);}
      p.setPen(QColor("#51677e"));p.drawText(QRect(12,height()-30,width()-24,24),Qt::AlignCenter,uiText("Relative sky directions · no orbital distance","相对天空方向 · 非卫星实际轨道距离"));
    }else{
      const auto& track=kind==1?model->gnss_track:model->rtk_track;
      prepareView();area=trackArea();p.save();p.setClipRect(area);
      const double step=view.rulerMeters(80)*view.pixels_per_meter;
      const auto zero=view.screen(QPointF(),area);p.setPen(QPen(QColor("#dce5ef"),1));
      // Remainder keeps grid work bounded even after long pans at mm scale.
      for(double x=area.left()+std::fmod(zero.x()-area.left(),step);x<=area.right();x+=step)
        p.drawLine(QPointF(x,area.top()),QPointF(x,area.bottom()));
      for(double y=area.top()+std::fmod(zero.y()-area.top(),step);y<=area.bottom();y+=step)
        p.drawLine(QPointF(area.left(),y),QPointF(area.right(),y));
      QPointF prev;uint64_t segment=0;bool has=false;
      for(const auto& q:track){if(!top&&!q.height_valid){has=false;continue;}auto v=view.screen(project(q.e,q.n,top?0:q.u),area);
        p.setPen(QPen(quality(q.quality),1.8));if(has&&q.segment==segment)p.drawLine(prev,v);else p.drawEllipse(v,2,2);prev=v;segment=q.segment;has=true;}
      const auto& pos=kind==1?model->gnss:model->rtk;
      if(has){p.setBrush(pos.valid&&prism::gnss_plot::fresh(now,pos.ms,2000)?quality(pos.quality):QColor("#95a1b0"));p.drawEllipse(prev,5,5);}
      if(!has){p.setPen(QColor("#51677e"));p.drawText(area,Qt::AlignCenter,
          track.empty()?uiText("Waiting for valid positions","等待有效定位"):
          uiText("No height data; use Top view (2D)","无高程数据，请切换俯视图（2D）"));}
      p.restore();
      p.setPen(QColor("#51677e"));p.drawText(12,22,uiText("Local ENU · meters · green FIX / amber FLOAT / blue other","局部 ENU · 米 · 绿 FIX / 黄 FLOAT / 蓝其他"));
      drawRuler(p);
    }
  }
  void mousePressEvent(QMouseEvent* e)override{previous=e->pos();if(kind==0)for(const auto& [id,p]:hits)if(QLineF(p,e->pos()).length()<14){selected=id;if(onSelected)onSelected(id);update();break;}}
  void mouseMoveEvent(QMouseEvent* e)override{
    if(e->buttons()&(Qt::LeftButton|Qt::RightButton)){
      auto d=e->pos()-previous;
      if(kind!=0&&(top||(e->buttons()&Qt::RightButton)||(e->modifiers()&Qt::ShiftModifier))){prepareView();view.pan(d);}
      else if(e->buttons()&Qt::LeftButton){yaw+=d.x()*.4;pitch=std::clamp(pitch+d.y()*.4,5.0,90.0);}
      previous=e->pos();update();
    }
  }
  void mouseDoubleClickEvent(QMouseEvent* e)override{if(kind!=0&&e->button()==Qt::LeftButton){fitTrack();e->accept();}else QWidget::mouseDoubleClickEvent(e);}
  void wheelEvent(QWheelEvent* e)override{
    const double delta=e->angleDelta().y()!=0?e->angleDelta().y():e->pixelDelta().y()*3.;
    if(delta==0){e->ignore();return;}
    const double factor=std::exp(std::clamp(delta,-1200.0,1200.0)*std::log(1.25)/120.);
    if(kind==0){zoom=std::clamp(zoom*std::pow(1.001,e->angleDelta().y()),.3,5.0);update();}
    else {
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
      const auto anchor=e->position();
#else
      const auto anchor=e->posF();
#endif
      zoomTrack(factor,trackArea().contains(anchor)?anchor:trackArea().center());
    }
    e->accept();
  }
};
GnssVisualization::GnssVisualization(QWidget* parent):QWidget(parent){
  auto* root=new QVBoxLayout(this);auto* actions=new QHBoxLayout;status_=new QLabel(this);status_->setWordWrap(true);
  auto* clear=new QPushButton(uiText("Clear trajectories / reset origin","清空轨迹／重置原点"),this);
  auto* top=new QCheckBox(uiText("Top view (2D)","俯视图（2D）"),this);actions->addWidget(status_,1);actions->addWidget(top);actions->addWidget(clear);root->addLayout(actions);
  auto* tabs=new QTabWidget(this);root->addWidget(tabs,1);auto* skyPage=new QWidget(tabs);auto* skyLayout=new QVBoxLayout(skyPage);
  auto* hint=new QLabel(uiText("GSV azimuth/elevation/C/N0; GSA marks satellites used for GNSS, not per-satellite RTK participation.","GSV 方位角／仰角／信噪比；GSA 标识 GNSS 使用卫星，不代表逐颗 RTK 参与状态。"),skyPage);hint->setWordWrap(true);skyLayout->addWidget(hint);
  auto* splitter=new QSplitter(skyPage);sky_=new GnssPlot(0,&model_,splitter);auto* list=new QWidget(splitter);auto* listLayout=new QVBoxLayout(list);
  filter_=new QLineEdit(list);filter_->setPlaceholderText(uiText("Filter constellation / PRN / signal","筛选星座／编号／信号"));listLayout->addWidget(filter_);
  table_=new QTableWidget(0,6,list);table_->setHorizontalHeaderLabels({uiText("Satellite","卫星"),uiText("Azimuth °","方位角 °"),uiText("Elevation °","仰角 °"),"C/N0 dB-Hz",uiText("Signals","信号"),"GSA"});
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);table_->horizontalHeader()->setStretchLastSection(true);table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->setSortingEnabled(true);listLayout->addWidget(table_);splitter->addWidget(sky_);splitter->addWidget(list);splitter->setStretchFactor(0,1);splitter->setStretchFactor(1,1);skyLayout->addWidget(splitter,1);tabs->addTab(skyPage,uiText("Satellite sky","卫星天空图"));
  auto page=[&](int kind,const QString& title,QLabel*& label,GnssPlot*& plot){
    auto* w=new QWidget(tabs);auto* l=new QVBoxLayout(w);label=new QLabel(w);label->setObjectName(kind==2?"rtkReceiverPosition":"gnssPosition");label->setTextFormat(Qt::PlainText);label->setWordWrap(true);label->setMinimumWidth(0);label->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);label->setMinimumHeight(75);l->addWidget(label);
    plot=new GnssPlot(kind,&model_,w);const QString prefix=kind==2?"rtk":"gnss";plot->setObjectName(prefix+"Trajectory");
    plot->setAccessibleName(title);
    auto* tools=new QHBoxLayout;
    auto* out=new QPushButton(uiText("− Zoom out","− 缩小"),w);out->setObjectName(prefix+"ZoomOut");
    auto* in=new QPushButton(uiText("+ Zoom in","+ 放大"),w);in->setObjectName(prefix+"ZoomIn");
    auto* fit=new QPushButton(uiText("Fit trajectory","适应轨迹"),w);fit->setObjectName(prefix+"FitTrajectory");
    tools->addWidget(out);tools->addWidget(in);tools->addWidget(fit);tools->addStretch();l->addLayout(tools);
    auto* help=new QLabel(uiText("Wheel / trackpad: zoom · 2D drag: pan · 3D drag: rotate (Shift: pan) · Double-click: fit",
        "滚轮／触控板：缩放 · 2D 拖动：平移 · 3D 拖动：旋转（Shift：平移）· 双击：适应轨迹"),w);
    help->setWordWrap(true);l->addWidget(help);plot->setToolTip(help->text());
    connect(in,&QPushButton::clicked,plot,[plot]{plot->zoomTrack(2,plot->trackArea().center());});
    connect(out,&QPushButton::clicked,plot,[plot]{plot->zoomTrack(.5,plot->trackArea().center());});
    connect(fit,&QPushButton::clicked,plot,[plot]{plot->fitTrack();});
    l->addWidget(plot,1);tabs->addTab(w,title);
  };
  page(1,uiText("GNSS · GGA trajectory","GNSS · GGA 轨迹"),gnss_,gnss_plot_);page(2,uiText("RTK · ADRNAV trajectory","RTK · ADRNAV 轨迹"),rtk_,rtk_plot_);
  clear->setObjectName("clearTrajectories");top->setObjectName("trajectoryTopView");
  connect(clear,&QPushButton::clicked,this,[this]{model_.clearTracks();gnss_plot_->fitTrack();rtk_plot_->fitTrack();refresh();});
  connect(top,&QCheckBox::toggled,this,[this](bool v){for(auto* p:{sky_,gnss_plot_,rtk_plot_}){p->top=v;p->fitTrack();}});
  connect(filter_,&QLineEdit::textChanged,this,[this]{refresh();});
  connect(table_,&QTableWidget::itemSelectionChanged,this,[this]{if(auto* item=table_->item(table_->currentRow(),0)){sky_->selected=item->text();sky_->update();}});
  sky_->onSelected=[this](QString id){for(int i=0;i<table_->rowCount();i++)if(table_->item(i,0)->text()==id){table_->selectRow(i);table_->scrollToItem(table_->item(i,0));break;}};
  auto* timer=new QTimer(this);connect(timer,&QTimer::timeout,this,[this]{refresh();});timer->start(200);refresh();
}
void GnssVisualization::apply(const prism::GnssObservations& b){if(model_.session&&model_.session!=b.session){gnss_plot_->fitTrack();rtk_plot_->fitTrack();}model_.apply(b);age_.restart();error_.clear();refresh();}
void GnssVisualization::unavailable(const QString& s){error_=s;refresh();}
void GnssVisualization::reset(){model_={};gnss_plot_->fitTrack();rtk_plot_->fitTrack();age_.invalidate();error_.clear();refresh();}
void GnssVisualization::refresh(){
  uint64_t now=model_.now+(age_.isValid()?uint64_t(age_.elapsed()):10000);auto satellites=model_.satellites(now);
  status_->setText(!error_.isEmpty()?error_:uiText("%1 satellites · gaps %2 · live data only","%1 颗卫星 · 数据断档 %2 · 仅显示实测数据").arg(satellites.size()).arg(model_.gaps));
  auto position=[&](const prism::gnss_plot::Position& p){
    if(!p.valid||!prism::gnss_plot::fresh(now,p.ms,2000)||!error_.isEmpty())return uiText("No fresh valid solution; historical trajectory retained","暂无新鲜有效解；保留历史轨迹");
    if(!model_.origin.valid)return uiText("Waiting for the next valid position to set origin","等待下一条有效定位建立原点");
    auto xyz=model_.origin.project(p);
    return QString("%1 · %2 · %3\nLat %4°  Lon %5°  h %6 m\nE %7 m  N %8 m  U %9 m · %10 s")
      .arg(QString::fromStdString(p.source),QString::fromStdString(p.solution),QString::fromStdString(p.epoch))
      .arg(p.latitude,0,'f',9).arg(p.longitude,0,'f',9).arg(p.height?QString::number(*p.height,'f',3):"—")
      .arg(xyz.e,0,'f',3).arg(xyz.n,0,'f',3).arg(xyz.height_valid?QString::number(xyz.u,'f',3):"—").arg((now-p.ms)/1000.,0,'f',1);
  };
  const bool rtk_fresh=model_.rtk.valid&&prism::gnss_plot::fresh(now,model_.rtk.ms,2000)&&error_.isEmpty();
  const auto sigma=[&](std::optional<double> value){return rtk_fresh&&value&&std::isfinite(*value)&&*value>=0?QString::number(*value, 'f',3):QString("—");};
  const auto accuracy=uiText("Receiver-estimated precision (1σ, m): North N %1 · East E %2 · Up U %3",
      "接收机估计精度（1σ，m）：北向 N %1 · 东向 E %2 · 高程 U %3")
      .arg(sigma(model_.rtk.north_sigma),sigma(model_.rtk.east_sigma),sigma(model_.rtk.up_sigma));
  const auto accuracy_note=uiText("— means unavailable or stale; estimated standard deviations are not guaranteed actual errors.",
      "— 表示未提供或已失效；标准差估计不代表实际误差保证。");
  gnss_->setText(position(model_.gnss));rtk_->setText(position(model_.rtk)+"\n"+accuracy+"\n"+accuracy_note);
  auto selected=sky_->selected;int sort=table_->horizontalHeader()->sortIndicatorSection();auto order=table_->horizontalHeader()->sortIndicatorOrder();
  QString fingerprint=filter_->text()+"|"+selected+"|"+QString::number(sort)+"|"+QString::number(int(order));
  for(const auto& s:satellites) {
    fingerprint+="|"+QString::fromStdString(s.id)+":"+QString::fromStdString(s.signal_ids)+":"+QString::number(s.used);
    for(auto v:{s.azimuth,s.elevation,s.cn0})fingerprint+=":"+(v?QString::number(*v):QString("-"));
  }
  if(fingerprint!=table_fingerprint_) {
  table_fingerprint_=fingerprint;QSignalBlocker blocker(table_);table_->setSortingEnabled(false);table_->setRowCount(0);
  for(const auto& s:satellites){QString id=QString::fromStdString(s.id),signalText=QString::fromStdString(s.signal_ids);if(!(id+" "+signalText).contains(filter_->text(),Qt::CaseInsensitive))continue;
    int r=table_->rowCount();table_->insertRow(r);table_->setItem(r,0,new QTableWidgetItem(id));int col=1;
    for(auto v:{s.azimuth,s.elevation,s.cn0}){auto* item=new QTableWidgetItem;if(v)item->setData(Qt::DisplayRole,*v);else item->setText("—");table_->setItem(r,col++,item);}
    table_->setItem(r,4,new QTableWidgetItem(signalText));table_->setItem(r,5,new QTableWidgetItem(s.used?"✓":"—"));}
  table_->setSortingEnabled(true);table_->sortItems(sort,order);for(int r=0;r<table_->rowCount();r++)if(table_->item(r,0)->text()==selected)table_->selectRow(r);
  }
  for(auto* p:{sky_,gnss_plot_,rtk_plot_}){p->now=now;p->update();}
}
}
