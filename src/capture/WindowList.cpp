#include "capture/WindowList.h"

#include <dwmapi.h>

#include "capture/Monitors.h"

namespace opal {
namespace {

struct EnumCtx {
  std::vector<WindowInfo>* out;
  DWORD selfPid;
};

bool IsCloaked(HWND h) {
  DWORD cloaked = 0;
  return SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0;
}

BOOL CALLBACK EnumProc(HWND h, LPARAM lp) {
  auto* ctx = reinterpret_cast<EnumCtx*>(lp);
  if (!IsWindowVisible(h) || IsIconic(h) || IsCloaked(h)) return TRUE;
  DWORD pid = 0;
  GetWindowThreadProcessId(h, &pid);
  const LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
  const LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE);
  if (pid == ctx->selfPid && (ex & WS_EX_TOOLWINDOW)) return TRUE;
  if (ex & WS_EX_TRANSPARENT) return TRUE;
  wchar_t cls[64] = {};
  GetClassNameW(h, cls, 64);
  if (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0) return TRUE;

  WindowInfo w;
  w.hwnd = h;
  RECT r;
  if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(h, &r);
  w.frame = ToRectI(r);
  GetWindowRect(h, &r);
  w.window = ToRectI(r);
  if (w.frame.Width() < 8 || w.frame.Height() < 8) return TRUE;
  if (ex & WS_EX_LAYERED) {
    BYTE alpha = 255;
    DWORD flags = 0;
    if (GetLayeredWindowAttributes(h, nullptr, &alpha, &flags) && (flags & LWA_ALPHA) && alpha == 0) return TRUE;
  }
  wchar_t title[256] = {};
  GetWindowTextW(h, title, 256);
  w.title = title;
  w.maximized = IsZoomed(h) != FALSE;
  w.popup = (style & WS_POPUP) && !(style & WS_CAPTION);
  DwmGetWindowAttribute(h, DWMWA_WINDOW_CORNER_PREFERENCE, &w.cornerPreference, sizeof(int));
  ctx->out->push_back(std::move(w));
  return TRUE;
}

}

std::vector<WindowInfo> EnumerateWindows() {
  std::vector<WindowInfo> out;
  EnumCtx ctx{&out, GetCurrentProcessId()};
  EnumWindows(&EnumProc, reinterpret_cast<LPARAM>(&ctx));
  return out;
}

int WindowAt(const std::vector<WindowInfo>& windows, POINT pt) {
  for (size_t i = 0; i < windows.size(); ++i) {
    if (windows[i].frame.Contains(pt.x, pt.y)) return int(i);
  }
  return -1;
}

static bool IsWindows11() {
  static const bool v = [] {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW vi{sizeof(vi)};
    return fn && fn(&vi) == 0 && vi.dwBuildNumber >= 22000;
  }();
  return v;
}

float WindowCornerRadius(const WindowInfo& w, UINT dpi, const RectI& monitorRect) {
  if (!IsWindows11() || w.maximized || w.frame == monitorRect) return 0.f;
  const float scale = float(dpi) / 96.f;
  switch (w.cornerPreference) {
    case DWMWCP_DONOTROUND:
      return 0.f;
    case DWMWCP_ROUNDSMALL:
      return 4.f * scale;
    default:
      return 8.f * scale;
  }
}

}
