#include "ui/bayer_preview.hpp"
#include "ui/camera_zoom_dialog.hpp"
#include "ui/image_view_label.hpp"
#include "ui/zoomable_image_view.hpp"
#include <QtWidgets/QApplication>
#include <QtWidgets/QGraphicsPixmapItem>
#include <QtWidgets/QGraphicsScene>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <iostream>
#include <stdexcept>
#include <cmath>

using namespace prism_viewer::ui;
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
QPixmap labelPixmap(const QLabel& label) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return label.pixmap();
#else
  const auto* value=label.pixmap();
  return value ? *value : QPixmap{};
#endif
}
void uniform(const QImage& image,int expected) {
  require(!image.isNull(),"missing display");
  for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)
    require(qGray(image.pixel(x,y))==expected,"CFA checkerboard/brightness mismatch");
}
QGraphicsPixmapItem* pixmap(ZoomableImageView& view){
  for(auto* item:view.scene()->items())if(auto* p=dynamic_cast<QGraphicsPixmapItem*>(item))return p;
  throw std::runtime_error("missing zoom pixmap");
}
int main(int argc,char** argv){
 QApplication app(argc,argv);
 try {
  const auto& lut=prism_preview::linear8ToSrgb();
  QImage ramp(256,1,QImage::Format_Grayscale8);
  for(int i=0;i<256;++i){
    ramp.scanLine(0)[i]=uint8_t(i);
    const double x=i/255.0;
    const int expected=int(std::lround(255*(x<=0.0031308?12.92*x:1.055*std::pow(x,1/2.4)-0.055)));
    require(lut[i]==expected,"sRGB reference mismatch");
    if(i)require(lut[i]>=lut[i-1],"nonmonotonic transfer");
  }
  require(lut[0]==0&&lut[1]==13&&lut[128]==188&&lut[255]==255,"sRGB anchors");
  require(raw12SrgbDisplay(ramp)==ramp,"unmarked grayscale/JPEG changed");
  ramp.setText(kRaw12PreviewKey,QStringLiteral("cell-average"));
  const auto displayRamp=raw12SrgbDisplay(ramp);
  for(int i=0;i<256;++i){
    require(ramp.constScanLine(0)[i]==i,"linear cache mutated");
    require(displayRamp.constScanLine(0)[i]==lut[i],"display transfer missing");
  }
  require(raw12SrgbDisplay(displayRamp)==displayRamp,"double sRGB encoding");
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
  require(displayRamp.colorSpace()==QColorSpace(QColorSpace::SRgb),"missing sRGB color space");
#endif
  QImage native(5120,3072,QImage::Format_Grayscale8);
  const unsigned char phase[]={40,180,180,80};
  for(int y=0;y<native.height();++y)for(int x=0;x<native.width();++x)
    native.scanLine(y)[x]=phase[(y&1)*2+(x&1)];
  native.setText(kRaw12PreviewKey,QStringLiteral("native-bayer"));
  const QImage original=native.copy();
  const QImage displayNative=raw12SrgbDisplay(original);
  auto cells=raw12ThumbnailSource(native);
  require(cells.size()==QSize(2560,1536),"bounded cell cache");uniform(cells,120);
  require(raw12ThumbnailSource(cells).size()==cells.size(),"filtered thumbnail binned twice");
  ImageViewLabel tile;tile.setTransformationMode(Qt::FastTransformation);
  for(const QSize size:{QSize(233,140),QSize(226,136),QSize(317,191),QSize(640,512)}){
    tile.setFixedSize(size);tile.show();app.processEvents();tile.setImage(native);
    uniform(labelPixmap(tile).toImage(),lut[120]);
    tile.setImage(cells.scaled(640,384,Qt::IgnoreAspectRatio,Qt::SmoothTransformation));
    uniform(labelPixmap(tile).toImage(),lut[120]);
  }
  // Legacy JPEG/41M3/default images are unmarked; retain previous sampling.
  QImage jpeg(1280,1024,QImage::Format_RGB32);
  for(int y=0;y<jpeg.height();++y)for(int x=0;x<jpeg.width();++x)
    jpeg.setPixel(x,y,qRgb(x%256,y%256,(x+y)%256));
  tile.setImage(jpeg);
  require(labelPixmap(tile).toImage()==QPixmap::fromImage(jpeg).scaled(
      tile.size(),Qt::KeepAspectRatio,Qt::FastTransformation).toImage(),"legacy grid changed");
  tile.clearImage("none");require(labelPixmap(tile).isNull(),"stale tile");
  CameraZoomDialog dialog;dialog.show();
  auto* view=dynamic_cast<ZoomableImageView*>(dialog.findChild<QGraphicsView*>("cameraZoomImage"));
  auto* actual=dialog.findChild<QPushButton*>("cameraZoomActualSize");
  require(view&&actual,"zoom controls");
  std::array<QImage,4> images{cells,cells,native,cells};
  const std::array<bool,4> full{false,false,true,false};
  for(const QSize size:{QSize(1240,880),QSize(1000,720),QSize(1400,960)}) {
    dialog.resize(size);dialog.setImageSet(images,2,"Bayer test",full);app.processEvents();
    auto* thumb=dialog.findChild<QLabel*>("cameraZoomThumbnail2");require(thumb,"thumbnail");
    uniform(labelPixmap(*thumb).toImage(),lut[120]);
    view->fitToWindow();
    uniform(pixmap(*view)->pixmap().toImage(),lut[120]);
    require(pixmap(*view)->transform().m11()==2.0,"filtered scene native coordinates");
    require(view->scene()->sceneRect()==QRectF(0,0,5120,3072),"scene dimensions changed");
    actual->click();require(view->zoomPercent()==100,"native100%");
    require(pixmap(*view)->pixmap().toImage().convertToFormat(QImage::Format_Grayscale8)==displayNative,
            "100% sRGB display mismatch");
    require(pixmap(*view)->transform().isIdentity(),"100% scaled item");
    for(int n=0;n<5;++n)view->zoomOut();
    const auto before=view->transform();const auto bounds=view->scene()->sceneRect();
    dialog.setImageSet(images,2,"next frame",full);
    require(view->transform()==before&&view->scene()->sceneRect()==bounds,"live zoom jump");
    uniform(pixmap(*view)->pixmap().toImage(),lut[120]);
    actual->click();view->zoomIn();
    require(pixmap(*view)->pixmap().toImage().convertToFormat(QImage::Format_Grayscale8)==displayNative,
            "above100% sRGB display mismatch");
  }
  images[1]=jpeg;dialog.setImageSet(images,1,"legacy",{false,true,true,false});
  app.processEvents();
  require(pixmap(*view)->pixmap().toImage()==QPixmap::fromImage(jpeg).toImage(),"legacy zoom changed");
  require(pixmap(*view)->transform().isIdentity(),"legacy inherited Bayer scale");
  auto* thumb=dialog.findChild<QLabel*>("cameraZoomThumbnail1");
  require(labelPixmap(*thumb).toImage()==QPixmap::fromImage(jpeg).scaled(
      thumb->size(),Qt::KeepAspectRatio,Qt::FastTransformation).toImage(),"legacy thumbnail changed");
  images[2]=cells;dialog.setImageSet(images,2,"late thumbnail",{});
  require(!view->hasImage()&&!actual->isEnabled(),"late thumbnail enlarged");
  images[2]=native;dialog.setImageSet(images,2,"restored full",full);
  require(view->hasImage(),"full image after late thumbnail");
  view->clearImage("none");require(!view->hasImage(),"clear cached raw");
  require(native==original,"original display pixels mutated");
  std::cout<<"PASS sRGB all256/once, linear CFA averaging, grid/zoom/strip, 7 sizes, native100%, live zoom, RAW and legacy unchanged, late/clear guards\n";
  return 0;
 } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
