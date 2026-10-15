#include "overlay/OverlayWindow.h"

#include <windowsx.h>

#include <cstdio>
#include <map>

#include "app/Hotkeys.h"
#include "core/Strings.h"

namespace opal {
namespace {

constexpr UINT_PTR kTreeTimer = 2;

HCURSOR MakeIconCursor(Icon icon, int size) {
  static std::map<std::pair<int, int>, HCURSOR> cache;
  auto key = std::make_pair(int(icon), size);
  if (auto it = cache.find(key); it != cache.end()) return it->second;

  HCURSOR result = LoadCursorW(nullptr, IDC_CROSS);
  IWICImagingFactory* wic = Gpu::Get().Wic();
  ID2D1Factory6* f = Gpu::Get().D2DFactory();
  ComPtr<IWICBitmap> bmp;
  if (!wic || !f ||
      FAILED(wic->CreateBitmap(UINT(size), UINT(size), GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) {
    return result;
  }
  ComPtr<ID2D1RenderTarget> rt;
  D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  if (FAILED(f->CreateWicBitmapRenderTarget(bmp.Get(), props, &rt))) return result;
  ComPtr<ID2D1DeviceContext> dc;
  if (FAILED(rt.As(&dc))) return result;
  ComPtr<ID2D1SolidColorBrush> brush;
  dc->CreateSolidColorBrush({0, 0, 0, 0.55f}, &brush);
  dc->BeginDraw();
  dc->Clear({0, 0, 0, 0});
  const float s = float(size);
  const D2D1_RECT_F box{s * 0.12f, s * 0.12f, s * 0.88f, s * 0.88f};
  DrawIcon(dc.Get(), brush.Get(), icon, box, s / 7.f);
  brush->SetColor({1, 1, 1, 1});
  DrawIcon(dc.Get(), brush.Get(), icon, box, s / 18.f);
  dc->EndDraw();

  std::vector<uint32_t> px(size_t(size) * size);
  bmp->CopyPixels(nullptr, UINT(size * 4), UINT(px.size() * 4), reinterpret_cast<BYTE*>(px.data()));
  BITMAPV5HEADER bi{};
  bi.bV5Size = sizeof(bi);
  bi.bV5Width = size;
  bi.bV5Height = -size;
  bi.bV5Planes = 1;
  bi.bV5BitCount = 32;
  bi.bV5Compression = BI_BITFIELDS;
  bi.bV5RedMask = 0x00FF0000;
  bi.bV5GreenMask = 0x0000FF00;
  bi.bV5BlueMask = 0x000000FF;
  bi.bV5AlphaMask = 0xFF000000;
  void* bits = nullptr;
  HDC screen = GetDC(nullptr);
  HBITMAP color = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
  ReleaseDC(nullptr, screen);
  if (!color) return result;
  std::memcpy(bits, px.data(), px.size() * 4);
  HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
  ICONINFO ii{FALSE, DWORD(size / 2), DWORD(size / 2), mask, color};
  if (HCURSOR c = CreateIconIndirect(&ii)) result = c;
  DeleteObject(mask);
  DeleteObject(color);
  cache[key] = result;
  return result;
}

std::wstring SizeText(int w, int h) {
  wchar_t buf[64];
  swprintf_s(buf, L"%d × %d", w, h);
  return buf;
}

}

OverlayWindow::OverlayWindow(CaptureSession* session, int monitor)
    : s_(session), index_(monitor), mon_(session->Monitor(monitor).rect) {}

bool OverlayWindow::Init() {
  CreateParams p;
  p.exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
  p.style = WS_POPUP;
  p.title = L"Opal Capture";
  p.rect = ToRECT(mon_);
  return Create(p);
}

void OverlayWindow::Reveal() {
  SetWindowPos(hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

void OverlayWindow::TakeFocus() {
  HWND fg = GetForegroundWindow();
  const DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
  const DWORD me = GetCurrentThreadId();
  const bool attach = fgThread && fgThread != me && AttachThreadInput(fgThread, me, TRUE);
  SetForegroundWindow(hwnd());
  SetActiveWindow(hwnd());
  SetFocus(hwnd());
  if (attach) AttachThreadInput(fgThread, me, FALSE);
}

void OverlayWindow::OnDeviceLost() {
  freeze_.Reset();
  blur_.Reset();
  saturate_.Reset();
}

bool OverlayWindow::WantsFrame() { return false; }

bool OverlayWindow::EnsureResources(ID2D1DeviceContext5* dc) {
  if (freeze_) return true;
  const Image& img = s_->Freeze();
  const RectI& virt = s_->Virtual();
  const RectI local = mon_.Offset(-virt.left, -virt.top).Intersect(img.Bounds());
  if (local.Empty()) return false;
  D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  const uint32_t* src = img.Row(local.top) + local.left;
  if (FAILED(dc->CreateBitmap(D2D1::SizeU(UINT(local.Width()), UINT(local.Height())), src, UINT(img.width * 4),
                              &props, &freeze_))) {
    return false;
  }
  if (SUCCEEDED(dc->CreateEffect(CLSID_D2D1GaussianBlur, &blur_))) {
    blur_->SetInput(0, freeze_.Get());
    blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 26.f * Scale());
    blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
    blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, D2D1_GAUSSIANBLUR_OPTIMIZATION_SPEED);
    if (SUCCEEDED(dc->CreateEffect(CLSID_D2D1Saturation, &saturate_))) {
      saturate_->SetInputEffect(0, blur_.Get());
      saturate_->SetValue(D2D1_SATURATION_PROP_SATURATION, 1.7f);
    }
  }
  return true;
}

bool OverlayWindow::BarHasMouse() const {
  if (s_->BarMonitor() != index_) return false;
  return s_->Bar().Contains(input_.mouse) || s_->Bar().MenuOpen() || ui_.AnyActive();
}

HCURSOR OverlayWindow::Cursor() {
  if (BarHasMouse()) return LoadCursorW(nullptr, IDC_ARROW);
  const int size = int(32 * Scale());
  switch (s_->Mode()) {
    case CaptureMode::Window:
    case CaptureMode::Screen:
      return MakeIconCursor(Icon::Camera, size);
    case CaptureMode::Region: {
      if (s_->BarMode()) {
        const POINT g = ToGlobal(mousePx_);
        const int h = s_->HandleAt(g);
        if (h == 0 || h == 4) return LoadCursorW(nullptr, IDC_SIZENWSE);
        if (h == 2 || h == 6) return LoadCursorW(nullptr, IDC_SIZENESW);
        if (h == 1 || h == 5) return LoadCursorW(nullptr, IDC_SIZENS);
        if (h == 3 || h == 7) return LoadCursorW(nullptr, IDC_SIZEWE);
        const auto v = s_->GetVisual();
        if (v.hasSelection && !v.dragging && v.selection.Contains(g.x, g.y)) return LoadCursorW(nullptr, IDC_SIZEALL);
      }
      return LoadCursorW(nullptr, IDC_CROSS);
    }
    case CaptureMode::Element:
      return LoadCursorW(nullptr, IDC_CROSS);
  }
  return LoadCursorW(nullptr, IDC_ARROW);
}

LRESULT OverlayWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp, bool& handled) {
  handled = false;
  switch (msg) {
    case WM_MOUSEMOVE:
      if (!BarHasMouse() || pressedForSession_) s_->OnMouseMove(ToGlobal({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}));
      break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
      if (!BarHasMouse()) {
        pressedForSession_ = true;
        s_->OnMouseDown(ToGlobal({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}));
      }
      break;
    case WM_LBUTTONUP:
      if (pressedForSession_) {
        pressedForSession_ = false;
        s_->OnMouseUp(ToGlobal({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}));
      }
      break;
    case WM_RBUTTONUP:
      s_->Cancel();
      handled = true;
      return 0;
    case WM_MOUSEWHEEL:
      s_->OnWheel(float(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA);
      break;
    case WM_KEYDOWN:
    case WM_KEYUP:
      s_->OnKey(UINT(wp), msg == WM_KEYUP, CurrentModifiers());
      break;
    case kMsgCaptureKey:
      s_->OnKey(UINT(wp), (lp & HotkeyManager::kKeyUpFlag) != 0, uint32_t(lp & 0xFFFF));
      handled = true;
      return 0;
    case kMsgBarAction:
      if ((wp & 0xFF00) == 0xFF00) s_->OnBarModeChanged(CaptureMode(wp & 0xFF));
      else s_->OnBarAction(BarAction(wp));
      handled = true;
      return 0;
    case WM_TIMER:
      if (wp == kTreeTimer) {
        KillTimer(hwnd(), kTreeTimer);
        s_->RefreshHover();
        handled = true;
        return 0;
      }
      break;
    case WM_MOUSEACTIVATE:
      handled = true;
      return MA_ACTIVATE;
    default:
      break;
  }
  return 0;
}

void OverlayWindow::OnRender(ID2D1DeviceContext5* dc) {
  if (!EnsureResources(dc)) return;
  const SIZE sz = ClientSizePx();
  dc->DrawBitmap(freeze_.Get(), D2D1::RectF(0, 0, float(sz.cx), float(sz.cy)), 1.f,
                 D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
  const CaptureSession::Visual v = s_->GetVisual();
  switch (s_->Mode()) {
    case CaptureMode::Region:
      DrawRegion(dc, v);
      break;
    default:
      DrawHighlight(dc, v);
      break;
  }
  const bool cursorHere = s_->CursorMonitor() == index_;
  loupeLabel_.clear();
  if (cursorHere && AppSettings().showLoupe && !BarHasMouse() &&
      (s_->Mode() == CaptureMode::Region || s_->Mode() == CaptureMode::Element) && mouseInside_) {
    DrawLoupe(dc);
  }

  BeginUiFrame(dc);
  DrawBadges(v);
  if (!loupeLabel_.empty()) HudPill(loupeLabel_, loupeAnchor_, false);
  if (s_->BarMonitor() == index_) DrawBar(dc);
  EndUiFrame(dc);

  if (s_->WaitingForTree()) SetTimer(hwnd(), kTreeTimer, 120, nullptr);
}

void OverlayWindow::DrawRegion(ID2D1DeviceContext5* dc, const CaptureSession::Visual& v) {
  Painter& p = ui_.p;
  p.Begin(dc, Gpu::Get().DWrite());
  const Theme& t = Theme::Current();
  const SIZE sz = ClientSizePx();
  const float W = float(sz.cx), H = float(sz.cy), sc = Scale();
  if (!v.hasSelection) {
    if (s_->CursorMonitor() == index_ && mouseInside_ && !BarHasMouse()) {
      const float x = float(mousePx_.x) + 0.5f, y = float(mousePx_.y) + 0.5f;
      p.Line({0, y}, {W, y}, Hex(0x000000, 0.18f), 3.f);
      p.Line({x, 0}, {x, H}, Hex(0x000000, 0.18f), 3.f);
      p.Line({0, y}, {W, y}, Hex(0xFFFFFF, 0.55f), 1.f);
      p.Line({x, 0}, {x, H}, Hex(0xFFFFFF, 0.55f), 1.f);
    }
    return;
  }
  const D2D1_RECT_F sel = Local(v.selection);
  const D2D1_RECT_F clip{std::max(0.f, sel.left), std::max(0.f, sel.top), std::min(W, sel.right), std::min(H, sel.bottom)};
  if (clip.right <= clip.left || clip.bottom <= clip.top) {
    p.FillRect({0, 0, W, H}, t.dim);
    return;
  }
  p.FillRect({0, 0, W, clip.top}, t.dim);
  p.FillRect({0, clip.bottom, W, H}, t.dim);
  p.FillRect({0, clip.top, clip.left, clip.bottom}, t.dim);
  p.FillRect({clip.right, clip.top, W, clip.bottom}, t.dim);
  dc->DrawRectangle(Inset(sel, -0.5f), p.Brush(Hex(0x000000, 0.35f)), 1.f);
  dc->DrawRectangle(Inset(sel, 0.5f), p.Brush(Hex(0xFFFFFF, 0.95f)), 1.f);
  if (v.showHandles) {
    const float cx = (sel.left + sel.right) / 2, cy = (sel.top + sel.bottom) / 2;
    const D2D1_POINT_2F hs[8] = {{sel.left, sel.top},     {cx, sel.top},     {sel.right, sel.top},
                                 {sel.right, cy},         {sel.right, sel.bottom}, {cx, sel.bottom},
                                 {sel.left, sel.bottom},  {sel.left, cy}};
    for (const auto& h : hs) {
      p.FillCircle(h, 5.5f * sc, Hex(0x000000, 0.25f));
      p.FillCircle(h, 4.5f * sc, Hex(0xFFFFFF));
      dc->DrawEllipse({h, 4.5f * sc, 4.5f * sc}, p.Brush(Hex(0x000000, 0.2f)), 0.75f);
    }
  }
}

void OverlayWindow::DrawHighlight(ID2D1DeviceContext5* dc, const CaptureSession::Visual& v) {
  Painter& p = ui_.p;
  p.Begin(dc, Gpu::Get().DWrite());
  const Theme& t = Theme::Current();
  const float sc = Scale();
  if (v.hasHighlight && !v.highlight.Intersect(mon_).Empty()) {
    const D2D1_RECT_F r = Local(v.highlight);
    const CornerRadii& cr = v.highlightRadii;
    auto path = MakeRoundedPath(dc, r, cr.tl, cr.tr, cr.br, cr.bl);
    if (path) {
      dc->FillGeometry(path.Get(), p.Brush(s_->Mode() == CaptureMode::Screen ? WithAlpha(t.accent, 0.14f) : t.selection));
      if (s_->Mode() != CaptureMode::Screen) {
        auto inner = MakeRoundedPath(dc, Inset(r, 1.f * sc), std::max(0.f, cr.tl - sc), std::max(0.f, cr.tr - sc),
                                     std::max(0.f, cr.br - sc), std::max(0.f, cr.bl - sc));
        dc->DrawGeometry(inner ? inner.Get() : path.Get(), p.Brush(WithAlpha(t.accent, 0.95f)), 2.f * sc);
      }
    }
  }
  if (v.hasSelection) {
    const D2D1_RECT_F r = Local(v.selection);
    ComPtr<ID2D1StrokeStyle> dash;
    ComPtr<ID2D1Factory> f;
    dc->GetFactory(&f);
    const float dashes[] = {4.f, 3.f};
    f->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                                                     D2D1_LINE_JOIN_MITER, 10.f, D2D1_DASH_STYLE_CUSTOM, 0.f),
                         dashes, 2, &dash);
    dc->DrawRectangle(Inset(r, 0.5f), p.Brush(Hex(0x000000, 0.4f)), 1.f);
    dc->DrawRectangle(Inset(r, 0.5f), p.Brush(Hex(0xFFFFFF, 0.9f)), 1.f, dash.Get());
  }
}

void OverlayWindow::DrawLoupe(ID2D1DeviceContext5* dc) {
  Painter& p = ui_.p;
  const float sc = Scale();
  const int n = 15;
  const float cell = std::round(8.f * sc);
  const float L = cell * n;
  const SIZE sz = ClientSizePx();
  float x = float(mousePx_.x) + 26.f * sc, y = float(mousePx_.y) + 26.f * sc;
  if (x + L > sz.cx - 4) x = float(mousePx_.x) - 26.f * sc - L;
  if (y + L + 34.f * sc > sz.cy - 4) y = float(mousePx_.y) - 26.f * sc - L - 34.f * sc;
  const D2D1_RECT_F box{x, y, x + L, y + L};
  const D2D1_ELLIPSE ell{{x + L / 2, y + L / 2}, L / 2, L / 2};

  ComPtr<ID2D1Factory> f;
  dc->GetFactory(&f);
  ComPtr<ID2D1EllipseGeometry> clip;
  f->CreateEllipseGeometry(ell, &clip);
  p.FillCircle(ell.point, L / 2 + 6.f * sc, Hex(0x000000, 0.10f));
  p.FillCircle(ell.point, L / 2 + 3.f * sc, Hex(0x000000, 0.14f));
  dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), clip.Get()), nullptr);
  p.FillRect(box, Hex(0x000000));
  const D2D1_RECT_F src{float(mousePx_.x - n / 2), float(mousePx_.y - n / 2), float(mousePx_.x + n / 2 + 1),
                        float(mousePx_.y + n / 2 + 1)};
  dc->DrawBitmap(freeze_.Get(), box, 1.f, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, src);
  for (int i = 1; i < n; ++i) {
    p.Line({x + i * cell, y}, {x + i * cell, y + L}, Hex(0x000000, 0.12f), 1.f);
    p.Line({x, y + i * cell}, {x + L, y + i * cell}, Hex(0x000000, 0.12f), 1.f);
  }
  const D2D1_RECT_F mid{x + (n / 2) * cell, y + (n / 2) * cell, x + (n / 2 + 1) * cell, y + (n / 2 + 1) * cell};
  dc->DrawRectangle(Inset(mid, -0.5f), p.Brush(Hex(0x000000, 0.7f)), 1.f);
  dc->DrawRectangle(Inset(mid, 0.5f), p.Brush(Hex(0xFFFFFF)), 1.f);
  dc->PopLayer();
  dc->DrawEllipse(ell, p.Brush(Hex(0xFFFFFF, 0.95f)), 2.f * sc);

  const POINT g = ToGlobal(mousePx_);
  const Image& img = s_->Freeze();
  const RectI& virt = s_->Virtual();
  uint32_t px = 0;
  if (img.Bounds().Contains(g.x - virt.left, g.y - virt.top)) px = img.At(g.x - virt.left, g.y - virt.top);
  wchar_t buf[96];
  swprintf_s(buf, L"%d, %d   #%02X%02X%02X", mousePx_.x, mousePx_.y, ChR(px), ChG(px), ChB(px));
  loupeLabel_ = buf;
  loupeAnchor_ = {(x + L / 2) / sc, (y + L) / sc + 8.f};
}

