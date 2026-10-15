#pragma once
#include "app/Settings.h"
#include "ui/Ui.h"

namespace opal {

enum class BarAction { None, Capture, Cancel };

class CaptureBar {
 public:
  D2D1_RECT_F Layout(Ui& ui, D2D1_SIZE_F monitorDip);
  BarAction Draw(Ui& ui, CaptureMode* mode, bool canCapture);
  bool Contains(D2D1_POINT_2F dip) const;
  bool MenuOpen() const { return menuOpen_; }
  void CloseMenu() { menuOpen_ = false; }
  static constexpr float kRadius = 13.f;

 private:
  D2D1_RECT_F rect_{};
  D2D1_RECT_F menuRect_{};
  bool menuOpen_ = false;
};

}
