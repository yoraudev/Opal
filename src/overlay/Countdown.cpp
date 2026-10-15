#include "overlay/Countdown.h"

#include <dwmapi.h>

#include <cmath>
#include <string>

#include "overlay/OverlayWindow.h"

namespace opal {
namespace {

constexpr float kW = 96.f, kH = 96.f;
constexpr UINT_PTR kTickTimer = 7;

}

CountdownWindow::CountdownWindow(int seconds, const MonitorInfo& monitor, std::function<void()> onDone,
                                 std::function<void()> onCancel)
    : seconds_(seconds), monitor_(monitor), onDone_(std::move(onDone)), onCancel_(std::move(onCancel)) {}

bool CountdownWindow::Init() {
  const float s = monitor_.Scale();
  const int w = int(kW * s), h = int(kH * s);
  CreateParams p;
  p.exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
  p.style = WS_POPUP;
  p.title = L"Opal Timer";
  const int x = monitor_.rect.left + (monitor_.rect.Width() - w) / 2;
  const int y = monitor_.rect.top + int(28 * s);
  p.rect = {x, y, x + w, y + h};
  if (!Create(p)) return false;
  ExcludeFromCapture(hwnd());
  start_ = Now();
  RenderNow();
  ShowWindow(hwnd(), SW_SHOWNOACTIVATE);
  SetTimer(hwnd(), kTickTimer, 50, nullptr);
  return true;
}

LRESULT CountdownWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp, bool& handled) {
  handled = false;
  if (msg == WM_TIMER && wp == kTickTimer && !fired_) {
    if (Now() - start_ >= seconds_) {
      fired_ = true;
      KillTimer(hwnd(), kTickTimer);
      ShowWindow(hwnd(), SW_HIDE);
      DwmFlush();
      DwmFlush();
      if (onDone_) onDone_();
    }
    handled = true;
    return 0;
  }
  if (msg == kMsgCaptureKey && !fired_ && wp == VK_ESCAPE && !(lp & 0x10000)) {
    fired_ = true;
    KillTimer(hwnd(), kTickTimer);
    ShowWindow(hwnd(), SW_HIDE);
    if (onCancel_) onCancel_();
    handled = true;
    return 0;
  }
  if (msg == WM_LBUTTONUP && !fired_) {
    fired_ = true;
    ShowWindow(hwnd(), SW_HIDE);
    if (onCancel_) onCancel_();
    handled = true;
    return 0;
  }
  (void)lp;
  return 0;
}

void CountdownWindow::OnRender(ID2D1DeviceContext5* dc) {
  BeginUiFrame(dc);
  Painter& p = ui_.p;
  const Theme& t = Theme::Current();
  const D2D1_RECT_F r = Inset(RX(0, 0, kW, kH), 6.f);
  p.FillRound(r, 22.f, Hex(0x1C1C1E, 0.88f));
  p.StrokeRound(r, 22.f, t.hudBorder, 1.f);
  const double elapsed = Now() - start_;
  const int remaining = std::max(1, int(std::ceil(seconds_ - elapsed)));
  const float frac = float(std::fmod(elapsed, 1.0));

  const D2D1_POINT_2F c = Center(r);
  const float rad = 30.f;
  dc->DrawEllipse({c, rad, rad}, p.Brush(Hex(0xFFFFFF, 0.14f)), 4.f);
  ComPtr<ID2D1Factory> f;
  dc->GetFactory(&f);
  ComPtr<ID2D1PathGeometry> arc;
  if (SUCCEEDED(f->CreatePathGeometry(&arc))) {
    ComPtr<ID2D1GeometrySink> sink;
    arc->Open(&sink);
    const float a = (1.f - frac) * 6.2831853f;
    sink->BeginFigure({c.x, c.y - rad}, D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc({{c.x + rad * std::sin(a), c.y - rad * std::cos(a)}, {rad, rad}, 0.f,
                  D2D1_SWEEP_DIRECTION_CLOCKWISE, a > 3.14159f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL});
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    D2D1_STROKE_STYLE_PROPERTIES sp{D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                    D2D1_LINE_JOIN_ROUND, 10.f, D2D1_DASH_STYLE_SOLID, 0.f};
    ComPtr<ID2D1StrokeStyle> round;
    f->CreateStrokeStyle(sp, nullptr, 0, &round);
    dc->DrawGeometry(arc.Get(), p.Brush(t.accent), 4.f, round.Get());
  }
  p.Text(std::to_wstring(remaining), r, 30.f, t.hudText, FontWeight::Semibold, HAlign::Center);
  EndUiFrame(dc);
}

}
