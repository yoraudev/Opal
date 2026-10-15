#pragma once
#include <functional>

#include "capture/Monitors.h"
#include "render/D2DWindow.h"

namespace opal {

class CountdownWindow : public D2DWindow {
 public:
  CountdownWindow(int seconds, const MonitorInfo& monitor, std::function<void()> onDone,
                  std::function<void()> onCancel);
  bool Init();

 protected:
  void OnRender(ID2D1DeviceContext5* dc) override;
  bool WantsFrame() override { return !fired_; }
  LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp, bool& handled) override;

 private:
  int seconds_;
  MonitorInfo monitor_;
  std::function<void()> onDone_, onCancel_;
  double start_ = 0;
  bool fired_ = false;
};

}
