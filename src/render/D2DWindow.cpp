#include "render/D2DWindow.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <cstring>

#include "core/Log.h"

namespace opal {
namespace {

constexpr wchar_t kClassName[] = L"OpalD2DWindow";
constexpr UINT_PTR kWakeTimer = 0x0FA1;
constexpr UINT kMsgDestroyed = WM_APP + 0x3F0;
constexpr UINT kMsgCloseSelf = WM_APP + 0x3F1;
constexpr UINT kMsgAfterFrame = WM_APP + 0x3F2;
std::function<void(D2DWindow*)> g_destroyed;
HWND g_reaper = nullptr;

LRESULT CALLBACK ReaperProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == kMsgDestroyed) {
    if (g_destroyed) g_destroyed(reinterpret_cast<D2DWindow*>(lp));
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

void RegisterClasses() {
  static bool done = false;
  if (done) return;
  done = true;
  WNDCLASSEXW wc{sizeof(wc)};
  wc.style = CS_DBLCLKS;
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = nullptr;
  wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
  wc.lpszClassName = kClassName;
  RegisterClassExW(&wc);

  WNDCLASSEXW rc{sizeof(rc)};
  rc.lpfnWndProc = ReaperProc;
  rc.hInstance = wc.hInstance;
  rc.lpszClassName = L"OpalReaper";
  RegisterClassExW(&rc);
  g_reaper = CreateWindowExW(0, L"OpalReaper", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
}

}

void ExcludeFromCapture(HWND hwnd) {
  static const bool show = [] {
    wchar_t v[8] = {};
    return GetEnvironmentVariableW(L"OPAL_SHOW_IN_CAPTURES", v, 8) > 0 && v[0] == L'1';
  }();
  if (!show) SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
}

void D2DWindow::SetDestroyedHandler(std::function<void(D2DWindow*)> fn) { g_destroyed = std::move(fn); }

D2DWindow::D2DWindow() = default;

D2DWindow::~D2DWindow() {
  if (hwnd_) {
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  ReleaseTarget();
  if (gpuRef_) Gpu::Get().Release();
}

double D2DWindow::Now() {
  static LARGE_INTEGER freq = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f;
  }();
  LARGE_INTEGER c;
  QueryPerformanceCounter(&c);
  return double(c.QuadPart) / double(freq.QuadPart);
}

bool D2DWindow::MicaSupported() {
  static const bool v = [] {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW vi{sizeof(vi)};
    return fn && fn(&vi) == 0 && vi.dwBuildNumber >= 22621;
  }();
  return v;
}

bool D2DWindow::Create(const CreateParams& p) {
  RegisterClasses();
  customFrame_ = p.customFrame;
  resizable_ = (p.style & WS_THICKFRAME) != 0;
  Gpu::Get().AddRef();
  gpuRef_ = true;
  hwnd_ = CreateWindowExW(p.exStyle | WS_EX_NOREDIRECTIONBITMAP, kClassName, p.title, p.style, p.rect.left,
                          p.rect.top, p.rect.right - p.rect.left, p.rect.bottom - p.rect.top, p.owner, nullptr,
                          GetModuleHandleW(nullptr), nullptr);
  if (!hwnd_) {
    Log(L"CreateWindowEx failed: %lu", GetLastError());
    return false;
  }
  SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  SetWindowLongPtrW(hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProcStatic));
  dpi_ = GetDpiForWindow(hwnd_);
  if (customFrame_) {
    MARGINS m = p.mica ? MARGINS{-1, -1, -1, -1} : MARGINS{0, 0, 1, 0};
    DwmExtendFrameIntoClientArea(hwnd_, &m);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  }
  if (p.mica && MicaSupported()) {
    const int backdrop = DWMSBT_MAINWINDOW;
    DwmSetWindowAttribute(hwnd_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
  }
  RECT rc;
  GetClientRect(hwnd_, &rc);
  size_ = {rc.right - rc.left, rc.bottom - rc.top};
  return true;
}

void D2DWindow::ApplyDarkFrame(bool dark) {
  if (!hwnd_) return;
  BOOL v = dark ? TRUE : FALSE;
  DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &v, sizeof(v));
}

void D2DWindow::Invalidate() {
  if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void D2DWindow::RenderNow() {
  if (hwnd_) Render();
}

void D2DWindow::AfterFrame(std::function<void()> fn) {
  afterFrame_.push_back(std::move(fn));
  if (hwnd_ && afterFrame_.size() == 1) PostMessageW(hwnd_, kMsgAfterFrame, 0, 0);
}

void D2DWindow::Close() {
  if (!hwnd_) return;
  if (modal_ > 0) {
    closePending_ = true;
    return;
  }
  PostMessageW(hwnd_, kMsgCloseSelf, 0, 0);
}

D2DWindow::ModalScope::ModalScope(D2DWindow* w) : w_(w) { ++w_->modal_; }

D2DWindow::ModalScope::~ModalScope() {
  if (--w_->modal_ > 0 || !w_->closePending_) return;
  w_->closePending_ = false;
  if (w_->hwnd_) PostMessageW(w_->hwnd_, kMsgCloseSelf, 0, 0);
}

bool D2DWindow::Snapshot(SIZE sizePx, UINT dpi, Image* out) {
  ComPtr<ID2D1DeviceContext5> dc = Gpu::Get().CreateContext();
  if (!dc || sizePx.cx <= 0 || sizePx.cy <= 0) return false;
  const D2D1_PIXEL_FORMAT fmt = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
  const D2D1_SIZE_U size = D2D1::SizeU(UINT(sizePx.cx), UINT(sizePx.cy));
  ComPtr<ID2D1Bitmap1> target, readback;
  if (FAILED(dc->CreateBitmap(size, nullptr, 0, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, fmt), &target)) ||
      FAILED(dc->CreateBitmap(size, nullptr, 0,
                              D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, fmt),
                              &readback))) {
    return false;
  }
  const SIZE oldSize = size_;
  const UINT oldDpi = dpi_;
  size_ = sizePx;
  dpi_ = dpi;
  dc->SetTarget(target.Get());
  dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  dc->BeginDraw();
  dc->SetTransform(D2D1::Matrix3x2F::Identity());
  dc->Clear(ClearColor());
  OnRender(dc.Get());
  const HRESULT hr = dc->EndDraw();
  input_.ClearEdges();
  size_ = oldSize;
  dpi_ = oldDpi;
  if (FAILED(hr) || FAILED(readback->CopyFromBitmap(nullptr, target.Get(), nullptr))) return false;
  D2D1_MAPPED_RECT map{};
  if (FAILED(readback->Map(D2D1_MAP_OPTIONS_READ, &map))) return false;
  *out = Image(sizePx.cx, sizePx.cy);
  for (int y = 0; y < sizePx.cy; ++y) {
    std::memcpy(out->Row(y), map.bits + size_t(y) * map.pitch, size_t(sizePx.cx) * 4);
  }
  readback->Unmap();
  return true;
}