void OverlayWindow::HudPill(const std::wstring& text, D2D1_POINT_2F anchorDip, bool above, Icon* icon) {
  Painter& p = ui_.p;
  const Theme& t = Theme::Current();
  const D2D1_SIZE_F ts = p.Measure(text, 12.f, FontWeight::Medium);
  const float iconW = icon ? 18.f : 0.f;
  const float w = ts.width + 18.f + iconW, h = 24.f;
  D2D1_RECT_F r = RX(anchorDip.x - w / 2, above ? anchorDip.y - h : anchorDip.y, w, h);
  const D2D1_SIZE_F area = ClientSizeDip();
  if (r.left < 6) r = RX(6, r.top, w, h);
  if (r.right > area.width - 6) r = RX(area.width - 6 - w, r.top, w, h);
  if (r.bottom > area.height - 6) r = RX(r.left, area.height - 6 - h, w, h);
  if (r.top < 6) r = RX(r.left, 6, w, h);
  p.SoftShadow(r, h / 2, 4.f, 0.18f, 1.f);
  p.FillRound(r, h / 2, Hex(0x1C1C1E, 0.86f));
  p.StrokeRound(r, h / 2, t.hudBorder, 1.f);
  if (icon) p.DrawIcon(*icon, RX(r.left + 9.f, r.top + 5.f, 14.f, 14.f), t.hudText, 1.4f);
  p.Text(text, RX(r.left + 9.f + iconW, r.top, w - 18.f - iconW, h), 12.f, t.hudText, FontWeight::Medium,
         HAlign::Center);
}

