#pragma once
#include <windows.h>

#include <string>
#include <vector>

#include "core/Geometry.h"

namespace opal {

struct WindowInfo {
  HWND hwnd = nullptr;
  RectI frame;
  RectI window;
  std::wstring title;
  bool maximized = false;
  bool popup = false;
  int cornerPreference = 0;
};

std::vector<WindowInfo> EnumerateWindows();
int WindowAt(const std::vector<WindowInfo>& windows, POINT pt);
float WindowCornerRadius(const WindowInfo& w, UINT dpi, const RectI& monitorRect);

}