bool D2DWindow::EnsureTarget() {
  Gpu& gpu = Gpu::Get();
  if (!gpu.Ensure()) return false;
  if (gen_ != gpu.Generation()) {
    ReleaseTarget();
    OnDeviceLost();
    gen_ = gpu.Generation();
  }
  if (size_.cx <= 0 || size_.cy <= 0) return false;
  if (!swap_) {
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(gpu.Dxgi()->GetAdapter(&adapter)) || FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
      Log(L"DXGI factory unavailable");
      return false;
    }
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = UINT(size_.cx);
    desc.Height = UINT(size_.cy);
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    HRESULT hr = factory->CreateSwapChainForComposition(gpu.D3D(), &desc, nullptr, &swap_);
    if (FAILED(hr)) {
      Log(L"CreateSwapChainForComposition failed 0x%08X", hr);
      return false;
    }
    hr = gpu.DComp()->CreateTargetForHwnd(hwnd_, TRUE, &target_);
    if (SUCCEEDED(hr)) hr = gpu.DComp()->CreateVisual(&visual_);
    if (SUCCEEDED(hr)) hr = visual_->SetContent(swap_.Get());
    if (SUCCEEDED(hr)) hr = target_->SetRoot(visual_.Get());
    if (SUCCEEDED(hr)) hr = gpu.DComp()->Commit();
    if (FAILED(hr)) {
      Log(L"DComp setup failed 0x%08X", hr);
      ReleaseTarget();
      return false;
    }
    if (FAILED(gpu.D2DDevice()->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, dc_.GetAddressOf()))) {
      Log(L"D2D device context failed");
      ReleaseTarget();
      return false;
    }
  }
  if (!backBitmap_) {
    ComPtr<IDXGISurface> surface;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&surface)))) return false;
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.f, 96.f);
    if (FAILED(dc_->CreateBitmapFromDxgiSurface(surface.Get(), &props, &backBitmap_))) return false;
    dc_->SetTarget(backBitmap_.Get());
    dc_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  }
  return true;
}

