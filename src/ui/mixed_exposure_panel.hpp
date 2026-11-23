#pragma once
#include "common/ui_text.hpp"
#include "prism/usb/exposure.hpp"
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>
#include <array>
#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>

namespace prism_viewer::ui {
// Each bank owns its edits and commits independently; refreshing one must not
// overwrite unsubmitted edits in the other bank.
class MixedExposurePanel final : public QWidget {
  struct Bank {
    QWidget* fields=nullptr;
    QLabel* message=nullptr;
    QSpinBox *target=nullptr,*min_exp=nullptr,*max_exp=nullptr;
    QDoubleSpinBox *min_gain=nullptr,*max_gain=nullptr;
    QComboBox* unified=nullptr;
    std::array<QComboBox*,2> mode{};
    std::array<QSpinBox*,2> exposure{};
    std::array<QDoubleSpinBox*,2> gain{};
    QPushButton *refresh=nullptr,*apply=nullptr;
    bool ready=false;
    uint32_t ceiling=995000;
  };
  std::array<Bank,2> banks_;
  bool available_=false,busy_=false;
  using Config=prism::ExposureGroupConfiguration;
  static int camera(unsigned g,unsigned i){return g?int(i+1):int(i*3);}
  void refreshEnabled() {
    for(unsigned g=0;g<2;++g){auto& b=banks_[g];
      b.refresh->setEnabled(available_&&!busy_);
      b.fields->setEnabled(available_&&!busy_&&b.ready);
      b.apply->setEnabled(available_&&!busy_&&b.ready);
      for(unsigned i=0;i<2;++i){
        const int hi=std::min<int>(b.max_exp->value(),b.ceiling);
        if(b.min_exp->value()<=hi)b.exposure[i]->setRange(b.min_exp->value(),hi);
        if(b.min_gain->value()<=b.max_gain->value())b.gain[i]->setRange(b.min_gain->value(),b.max_gain->value());
        const bool unified=!g&&b.unified->currentIndex()==1;
        b.mode[i]->setEnabled(!unified);
        const bool manual=!unified&&b.mode[i]->currentIndex()==1;
        b.exposure[i]->setEnabled(manual);b.gain[i]->setEnabled(manual);
      }
    }
  }
  Config edited(unsigned g)const {
    const auto& b=banks_[g];Config c;c.group=uint8_t(g);c.automatic_camera_mask=0;
    c.unified_automatic=!g&&b.unified->currentIndex()==1;
    c.target_brightness=uint8_t(b.target->value());
    c.min_exposure_us=b.min_exp->value();c.max_exposure_us=b.max_exp->value();
    c.min_gain_x1024=uint32_t(std::lround(b.min_gain->value()*1024));
    c.max_gain_x1024=uint32_t(std::lround(b.max_gain->value()*1024));
    for(unsigned i=0;i<2;++i){
      if(c.unified_automatic||b.mode[i]->currentIndex()==0)c.automatic_camera_mask|=uint8_t(1u<<camera(g,i));
      c.manual_exposure_us[i]=b.exposure[i]->value();c.gain_x1024[i]=uint32_t(std::lround(b.gain[i]->value()*1024));
    }
    return c;
  }
 public:
  std::function<void(uint8_t,std::optional<Config>)> on_request;
  explicit MixedExposurePanel(QWidget* parent=nullptr):QWidget(parent) {
    using prism_viewer::common::uiText;
    auto* root=new QVBoxLayout(this);root->setContentsMargins(0,0,0,0);
    const auto wrappedLabel=[](QWidget* parent,const QString& text=QString()) {
      auto* label=new QLabel(text,parent);label->setWordWrap(true);
      auto policy=label->sizePolicy();policy.setHorizontalPolicy(QSizePolicy::Ignored);
      policy.setVerticalPolicy(QSizePolicy::Maximum);label->setSizePolicy(policy);
      label->setMinimumWidth(0);return label;
    };
    for(unsigned g=0;g<2;++g){auto& b=banks_[g];
      auto* box=new QGroupBox(g?uiText("12M · Cameras 1 / 2","12M · 相机 1 / 2"):uiText("1.3M · Cameras 0 / 3","1.3M · 相机 0 / 3"),this);
      box->setObjectName(QStringLiteral("exposureBank%1").arg(g));root->addWidget(box);
      auto* layout=new QVBoxLayout(box);b.message=wrappedLabel(box);layout->addWidget(b.message);
      b.fields=new QWidget(box);auto* grid=new QGridLayout(b.fields);grid->setContentsMargins(0,0,0,0);layout->addWidget(b.fields);
      // Let labels wrap beside the numeric controls. Equal stretch would
      // duplicate Windows' wider spin-box minimum into the label column.
      grid->setColumnStretch(0,1);
      const auto integer=[&](int lo,int hi,const QString& name){auto* w=new QSpinBox(b.fields);w->setRange(lo,hi);w->setObjectName(name);return w;};
      const auto gain=[&](const QString& name){auto* w=new QDoubleSpinBox(b.fields);w->setDecimals(5);w->setRange(1,g?28:124);w->setSingleStep(g?0.0625:0.03125);w->setSuffix(QStringLiteral("×"));w->setObjectName(name);return w;};
      b.target=integer(1,255,QStringLiteral("target"));
      b.min_exp=integer(g?54:50,g?18432:995000,QStringLiteral("minExposure"));
      b.max_exp=integer(g?54:50,g?18432:995000,QStringLiteral("maxExposure"));
      b.min_gain=gain(QStringLiteral("minGain"));b.max_gain=gain(QStringLiteral("maxGain"));
      b.unified=new QComboBox(b.fields);b.unified->addItem(uiText("Independent automatic exposure","独立自动曝光"));
      if(!g)b.unified->addItem(uiText("Unified automatic exposure","统一自动曝光"));
      b.unified->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
      b.unified->setMinimumContentsLength(12);
      b.unified->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
      int row=0;grid->addWidget(b.unified,row++,0,1,2);
      const auto field=[&](const QString& label,QWidget* w){
        grid->addWidget(wrappedLabel(b.fields,label),row,0);
        w->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        grid->addWidget(w,row++,1);
      };
      field(uiText("Target brightness (1–255)","目标亮度（1–255）"),b.target);
      field(uiText("Minimum exposure (µs)","曝光下限（微秒）"),b.min_exp);
      field(uiText("Maximum exposure (µs)","曝光上限（微秒）"),b.max_exp);
      field(uiText("Minimum gain","增益下限"),b.min_gain);field(uiText("Maximum gain","增益上限"),b.max_gain);
      for(unsigned i=0;i<2;++i){
        b.mode[i]=new QComboBox(b.fields);b.mode[i]->addItems({uiText("Automatic","自动"),uiText("Manual","手动")});
        b.exposure[i]=integer(g?54:50,g?18432:995000,QStringLiteral("exposure%1").arg(camera(g,i)));
        b.gain[i]=gain(QStringLiteral("gain%1").arg(camera(g,i)));
        field(uiText("Camera %1 mode","相机 %1 模式").arg(camera(g,i)),b.mode[i]);
        field(uiText("Camera %1 exposure (µs)","相机 %1 曝光（微秒）").arg(camera(g,i)),b.exposure[i]);
        field(uiText("Camera %1 gain","相机 %1 增益").arg(camera(g,i)),b.gain[i]);
        connect(b.mode[i],qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{refreshEnabled();});
      }
      connect(b.unified,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{refreshEnabled();});
      for(auto* s:{b.min_exp,b.max_exp})connect(s,qOverload<int>(&QSpinBox::valueChanged),this,[this]{refreshEnabled();});
      for(auto* s:{b.min_gain,b.max_gain})connect(s,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{refreshEnabled();});
      auto* buttons=new QHBoxLayout();layout->addLayout(buttons);
      b.refresh=new QPushButton(uiText("Read this group","读取本组"),box);b.apply=new QPushButton(uiText("Apply this group","应用本组"),box);
      buttons->addWidget(b.refresh);buttons->addWidget(b.apply);
      connect(b.refresh,&QPushButton::clicked,this,[this,g]{if(on_request)on_request(uint8_t(g),std::nullopt);});
      connect(b.apply,&QPushButton::clicked,this,[this,g]{
        // Commit text even if macOS has not moved focus from a spin box.
        for(auto* spin:banks_[g].fields->findChildren<QAbstractSpinBox*>())spin->interpretText();
        if(on_request)on_request(uint8_t(g),edited(g));
      });
    }
    clear();
  }
  void clear(){using prism_viewer::common::uiText;for(auto& b:banks_){b.ready=false;b.message->setText(uiText("Read settings before editing this group.","读取本组设置后可修改。"));}busy_=false;refreshEnabled();}
  void setAvailability(bool available){available_=available;refreshEnabled();}
  void setBusy(bool busy){busy_=busy;refreshEnabled();}
  void setError(unsigned g,const QString& error){if(g<2)banks_[g].message->setText(error);busy_=false;refreshEnabled();}
  void setConfiguration(const Config& c,bool live){
    if(c.group>1)return;auto& b=banks_[c.group];b.ready=true;b.ceiling=c.effective_max_exposure_us;
    b.target->setValue(c.target_brightness);b.min_exp->setValue(c.min_exposure_us);b.max_exp->setValue(c.max_exposure_us);
    b.min_gain->setValue(c.min_gain_x1024/1024.0);b.max_gain->setValue(c.max_gain_x1024/1024.0);b.unified->setCurrentIndex(c.unified_automatic?1:0);
    for(unsigned i=0;i<2;++i){b.mode[i]->setCurrentIndex((c.automatic_camera_mask&(1u<<camera(c.group,i)))?0:1);b.exposure[i]->setValue(c.manual_exposure_us[i]);b.gain[i]->setValue(c.gain_x1024[i]/1024.0);}
    using prism_viewer::common::uiText;
    b.message->setText((live?uiText("Runtime settings","运行时设置"):uiText("Settings for the next capture","下次采集设置"))+uiText(" · Effective maximum: %1 µs"," · 实际曝光上限：%1 微秒").arg(c.effective_max_exposure_us));
    busy_=false;refreshEnabled();
  }
};
}
