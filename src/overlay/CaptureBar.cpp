#include "overlay/CaptureBar.h"

#include <string>
#include <vector>

#include "core/Strings.h"

namespace opal {
namespace {

constexpr float kHeight = 52.f;
constexpr float kPad = 8.f;
constexpr float kCloseW = 36.f;
constexpr float kModeW = 46.f;
constexpr float kSepW = 13.f;
constexpr float kOptionsW = 112.f;
constexpr float kCaptureW = 88.f;
constexpr float kWidth = kPad + kCloseW + kSepW + 4 * kModeW + kSepW + kOptionsW + 6.f + kCaptureW + kPad;

}

D2D1_RECT_F CaptureBar::Layout(Ui& ui, D2D1_SIZE_F monitorDip) {
  const float slide = ui.AnimFrom(MakeId("bar.slide"), -kHeight - 30.f, 0.f, 16.f);
  const float x = (monitorDip.width - kWidth) * 0.5f;
  rect_ = RX(x, 18.f + slide, kWidth, kHeight);
  return rect_;
}

bool CaptureBar::Contains(D2D1_POINT_2F dip) const {
  return InRect(rect_, dip) || (menuOpen_ && InRect(menuRect_, dip));
}

BarAction CaptureBar::Draw(Ui& ui, CaptureMode* mode, bool canCapture) {
  const Theme& t = *ui.t;
  Painter& p = ui.p;
  BarAction action = BarAction::None;

  p.FillRound(rect_, kRadius, t.hudTint);
  p.StrokeRound(rect_, kRadius, t.hudBorder, 1.f);
  p.StrokeRound(Inset(rect_, 1.f), kRadius - 1.f, Hex(0x000000, 0.25f), 1.f);

  float x = rect_.left + kPad;
  const float y = rect_.top + kPad, h = kHeight - 2 * kPad;
  if (ui.IconButton(MakeId("bar.close"), RX(x, y + 4.f, kCloseW - 8.f, h - 8.f), Icon::Close, false,
                    T(S::Close), true, 15.f)) {
    action = BarAction::Cancel;
  }
  x += kCloseW;
  p.Line({x + kSepW / 2, y + 7.f}, {x + kSepW / 2, y + h - 7.f}, t.hudBorder, 1.f);
  x += kSepW;

  struct ModeDef {
    CaptureMode mode;
    Icon icon;
    S tip;
  };
  const ModeDef modes[] = {{CaptureMode::Screen, Icon::Screen, S::ModeScreen},
                           {CaptureMode::Window, Icon::Window, S::ModeWindow},
                           {CaptureMode::Region, Icon::Region, S::ModeRegion},
                           {CaptureMode::Element, Icon::Element, S::ModeElement}};
  for (const auto& m : modes) {
    if (ui.IconButton(MakeId("bar.mode", uint64_t(m.mode)), RX(x + 2.f, y, kModeW - 4.f, h), m.icon,
                      *mode == m.mode, T(m.tip), true, 22.f)) {
      *mode = m.mode;
    }
    x += kModeW;
  }
  p.Line({x + kSepW / 2, y + 7.f}, {x + kSepW / 2, y + h - 7.f}, t.hudBorder, 1.f);
  x += kSepW;

  Icon chevron = Icon::ChevronDown;
  D2D1_RECT_F optRect = RX(x, y, kOptionsW, h);
  if (ui.HudTextButton(MakeId("bar.options"), optRect, T(S::Options), false, &chevron)) {
    menuOpen_ = !menuOpen_;
  }
  x += kOptionsW + 6.f;
  if (canCapture) {
    if (ui.HudTextButton(MakeId("bar.capture"), RX(x, y + 2.f, kCaptureW, h - 4.f), T(S::Capture), true)) {
      action = BarAction::Capture;
    }
  } else {
    p.Text(T(S::Capture), RX(x, y, kCaptureW, h), 13.f, t.hudTextSecondary, FontWeight::Medium, HAlign::Center);
  }

  Settings& s = AppSettings();
  const int dest = s.copyToClipboard && s.saveToFile ? 2 : (s.saveToFile ? 1 : 0);
  std::vector<std::wstring> labels = {
      std::wstring(T(S::SaveTo)) + L": " + T(S::DestClipboard),
      std::wstring(T(S::SaveTo)) + L": " + T(S::DestFolder),
      std::wstring(T(S::SaveTo)) + L": " + T(S::DestBoth),
      L"",
      std::wstring(T(S::Timer)) + L": " + T(S::TimerNone),
      std::wstring(T(S::Timer)) + L": " + T(S::Timer5),
      std::wstring(T(S::Timer)) + L": " + T(S::Timer10),
      L"",
      T(S::ShowThumbnail),
      T(S::ShowPointer),
      T(S::WindowShadow),
      T(S::ShowLoupe),
  };
  std::vector<const wchar_t*> items;
  for (const auto& l : labels) items.push_back(l.c_str());
  const bool checked[] = {dest == 0, dest == 1, dest == 2, false,
                          s.timerSeconds == 0, s.timerSeconds == 5, s.timerSeconds == 10, false,
                          s.showThumbnail, s.showPointer, s.windowShadow, s.showLoupe};
  const bool wasOpen = menuOpen_;
  const int pick = ui.Menu(MakeId("bar.menu"), optRect, items, checked, true, &menuOpen_);
  if (wasOpen || menuOpen_) {
    float mh = 10.f;
    for (const auto& l : labels) mh += l.empty() ? 9.f : 24.f;
    float mw = 160.f;
    for (const auto& l : labels) mw = std::max(mw, p.Measure(l, 13.f).width + 40.f);
    menuRect_ = RX(optRect.left, optRect.bottom + 6.f, mw, mh);
  }
  bool changed = true;
  switch (pick) {
    case 0: s.copyToClipboard = true; s.saveToFile = false; break;
    case 1: s.copyToClipboard = false; s.saveToFile = true; break;
    case 2: s.copyToClipboard = true; s.saveToFile = true; break;
    case 4: s.timerSeconds = 0; break;
    case 5: s.timerSeconds = 5; break;
    case 6: s.timerSeconds = 10; break;
    case 8: s.showThumbnail = !s.showThumbnail; break;
    case 9: s.showPointer = !s.showPointer; break;
    case 10: s.windowShadow = !s.windowShadow; break;
    case 11: s.showLoupe = !s.showLoupe; break;
    default: changed = false; break;
  }
  if (changed) s.Save();
  return action;
}

}