void D2DWindow::ReleaseTarget() {
  if (dc_) dc_->SetTarget(nullptr);
  backBitmap_.Reset();
  dc_.Reset();
  if (target_) target_->SetRoot(nullptr);
  visual_.Reset();
  target_.Reset();
  swap_.Reset();
}

void D2DWindow::Render() {
  if (!EnsureTarget()) return;
  dc_->BeginDraw();
  dc_->SetTransform(D2D1::Matrix3x2F::Identity());
  dc_->Clear(ClearColor());
  OnRender(dc_.Get());
  HRESULT hr = dc_->EndDraw();
  input_.ClearEdges();
  if (hr == D2DERR_RECREATE_TARGET) {
    Gpu::Get().HandleDeviceLost();
    Invalidate();
    return;
  }
  hr = swap_->Present(1, 0);
  if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
    Gpu::Get().HandleDeviceLost();
    Invalidate();
    return;
  }
  if (frameRequested_ || WantsFrame()) {
    frameRequested_ = false;
    Invalidate();
  }
}

void D2DWindow::BeginUiFrame(ID2D1DeviceContext5* dc) {
  const float s = Scale();
  dc->SetTransform(D2D1::Matrix3x2F::Scale(s, s));
  ui_.Begin(dc, Gpu::Get().DWrite(), Theme::Current(), input_, Now());
}

void D2DWindow::EndUiFrame(ID2D1DeviceContext5* dc) {
  if (ui_.End()) frameRequested_ = true;
  const double wake = ui_.WakeIn();
  if (wake > 0 && hwnd_) SetTimer(hwnd_, kWakeTimer, UINT(wake * 1000.0) + 10, nullptr);
  dc->SetTransform(D2D1::Matrix3x2F::Identity());
}

void D2DWindow::UpdateModifiers() {
  input_.ctrl = GetKeyState(VK_CONTROL) < 0;
  input_.shift = GetKeyState(VK_SHIFT) < 0;
  input_.alt = GetKeyState(VK_MENU) < 0;
}

LRESULT D2DWindow::HandleMessage(UINT, WPARAM, LPARAM, bool& handled) {
  handled = false;
  return 0;
}

LRESULT CALLBACK D2DWindow::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto* self = reinterpret_cast<D2DWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
  return self->WndProc(msg, wp, lp);
}

