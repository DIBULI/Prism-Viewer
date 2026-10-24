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

using namespace prism_viewer::ui;
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
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
  QImage native(5120,3072,QImage::Format_Grayscale8);
  const unsigned char phase[]={40,180,180,80};
  for(int y=0;y<native.height();++y)for(int x=0;x<native.width();++x)
    native.scanLine(y)[x]=phase[(y&1)*2+(x&1)];
  native.setText(kRaw12PreviewKey,QStringLiteral("native-bayer"));
  const QImage original=native.copy();
  auto cells=raw12ThumbnailSource(native);
  require(cells.size()==QSize(2560,1536),"bounded cell cache");uniform(cells,120);
  require(raw12ThumbnailSource(cells).size()==cells.size(),"filtered thumbnail binned twice");
  ImageViewLabel tile;tile.setTransformationMode(Qt::FastTransformation);
  for(const QSize size:{QSize(233,140),QSize(226,136),QSize(317,191),QSize(640,512)}){
    tile.setFixedSize(size);tile.show();app.processEvents();tile.setImage(native);
    uniform(tile.pixmap(Qt::ReturnByValue).toImage(),120);
    tile.setImage(cells.scaled(640,384,Qt::IgnoreAspectRatio,Qt::SmoothTransformation));
    uniform(tile.pixmap(Qt::ReturnByValue).toImage(),120);
  }
  // Legacy JPEG/41M3/default images are unmarked; retain previous sampling.
  QImage jpeg(1280,1024,QImage::Format_RGB32);
  for(int y=0;y<jpeg.height();++y)for(int x=0;x<jpeg.width();++x)
    jpeg.setPixel(x,y,qRgb(x%256,y%256,(x+y)%256));
  tile.setImage(jpeg);
  require(tile.pixmap(Qt::ReturnByValue).toImage()==QPixmap::fromImage(jpeg).scaled(
      tile.size(),Qt::KeepAspectRatio,Qt::FastTransformation).toImage(),"legacy grid changed");
  tile.clearImage("none");require(tile.pixmap(Qt::ReturnByValue).isNull(),"stale tile");
  CameraZoomDialog dialog;dialog.show();
  auto* view=dynamic_cast<ZoomableImageView*>(dialog.findChild<QGraphicsView*>("cameraZoomImage"));
  auto* actual=dialog.findChild<QPushButton*>("cameraZoomActualSize");
  require(view&&actual,"zoom controls");
  std::array<QImage,4> images{cells,cells,native,cells};
  const std::array<bool,4> full{false,false,true,false};
  for(const QSize size:{QSize(1240,880),QSize(1000,720),QSize(1400,960)}) {
    dialog.resize(size);dialog.setImageSet(images,2,"Bayer test",full);app.processEvents();
    auto* thumb=dialog.findChild<QLabel*>("cameraZoomThumbnail2");require(thumb,"thumbnail");
    uniform(thumb->pixmap(Qt::ReturnByValue).toImage(),120);
    view->fitToWindow();
    uniform(pixmap(*view)->pixmap().toImage(),120);
    require(pixmap(*view)->transform().m11()==2.0,"filtered scene native coordinates");
    require(view->scene()->sceneRect()==QRectF(0,0,5120,3072),"scene dimensions changed");
    actual->click();require(view->zoomPercent()==100,"native100%");
    require(pixmap(*view)->pixmap().toImage().convertToFormat(QImage::Format_Grayscale8)==original,
            "100% raw pixels changed");
    require(pixmap(*view)->transform().isIdentity(),"100% scaled item");
    for(int n=0;n<5;++n)view->zoomOut();
    const auto before=view->transform();const auto bounds=view->scene()->sceneRect();
    dialog.setImageSet(images,2,"next frame",full);
    require(view->transform()==before&&view->scene()->sceneRect()==bounds,"live zoom jump");
    uniform(pixmap(*view)->pixmap().toImage(),120);
    actual->click();view->zoomIn();
    require(pixmap(*view)->pixmap().toImage().convertToFormat(QImage::Format_Grayscale8)==original,
            "above100% raw pixels changed");
  }
  images[1]=jpeg;dialog.setImageSet(images,1,"legacy",{false,true,true,false});
  app.processEvents();
  require(pixmap(*view)->pixmap().toImage()==QPixmap::fromImage(jpeg).toImage(),"legacy zoom changed");
  require(pixmap(*view)->transform().isIdentity(),"legacy inherited Bayer scale");
  auto* thumb=dialog.findChild<QLabel*>("cameraZoomThumbnail1");
  require(thumb->pixmap(Qt::ReturnByValue).toImage()==QPixmap::fromImage(jpeg).scaled(
      thumb->size(),Qt::KeepAspectRatio,Qt::FastTransformation).toImage(),"legacy thumbnail changed");
  images[2]=cells;dialog.setImageSet(images,2,"late thumbnail",{});
  require(!view->hasImage()&&!actual->isEnabled(),"late thumbnail enlarged");
  images[2]=native;dialog.setImageSet(images,2,"restored full",full);
  require(view->hasImage(),"full image after late thumbnail");
  view->clearImage("none");require(!view->hasImage(),"clear cached raw");
  require(native==original,"original display pixels mutated");
  std::cout<<"PASS Bayer grid/zoom/strip, 7 sizes, native100%, live zoom, legacy unchanged, late/clear guards\n";
  return 0;
 } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