void OverlayWindow::DrawBadges(const CaptureSession::Visual& v) {
  const float sc = Scale();
  const auto toDip = [&](const RectI& g) {
    const D2D1_RECT_F l = Local(g);
    return D2D1_RECT_F{l.left / sc, l.top / sc, l.right / sc, l.bottom / sc};
  };
  switch (s_->Mode()) {
    case CaptureMode::Region:
      if (v.hasSelection && !v.selection.Intersect(mon_).Empty() &&
          s_->MonitorOf(v.selection) == index_) {
        const D2D1_RECT_F r = toDip(v.selection);
        HudPill(SizeText(v.selection.Width(), v.selection.Height()), {(r.left + r.right) / 2, r.bottom + 10.f},
                false);
      }
      break;
    case CaptureMode::Window:
      if (v.hasHighlight && s_->MonitorOf(v.highlight) == index_) {
        const D2D1_RECT_F r = toDip(v.highlight);
        std::wstring label = v.windowTitle && *v.windowTitle ? std::wstring(v.windowTitle) : std::wstring();
        if (label.size() > 48) label = label.substr(0, 47) + L"…";
        if (!label.empty()) label += L"   ";
        label += SizeText(v.highlight.Width(), v.highlight.Height());
        Icon cam = Icon::Camera;
        HudPill(label, {(r.left + r.right) / 2, (r.top + r.bottom) / 2 - 12.f}, false, &cam);
      }
      break;
    case CaptureMode::Element:
      if (v.hasHighlight && s_->MonitorOf(v.highlight) == index_) {
        const D2D1_RECT_F r = toDip(v.highlight);
        std::wstring label = SizeText(v.highlight.Width(), v.highlight.Height());
        const float rad = v.highlightRadii.Max();
        if (rad > 0.5f) {
          wchar_t buf[32];
          swprintf_s(buf, L"   r %.0f", rad);
          label += buf;
        }
        Icon icon = v.elementSnapped ? Icon::Sparkle : Icon::Element;
        HudPill(label, {(r.left + r.right) / 2, r.bottom + 10.f}, false, &icon);
      }
      break;
    case CaptureMode::Screen:
      if (v.hasHighlight && s_->MonitorOf(v.highlight) == index_) {
        const D2D1_SIZE_F a = ClientSizeDip();
        Icon cam = Icon::Camera;
        HudPill(SizeText(mon_.Width(), mon_.Height()), {a.width / 2, a.height / 2 - 12.f}, false, &cam);
      }
      break;
  }

  const bool hintMonitor = (s_->BarMode() ? s_->BarMonitor() : s_->CursorMonitor()) == index_;
  const bool showHint = hintMonitor && !s_->Interacted() && Now() - s_->StartTime() < 4.0;
  const float a = ui_.AnimFrom(MakeId("hint"), 0.f, showHint ? 1.f : 0.f, 6.f);
  if (a > 0.01f) {
    S hint = S::HintRegion;
    switch (s_->Mode()) {
      case CaptureMode::Region: hint = s_->BarMode() ? S::HintBarRegion : S::HintRegion; break;
      case CaptureMode::Window: hint = S::HintWindow; break;
      case CaptureMode::Element: hint = S::HintElement; break;
      case CaptureMode::Screen: hint = S::HintScreen; break;
    }
    Painter& p = ui_.p;
    const Theme& t = Theme::Current();
    const std::wstring text = T(hint);
    const D2D1_SIZE_F ts = p.Measure(text, 13.f, FontWeight::Medium);
    const D2D1_SIZE_F area = ClientSizeDip();
    const float w = ts.width + 32.f, h = 36.f;
    const D2D1_RECT_F r = RX((area.width - w) / 2, area.height - 64.f - h + (1.f - a) * 12.f, w, h);
    p.SoftShadow(r, h / 2, 8.f, 0.22f * a, 2.f);
    p.FillRound(r, h / 2, Hex(0x1C1C1E, 0.82f * a));
    p.StrokeRound(r, h / 2, WithAlpha(t.hudBorder, t.hudBorder.a * a), 1.f);
    p.Text(text, r, 13.f, WithAlpha(t.hudText, a), FontWeight::Medium, HAlign::Center);
  }
  if (showHint) ui_.RequestFrame();
}

