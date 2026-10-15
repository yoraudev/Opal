#pragma once
#include "overlay/CaptureSession.h"
#include "render/D2DWindow.h"

namespace opal {

constexpr UINT kMsgCaptureKey = WM_APP + 0x120;
constexpr UINT kMsgBarAction = WM_APP + 0x121;

class OverlayWindow : public D2DWindow {
 public:
  OverlayWindow(CaptureSession* session, int monitor);
  bool Init();
  void Reveal();
  void TakeFocus();

 protected:
  void OnRender(ID2D1DeviceContext5* dc) override;
  LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp, bool& handled) override;
  HCURSOR Cursor() override;
  void OnDeviceLost() override;
  bool WantsFrame() override;

 private:
  bool EnsureResources(ID2D1DeviceContext5* dc);
  bool BarHasMouse() const;
  POINT ToGlobal(POINT client) const { return {client.x + mon_.left, client.y + mon_.top}; }
  D2D1_RECT_F Local(const RectI& g) const {
    return {float(g.left - mon_.left), float(g.top - mon_.top), float(g.right - mon_.left), float(g.bottom - mon_.top)};
  }
  void DrawRegion(ID2D1DeviceContext5* dc, const CaptureSession::Visual& v);
  void DrawHighlight(ID2D1DeviceContext5* dc, const CaptureSession::Visual& v);
  void DrawLoupe(ID2D1DeviceContext5* dc);
  void DrawBadges(const CaptureSession::Visual& v);
  void DrawBar(ID2D1DeviceContext5* dc);
  void HudPill(const std::wstring& text, D2D1_POINT_2F anchorDip, bool above, Icon* icon = nullptr);

  CaptureSession* s_;
  int index_;
  RectI mon_;
  bool pressedForSession_ = false;
  std::wstring loupeLabel_;
  D2D1_POINT_2F loupeAnchor_{};
  ComPtr<ID2D1Bitmap1> freeze_;
  ComPtr<ID2D1Effect> blur_, saturate_;
  unsigned resGen_ = 0;
};

}
