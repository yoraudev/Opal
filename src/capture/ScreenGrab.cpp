#include "capture/ScreenGrab.h"

#include <windows.h>

#include <cstring>

#include "core/Log.h"

namespace opal {

bool GrabScreen(const RectI& area, bool includeCursor, Image* out) {
  if (area.Empty()) return false;
  const int w = area.Width(), h = area.Height();
  HDC screen = GetDC(nullptr);
  HDC mem = CreateCompatibleDC(screen);
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!dib) {
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    Log(L"CreateDIBSection failed for %dx%d", w, h);
    return false;
  }
  HGDIOBJ old = SelectObject(mem, dib);
  const BOOL ok = BitBlt(mem, 0, 0, w, h, screen, area.left, area.top, SRCCOPY);
  if (ok && includeCursor) {
    CURSORINFO ci{sizeof(ci)};
    if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING) && ci.hCursor) {
      ICONINFO ii{};
      if (GetIconInfo(ci.hCursor, &ii)) {
        DrawIconEx(mem, ci.ptScreenPos.x - area.left - int(ii.xHotspot), ci.ptScreenPos.y - area.top - int(ii.yHotspot),
                   ci.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
        if (ii.hbmMask) DeleteObject(ii.hbmMask);
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
      }
    }
  }
  GdiFlush();
  if (ok) {
    *out = Image(w, h);
    const uint32_t* src = static_cast<const uint32_t*>(bits);
    for (size_t i = 0; i < out->pixels.size(); ++i) out->pixels[i] = src[i] | 0xFF000000u;
  }
  SelectObject(mem, old);
  DeleteObject(dib);
  DeleteDC(mem);
  ReleaseDC(nullptr, screen);
  if (!ok) Log(L"BitBlt failed: %lu", GetLastError());
  return ok != FALSE;
}

}