void OverlayWindow::DrawBar(ID2D1DeviceContext5* dc) {
  CaptureBar& bar = s_->Bar();
  const D2D1_RECT_F r = bar.Layout(ui_, ClientSizeDip());
  const float sc = Scale();
  const D2D1_RECT_F px{r.left * sc, r.top * sc, r.right * sc, r.bottom * sc};
  ui_.p.SoftShadow(r, CaptureBar::kRadius, 18.f, 0.35f, 8.f);
  if (saturate_) {
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    ComPtr<ID2D1Factory> f;
    dc->GetFactory(&f);
    ComPtr<ID2D1RoundedRectangleGeometry> geo;
    f->CreateRoundedRectangleGeometry({px, CaptureBar::kRadius * sc, CaptureBar::kRadius * sc}, &geo);
    dc->PushLayer(D2D1::LayerParameters1(px, geo.Get()), nullptr);
    dc->DrawImage(saturate_.Get());
    dc->PopLayer();
    dc->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc));
  }
  CaptureMode mode = s_->Mode();
  const BarAction action = bar.Draw(ui_, &mode, s_->CanCaptureFromBar());
  if (mode != s_->Mode()) PostMessageW(hwnd(), kMsgBarAction, WPARAM(0xFF00 | int(mode)), 0);
  if (action != BarAction::None) PostMessageW(hwnd(), kMsgBarAction, WPARAM(action), 0);
}

}
