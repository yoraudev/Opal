#include "overlay/CaptureSession.h"

#include <dwmapi.h>

#include <algorithm>

#include "app/Hotkeys.h"
#include "capture/CornerRadius.h"
#include "capture/EdgeSnap.h"
#include "capture/ScreenGrab.h"
#include "capture/WindowCapture.h"
#include "core/Log.h"
#include "overlay/Countdown.h"
#include "overlay/OverlayWindow.h"
#include "render/ImageOps.h"

namespace opal {
namespace {

RectI ClampInto(RectI r, const RectI& bounds) {
  if (r.Width() > bounds.Width()) r.right = r.left + bounds.Width();
  if (r.Height() > bounds.Height()) r.bottom = r.top + bounds.Height();
  if (r.left < bounds.left) r = r.Offset(bounds.left - r.left, 0);
  if (r.top < bounds.top) r = r.Offset(0, bounds.top - r.top);
  if (r.right > bounds.right) r = r.Offset(bounds.right - r.right, 0);
  if (r.bottom > bounds.bottom) r = r.Offset(0, bounds.bottom - r.bottom);
  return r;
}

bool NearlyEqual(const RectI& a, const RectI& b, int tol) {
  return std::abs(a.left - b.left) <= tol && std::abs(a.top - b.top) <= tol && std::abs(a.right - b.right) <= tol &&
         std::abs(a.bottom - b.bottom) <= tol;
}

bool AltDown() { return GetAsyncKeyState(VK_MENU) < 0; }
bool ShiftDown() { return GetAsyncKeyState(VK_SHIFT) < 0; }

}

CaptureSession::CaptureSession(CaptureMode mode, bool showBar, DoneFn done)
    : mode_(mode), showBar_(showBar), done_(std::move(done)) {}

CaptureSession::~CaptureSession() {
  HotkeyManager::Get().SetCaptureTarget(nullptr, 0);
  overlays_.clear();
  countdown_.reset();
}

std::optional<CaptureResult> CaptureSession::CaptureScreenNow() {
  POINT pt;
  GetCursorPos(&pt);
  auto monitors = EnumerateMonitors();
  if (monitors.empty()) return std::nullopt;
  const MonitorInfo& m = monitors[size_t(MonitorAt(monitors, pt))];
  CaptureResult r;
  if (!GrabScreen(m.rect, AppSettings().showPointer, &r.image)) return std::nullopt;
  r.sourceRect = m.rect;
  r.mode = CaptureMode::Screen;
  r.monitorRect = m.rect;
  r.monitorDpi = m.dpi;
  return r;
}

bool CaptureSession::Start() {
  startTime_ = D2DWindow::Now();
  const Settings& s = AppSettings();
  virt_ = VirtualScreen();
  GetCursorPos(&cursor_);
  if (!GrabScreen(virt_, s.showPointer, &freeze_)) return false;
  monitors_ = EnumerateMonitors();
  if (monitors_.empty()) return false;
  windows_ = EnumerateWindows();
  cursorMonitor_ = MonitorAt(monitors_, cursor_);
  barMonitor_ = cursorMonitor_;
  if (mode_ == CaptureMode::Element) tree_ = std::make_unique<ElementTree>();
  if (showBar_ && mode_ == CaptureMode::Region && !s.lastRegion.Empty()) {
    selection_ = s.lastRegion.Intersect(virt_);
    hasSelection_ = selection_.Width() >= 4 && selection_.Height() >= 4;
  }

  for (int i = 0; i < int(monitors_.size()); ++i) {
    auto ov = std::make_unique<OverlayWindow>(this, i);
    if (!ov->Init()) {
      Log(L"Overlay %d failed", i);
      return false;
    }
    overlays_.push_back(std::move(ov));
  }
  UpdateHover();
  for (auto& ov : overlays_) ov->RenderNow();
  for (auto& ov : overlays_) ov->Reveal();
  overlays_[size_t(cursorMonitor_)]->TakeFocus();
  HotkeyManager::Get().SetCaptureTarget(overlays_[size_t(cursorMonitor_)]->hwnd(), kMsgCaptureKey);
  Log(L"Capture session started: mode %d bar %d, %zu windows, %zu monitors (%.1f ms)", int(mode_), int(showBar_),
      windows_.size(), monitors_.size(), (D2DWindow::Now() - startTime_) * 1000.0);
  return true;
}

void CaptureSession::InvalidateAll() {
  for (auto& ov : overlays_) ov->Invalidate();
}

bool CaptureSession::WaitingForTree() const {
  return mode_ == CaptureMode::Element && tree_ && elementHwnd_ && !tree_->IsReady(elementHwnd_);
}

void CaptureSession::RefreshHover() {
  if (drag_ != Drag::None || finished_) return;
  UpdateHover();
  InvalidateAll();
}

void CaptureSession::SwitchMode(CaptureMode mode) {
  if (finished_) return;
  OnBarModeChanged(mode);
}

void CaptureSession::OnBarModeChanged(CaptureMode m) {
  mode_ = m;
  drag_ = Drag::None;
  if (m == CaptureMode::Element && !tree_) tree_ = std::make_unique<ElementTree>();
  elementHwnd_ = nullptr;
  candidates_.clear();
  lastRefined_ = {};
  if (m == CaptureMode::Region && showBar_ && !hasSelection_ && !AppSettings().lastRegion.Empty()) {
    selection_ = AppSettings().lastRegion.Intersect(virt_);
    hasSelection_ = selection_.Width() >= 4 && selection_.Height() >= 4;
  }
  if (showBar_) AppSettings().lastBarMode = m;
  UpdateHover();
  InvalidateAll();
}

bool CaptureSession::CanCaptureFromBar() const {
  if (mode_ == CaptureMode::Region) return hasSelection_;
  return mode_ == CaptureMode::Screen;
}

void CaptureSession::OnBarAction(BarAction a) {
  if (a == BarAction::Cancel) {
    Cancel();
  } else if (a == BarAction::Capture && CanCaptureFromBar()) {
    if (mode_ == CaptureMode::Screen) hoverMonitor_ = barMonitor_;
    Finish();
  }
}

void CaptureSession::UpdateHover() {
  hoverWindow_ = -1;
  hoverMonitor_ = -1;
  switch (mode_) {
    case CaptureMode::Window:
      hoverWindow_ = WindowAt(windows_, cursor_);
      break;
    case CaptureMode::Screen:
      hoverMonitor_ = cursorMonitor_;
      break;
    case CaptureMode::Element:
      UpdateElementHover();
      break;
    case CaptureMode::Region:
      break;
  }
}

void CaptureSession::UpdateElementHover() {
  elementValid_ = false;
  const int wi = WindowAt(windows_, cursor_);
  if (wi < 0 || !tree_) {
    elementHwnd_ = nullptr;
    return;
  }
  const WindowInfo& w = windows_[size_t(wi)];
  tree_->Request(w.hwnd);
  std::vector<RectI> cands = tree_->CandidatesAt(w.hwnd, cursor_, w.frame);
  if (cands.empty() || !NearlyEqual(cands.back(), w.frame, 3)) cands.push_back(w.frame);
  if (w.hwnd != elementHwnd_ || cands != candidates_) {
    candidates_ = std::move(cands);
    elementHwnd_ = w.hwnd;
    const float scale = monitors_[size_t(cursorMonitor_)].Scale();
    candIndex_ = int(candidates_.size()) - 1;
    for (size_t i = 0; i < candidates_.size(); ++i) {
      if (candidates_[i].Width() >= int(28 * scale) && candidates_[i].Height() >= int(20 * scale)) {
        candIndex_ = int(i);
        break;
      }
    }
  }
  candIndex_ = std::clamp(candIndex_, 0, int(candidates_.size()) - 1);
  const RectI pick = candidates_[size_t(candIndex_)];
  if (!(pick == lastRefined_)) {
    lastRefined_ = pick;
    if (NearlyEqual(pick, w.frame, 3)) {
      elementRect_ = w.frame.Intersect(virt_);
      const int mi = MonitorAt(monitors_, {w.frame.left + w.frame.Width() / 2, w.frame.top + w.frame.Height() / 2});
      elementRadii_ = CornerRadii::Uniform(WindowCornerRadius(w, monitors_[size_t(mi)].dpi, monitors_[size_t(mi)].rect));
      elementSnapped_ = false;
    } else {
      const float scale = monitors_[size_t(cursorMonitor_)].Scale();
      RefineElement(pick, std::max(4, int(6 * scale)), true);
    }
  }
  elementValid_ = !elementRect_.Empty();
}

void CaptureSession::RefineElement(const RectI& rough, int band, bool estimateCorners) {
  const Settings& s = AppSettings();
  const bool raw = AltDown();
  RectI local = rough.Offset(-virt_.left, -virt_.top).Intersect(freeze_.Bounds());
  RectI rect = local;
  elementSnapped_ = false;
  if (s.snapEdges && !raw) {
    EdgeSnapOptions opt;
    opt.band = band;
    EdgeSnapResult res = SnapToEdges(freeze_, local, opt);
    rect = res.rect;
    elementSnapped_ = res.Any();
  }
  elementRadii_ = {};
  if (estimateCorners && s.autoCorners && !raw) {
    const float scale = monitors_[size_t(cursorMonitor_)].Scale();
    elementRadii_ = EstimateCornerRadii(freeze_, rect, int(48 * scale)).radii;
  }
  elementRect_ = rect.Offset(virt_.left, virt_.top);
}

int CaptureSession::HandleAt(POINT pt) const {
  if (!hasSelection_) return -1;
  const float scale = monitors_[size_t(MonitorAt(monitors_, pt))].Scale();
  const int tol = int(9 * scale);
  const RectI& r = selection_;
  const int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
  const POINT hs[8] = {{r.left, r.top}, {cx, r.top},       {r.right, r.top},   {r.right, cy},
                       {r.right, r.bottom}, {cx, r.bottom}, {r.left, r.bottom}, {r.left, cy}};
  for (int i = 0; i < 8; ++i) {
    if (std::abs(pt.x - hs[i].x) <= tol && std::abs(pt.y - hs[i].y) <= tol) return i;
  }
  return -1;
}

void CaptureSession::OnMouseMove(POINT pt) {
  if (finished_) return;
  const POINT prev = cursor_;
  cursor_ = pt;
  cursorMonitor_ = MonitorAt(monitors_, pt);
  switch (drag_) {
    case Drag::New: {
      if (spaceDown_) {
        dragStart_.x += pt.x - prev.x;
        dragStart_.y += pt.y - prev.y;
      }
      POINT end = pt;
      if (ShiftDown()) {
        const int side = std::max(std::abs(end.x - dragStart_.x), std::abs(end.y - dragStart_.y));
        end.x = dragStart_.x + (end.x >= dragStart_.x ? side : -side);
        end.y = dragStart_.y + (end.y >= dragStart_.y ? side : -side);
      }
      RectI r = RectI::FromPoints({dragStart_.x, dragStart_.y}, {end.x, end.y});
      r.right += 1;
      r.bottom += 1;
      selection_ = r.Intersect(virt_);
      if (selection_.Width() >= 3 || selection_.Height() >= 3) hasSelection_ = mode_ == CaptureMode::Region;
      if (mode_ == CaptureMode::Element && selection_.Width() >= 4 && selection_.Height() >= 4) {
        const float scale = monitors_[size_t(cursorMonitor_)].Scale();
        RefineElement(selection_, std::max(8, int(24 * scale)), true);
        elementValid_ = true;
      }
      break;
    }
    case Drag::Move:
      selection_ = ClampInto(dragOrig_.Offset(pt.x - dragStart_.x, pt.y - dragStart_.y), virt_);
      break;
    case Drag::Handle: {
      RectI r = dragOrig_;
      const int dx = pt.x - dragStart_.x, dy = pt.y - dragStart_.y;
      if (handle_ == 0 || handle_ == 6 || handle_ == 7) r.left += dx;
      if (handle_ == 2 || handle_ == 3 || handle_ == 4) r.right += dx;
      if (handle_ == 0 || handle_ == 1 || handle_ == 2) r.top += dy;
      if (handle_ == 4 || handle_ == 5 || handle_ == 6) r.bottom += dy;
      if (r.left > r.right - 2) std::swap(r.left, r.right);
      if (r.top > r.bottom - 2) std::swap(r.top, r.bottom);
      selection_ = r.Intersect(virt_);
      break;
    }
    case Drag::None:
      UpdateHover();
      break;
  }
  InvalidateAll();
}

void CaptureSession::OnMouseDown(POINT pt) {
  if (finished_) return;
  interacted_ = true;
  cursor_ = pt;
  altAtClick_ = AltDown();
  switch (mode_) {
    case CaptureMode::Region: {
      const int h = showBar_ ? HandleAt(pt) : -1;
      dragStart_ = pt;
      dragOrig_ = selection_;
      if (h >= 0) {
        drag_ = Drag::Handle;
        handle_ = h;
      } else if (showBar_ && hasSelection_ && selection_.Contains(pt.x, pt.y)) {
        drag_ = Drag::Move;
      } else {
        drag_ = Drag::New;
        hasSelection_ = false;
        selection_ = {pt.x, pt.y, pt.x + 1, pt.y + 1};
      }
      break;
    }
    case CaptureMode::Element:
      drag_ = Drag::New;
      dragStart_ = pt;
      selection_ = {};
      break;
    default:
      break;
  }
  InvalidateAll();
}

void CaptureSession::OnMouseUp(POINT pt) {
  if (finished_) return;
  cursor_ = pt;
  const Drag d = drag_;
  drag_ = Drag::None;
  switch (mode_) {
    case CaptureMode::Region:
      if (d == Drag::New) {
        if (selection_.Width() >= 3 && selection_.Height() >= 3) {
          hasSelection_ = true;
          if (!showBar_) Finish();
        } else {
          hasSelection_ = false;
        }
      }
      break;
    case CaptureMode::Element: {
      const bool dragged = std::abs(pt.x - dragStart_.x) >= 4 || std::abs(pt.y - dragStart_.y) >= 4;
      if (d == Drag::New && dragged) {
        if (selection_.Width() >= 4 && selection_.Height() >= 4) Finish();
      } else {
        UpdateHover();
        if (elementValid_) Finish();
      }
      selection_ = {};
      break;
    }
    case CaptureMode::Window:
      hoverWindow_ = WindowAt(windows_, pt);
      if (hoverWindow_ >= 0) Finish();
      break;
    case CaptureMode::Screen:
      hoverMonitor_ = MonitorAt(monitors_, pt);
      Finish();
      break;
  }
  InvalidateAll();
}

void CaptureSession::OnWheel(float delta) {
  if (finished_ || mode_ != CaptureMode::Element || candidates_.empty()) return;
  wheelAccum_ += delta;
  bool changed = false;
  while (wheelAccum_ >= 1.f) {
    wheelAccum_ -= 1.f;
    if (candIndex_ + 1 < int(candidates_.size())) {
      ++candIndex_;
      changed = true;
    }
  }
  while (wheelAccum_ <= -1.f) {
    wheelAccum_ += 1.f;
    if (candIndex_ > 0) {
      --candIndex_;
      changed = true;
    }
  }
  if (changed) {
    lastRefined_ = {};
    UpdateElementHover();
    InvalidateAll();
  }
}

void CaptureSession::OnKey(UINT vk, bool up, uint32_t mods) {
  if (finished_) return;
  if (vk == VK_SPACE) {
    spaceDown_ = !up;
    if (!up && drag_ == Drag::None && !showBar_) {
      if (mode_ == CaptureMode::Region) OnBarModeChanged(CaptureMode::Window);
      else if (mode_ == CaptureMode::Window) OnBarModeChanged(CaptureMode::Region);
    }
    return;
  }
  if (up) return;
  switch (vk) {
    case VK_ESCAPE:
      if (bar_.MenuOpen()) {
        bar_.CloseMenu();
        InvalidateAll();
      } else {
        Cancel();
      }
      break;
    case VK_RETURN:
      if (mode_ == CaptureMode::Region && hasSelection_) Finish();
      else if (mode_ == CaptureMode::Element && elementValid_) Finish();
      else if (mode_ == CaptureMode::Screen) {
        hoverMonitor_ = cursorMonitor_;
        Finish();
      }
      break;
    case '1': case '2': case '3': case '4':
      if (showBar_) {
        static constexpr CaptureMode kOrder[] = {CaptureMode::Screen, CaptureMode::Window, CaptureMode::Region,
                                                 CaptureMode::Element};
        OnBarModeChanged(kOrder[vk - '1']);
      }
      break;
    case VK_TAB:
      OnWheel((mods & kModShift) ? -1.f : 1.f);
      break;
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
      if (mode_ == CaptureMode::Region && hasSelection_ && drag_ == Drag::None) {
        const int step = (mods & kModShift) ? 10 : 1;
        const int dx = vk == VK_LEFT ? -step : vk == VK_RIGHT ? step : 0;
        const int dy = vk == VK_UP ? -step : vk == VK_DOWN ? step : 0;
        selection_ = ClampInto(selection_.Offset(dx, dy), virt_);
        InvalidateAll();
      }
      break;
    default:
      break;
  }
}

CaptureSession::Visual CaptureSession::GetVisual() const {
  Visual v;
  v.dragging = drag_ != Drag::None;
  switch (mode_) {
    case CaptureMode::Region:
      v.hasSelection = hasSelection_ || drag_ == Drag::New;
      v.selection = selection_;
      v.showHandles = showBar_ && hasSelection_ && drag_ == Drag::None;
      break;
    case CaptureMode::Element:
      if (drag_ == Drag::New && !selection_.Empty()) {
        v.hasSelection = true;
        v.selection = selection_;
      }
      if (elementValid_) {
        v.hasHighlight = true;
        v.highlight = elementRect_;
        v.highlightRadii = elementRadii_;
        v.elementSnapped = elementSnapped_;
      }
      break;
    case CaptureMode::Window:
      if (hoverWindow_ >= 0) {
        const WindowInfo& w = windows_[size_t(hoverWindow_)];
        v.hasHighlight = true;
        v.highlight = w.frame;
        const int mi = MonitorAt(monitors_, {w.frame.left + w.frame.Width() / 2, w.frame.top + w.frame.Height() / 2});
        v.highlightRadii =
            CornerRadii::Uniform(WindowCornerRadius(w, monitors_[size_t(mi)].dpi, monitors_[size_t(mi)].rect));
        v.windowTitle = w.title.c_str();
      }
      break;
    case CaptureMode::Screen:
      if (hoverMonitor_ >= 0) {
        v.hasHighlight = true;
        v.highlight = monitors_[size_t(hoverMonitor_)].rect;
      }
      break;
  }
  return v;
}

void CaptureSession::HideOverlays() {
  for (auto& ov : overlays_) ShowWindow(ov->hwnd(), SW_HIDE);
}

void CaptureSession::Cancel() {
  if (finished_) return;
  HideOverlays();
  Complete(std::nullopt);
}

void CaptureSession::Finish() {
  if (finished_) return;
  if (showBar_ && AppSettings().timerSeconds > 0) {
    StartTimer();
    return;
  }
  FinishNow();
}

void CaptureSession::StartTimer() {
  HideOverlays();
  const MonitorInfo& m = monitors_[size_t(barMonitor_)];
  countdown_ = std::make_unique<CountdownWindow>(
      AppSettings().timerSeconds, m,
      [this] {
        Image fresh;
        if (GrabScreen(virt_, AppSettings().showPointer, &fresh)) freeze_ = std::move(fresh);
        FinishNow();
      },
      [this] { Cancel(); });
  if (!countdown_->Init()) {
    FinishNow();
    return;
  }
  HotkeyManager::Get().SetCaptureTarget(countdown_->hwnd(), kMsgCaptureKey);
}

void CaptureSession::FinishNow() {
  if (finished_) return;
  HideOverlays();
  DwmFlush();
  std::optional<CaptureResult> r = BuildResult();
  if (!r) Log(L"Capture produced no image");
  Complete(std::move(r));
}

void CaptureSession::Complete(std::optional<CaptureResult> r) {
  finished_ = true;
  HotkeyManager::Get().SetCaptureTarget(nullptr, 0);
  if (showBar_) {
    Settings& s = AppSettings();
    s.lastBarMode = mode_;
    if (mode_ == CaptureMode::Region && hasSelection_) s.lastRegion = selection_;
    s.Save();
  }
  if (done_) done_(std::move(r));
}

std::optional<CaptureResult> CaptureSession::BuildResult() {
  const Settings& s = AppSettings();
  CaptureResult r;
  r.mode = mode_;
  auto cropGlobal = [&](const RectI& g) { return freeze_.Crop(g.Offset(-virt_.left, -virt_.top)); };

  switch (mode_) {
    case CaptureMode::Region:
      if (!hasSelection_) return std::nullopt;
      r.sourceRect = selection_;
      r.image = cropGlobal(selection_);
      break;
    case CaptureMode::Screen: {
      const int mi = hoverMonitor_ >= 0 ? hoverMonitor_ : cursorMonitor_;
      r.sourceRect = monitors_[size_t(mi)].rect;
      r.image = cropGlobal(r.sourceRect);
      break;
    }
    case CaptureMode::Window: {
      if (hoverWindow_ < 0) return std::nullopt;
      const WindowInfo& w = windows_[size_t(hoverWindow_)];
      r.sourceRect = w.frame;
      if (!CaptureWindowContent(w, &r.image)) {
        Log(L"Falling back to frozen pixels for window %p", w.hwnd);
        r.image = cropGlobal(w.frame);
      }
      const int mi = MonitorAt(monitors_, {w.frame.left + w.frame.Width() / 2, w.frame.top + w.frame.Height() / 2});
      const MonitorInfo& m = monitors_[size_t(mi)];
      const float radius = WindowCornerRadius(w, m.dpi, m.rect);
      if (radius > 0) {
        ApplyRoundedMask(r.image, CornerRadii::Uniform(radius));
        ApplyHairline(r.image, CornerRadii::Uniform(radius), PackBGRA(0, 0, 0, 34));
      }
      if (s.windowShadow && !altAtClick_) r.image = AddDropShadow(r.image, ShadowStyle::MacWindow(m.Scale()));
      break;
    }
    case CaptureMode::Element: {
      if (!elementValid_) return std::nullopt;
      r.sourceRect = elementRect_;
      r.image = cropGlobal(elementRect_);
      if (!elementRadii_.IsZero()) ApplyRoundedMask(r.image, elementRadii_);
      if (s.elementShadow && !altAtClick_) {
        const float scale = monitors_[size_t(cursorMonitor_)].Scale();
        ShadowStyle st;
        st.layers.push_back({18.f * scale, 0.f, 8.f * scale, 0.3f});
        st.layers.push_back({2.f * scale, 0.f, 1.f * scale, 0.15f});
        r.image = AddDropShadow(r.image, st);
      }
      break;
    }
  }
  if (r.image.Empty()) return std::nullopt;
  const POINT c{r.sourceRect.left + r.sourceRect.Width() / 2, r.sourceRect.top + r.sourceRect.Height() / 2};
  const MonitorInfo& m = monitors_[size_t(MonitorAt(monitors_, c))];
  r.monitorRect = m.rect;
  r.monitorDpi = m.dpi;
  return r;
}

}