LRESULT D2DWindow::WndProc(UINT msg, WPARAM wp, LPARAM lp) {
  const float s = Scale();
  switch (msg) {
    case WM_MOUSEMOVE:
      mousePx_ = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      input_.mouse = {mousePx_.x / s, mousePx_.y / s};
      mouseInside_ = true;
      if (!tracking_) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
        tracking_ = TrackMouseEvent(&tme) != FALSE;
      }
      UpdateModifiers();
      Invalidate();
      break;
    case WM_MOUSELEAVE:
      tracking_ = false;
      mouseInside_ = false;
      if (GetCapture() != hwnd_) input_.mouse = {-1e6f, -1e6f};
      Invalidate();
      break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
      mousePx_ = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      input_.mouse = {mousePx_.x / s, mousePx_.y / s};
      SetCapture(hwnd_);
      input_.pressed = true;
      input_.mouseDown = true;
      if (msg == WM_LBUTTONDBLCLK) input_.doubleClick = true;
      UpdateModifiers();
      Invalidate();
      break;
    case WM_LBUTTONUP:
      mousePx_ = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      input_.mouse = {mousePx_.x / s, mousePx_.y / s};
      if (GetCapture() == hwnd_) ReleaseCapture();
      input_.released = true;
      input_.mouseDown = false;
      UpdateModifiers();
      Invalidate();
      break;
    case WM_CAPTURECHANGED:
      if (input_.mouseDown) {
        input_.mouseDown = false;
        input_.released = true;
        Invalidate();
      }
      break;
    case WM_RBUTTONDOWN:
      input_.rightPressed = true;
      Invalidate();
      break;
    case WM_MOUSEWHEEL:
      input_.wheel += float(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA;
      Invalidate();
      break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
      input_.keys.push_back(unsigned(wp));
      UpdateModifiers();
      Invalidate();
      break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
      UpdateModifiers();
      Invalidate();
      break;
    case WM_CHAR:
      input_.chars.push_back(wchar_t(wp));
      Invalidate();
      break;
    default:
      break;
  }

  bool handled = false;
  LRESULT r = HandleMessage(msg, wp, lp, handled);
  if (handled || !hwnd_) return r;

  switch (msg) {
    case kMsgCloseSelf:
    case WM_CLOSE:
      if (modal_ > 0) {
        closePending_ = true;
        return 0;
      }
      DestroyWindow(hwnd_);
      return 0;
    case kMsgAfterFrame: {
      auto work = std::move(afterFrame_);
      afterFrame_.clear();
      for (auto& fn : work) {
        if (!hwnd_) break;
        fn();
      }
      Invalidate();
      return 0;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd_, &ps);
      EndPaint(hwnd_, &ps);
      Render();
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_TIMER:
      if (wp == kWakeTimer) {
        KillTimer(hwnd_, kWakeTimer);
        Invalidate();
        return 0;
      }
      break;
    case WM_SIZE: {
      SIZE ns{LOWORD(lp), HIWORD(lp)};
      if (ns.cx != size_.cx || ns.cy != size_.cy) {
        size_ = ns;
        if (swap_ && ns.cx > 0 && ns.cy > 0) {
          if (dc_) dc_->SetTarget(nullptr);
          backBitmap_.Reset();
          HRESULT hr = swap_->ResizeBuffers(0, UINT(ns.cx), UINT(ns.cy), DXGI_FORMAT_UNKNOWN, 0);
          if (FAILED(hr)) ReleaseTarget();
        }
        Render();
      }
      return 0;
    }
    case WM_DPICHANGED: {
      dpi_ = HIWORD(wp);
      const RECT* nr = reinterpret_cast<const RECT*>(lp);
      SetWindowPos(hwnd_, nullptr, nr->left, nr->top, nr->right - nr->left, nr->bottom - nr->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      OnDpiChanged();
      Invalidate();
      return 0;
    }
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT) {
        SetCursor(Cursor());
        return TRUE;
      }
      break;
    case WM_NCCALCSIZE:
      if (customFrame_ && wp) {
        auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
        if (IsZoomed(hwnd_)) {
          const int fx = GetSystemMetricsForDpi(SM_CXFRAME, dpi_) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi_);
          const int fy = GetSystemMetricsForDpi(SM_CYFRAME, dpi_) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi_);
          params->rgrc[0].left += fx;
          params->rgrc[0].right -= fx;
          params->rgrc[0].top += fy;
          params->rgrc[0].bottom -= fy;
        }
        return 0;
      }
      break;
    case WM_NCHITTEST:
      if (customFrame_) {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT wr;
        GetWindowRect(hwnd_, &wr);
        if (resizable_ && !IsZoomed(hwnd_)) {
          const int b = int(6 * s);
          const bool l = pt.x < wr.left + b, rr = pt.x >= wr.right - b;
          const bool t = pt.y < wr.top + b, bt = pt.y >= wr.bottom - b;
          if (t && l) return HTTOPLEFT;
          if (t && rr) return HTTOPRIGHT;
          if (bt && l) return HTBOTTOMLEFT;
          if (bt && rr) return HTBOTTOMRIGHT;
          if (l) return HTLEFT;
          if (rr) return HTRIGHT;
          if (t) return HTTOP;
          if (bt) return HTBOTTOM;
        }
        POINT cp = pt;
        ScreenToClient(hwnd_, &cp);
        return HitTestClient(cp);
      }
      break;
    case WM_NCDESTROY: {
      ReleaseTarget();
      if (gpuRef_) {
        Gpu::Get().Release();
        gpuRef_ = false;
      }
      SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
      HWND h = hwnd_;
      hwnd_ = nullptr;
      OnDestroyed();
      if (g_reaper) PostMessageW(g_reaper, kMsgDestroyed, 0, reinterpret_cast<LPARAM>(this));
      return DefWindowProcW(h, msg, wp, lp);
    }
    default:
      break;
  }
  return DefWindowProcW(hwnd_, msg, wp, lp);
}

}
