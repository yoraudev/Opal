#pragma once
#include <windows.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "app/Settings.h"
#include "capture/ElementTree.h"
#include "capture/Monitors.h"
#include "capture/WindowList.h"
#include "core/Image.h"
#include "overlay/CaptureBar.h"

namespace opal {

class OverlayWindow;
class CountdownWindow;

struct CaptureResult {
  Image image;
  RectI sourceRect;
  CaptureMode mode = CaptureMode::Region;
  RectI monitorRect;
  UINT monitorDpi = 96;
};

class CaptureSession {
 public:
  using DoneFn = std::function<void(std::optional<CaptureResult>)>;

  CaptureSession(CaptureMode mode, bool showBar, DoneFn done);
  ~CaptureSession();

  bool Start();
  void SwitchMode(CaptureMode mode);
  void Cancel();
  bool Finished() const { return finished_; }

  const Image& Freeze() const { return freeze_; }
  const RectI& Virtual() const { return virt_; }
  const MonitorInfo& Monitor(int i) const { return monitors_[size_t(i)]; }
  int MonitorCount() const { return int(monitors_.size()); }
  int BarMonitor() const { return showBar_ ? barMonitor_ : -1; }
  CaptureBar& Bar() { return bar_; }
  CaptureMode Mode() const { return mode_; }
  bool BarMode() const { return showBar_; }
  POINT CursorPos() const { return cursor_; }
  int CursorMonitor() const { return cursorMonitor_; }
  int MonitorOf(const RectI& r) const {
    return opal::MonitorAt(monitors_, {r.left + r.Width() / 2, r.top + r.Height() / 2});
  }

  void OnMouseMove(POINT pt);
  void OnMouseDown(POINT pt);
  void OnMouseUp(POINT pt);
  void OnWheel(float delta);
  void OnKey(UINT vk, bool up, uint32_t mods);
  void OnBarAction(BarAction a);
  void OnBarModeChanged(CaptureMode m);
  void InvalidateAll();
  bool WaitingForTree() const;
  void RefreshHover();
  static std::optional<CaptureResult> CaptureScreenNow();

  struct Visual {
    bool dragging = false;
    bool hasSelection = false;
    RectI selection;
    bool showHandles = false;
    RectI highlight;
    CornerRadii highlightRadii;
    bool hasHighlight = false;
    bool elementSnapped = false;
    const wchar_t* windowTitle = nullptr;
  };
  Visual GetVisual() const;
  bool CanCaptureFromBar() const;
  int HandleAt(POINT pt) const;
  double StartTime() const { return startTime_; }
  bool Interacted() const { return interacted_; }

 private:
  enum class Drag { None, New, Move, Handle };

  void UpdateHover();
  void UpdateElementHover();
  void RefineElement(const RectI& rough, int band, bool estimateCorners);
  void Finish();
  void FinishNow();
  void Complete(std::optional<CaptureResult> r);
  void HideOverlays();
  std::optional<CaptureResult> BuildResult();
  void StartTimer();

  CaptureMode mode_;
  bool showBar_;
  DoneFn done_;
  bool finished_ = false;
  bool interacted_ = false;
  double startTime_ = 0;

  Image freeze_;
  RectI virt_;
  std::vector<MonitorInfo> monitors_;
  std::vector<WindowInfo> windows_;
  std::vector<std::unique_ptr<OverlayWindow>> overlays_;
  std::unique_ptr<CountdownWindow> countdown_;
  int barMonitor_ = 0;
  CaptureBar bar_;

  POINT cursor_{};
  int cursorMonitor_ = 0;
  bool spaceDown_ = false;
  bool altAtClick_ = false;

  Drag drag_ = Drag::None;
  POINT dragStart_{};
  RectI dragOrig_;
  int handle_ = -1;
  RectI selection_;
  bool hasSelection_ = false;

  int hoverWindow_ = -1;
  int hoverMonitor_ = -1;

  std::unique_ptr<ElementTree> tree_;
  HWND elementHwnd_ = nullptr;
  std::vector<RectI> candidates_;
  int candIndex_ = -1;
  float wheelAccum_ = 0;
  RectI elementRect_;
  CornerRadii elementRadii_;
  bool elementValid_ = false;
  bool elementSnapped_ = false;
  RectI lastRefined_;
};

}
