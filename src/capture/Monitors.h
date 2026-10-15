#pragma once
#include <windows.h>

#include <vector>

#include "core/Geometry.h"

namespace opal {

struct MonitorInfo {
  HMONITOR handle = nullptr;
  RectI rect;
  RectI work;
  UINT dpi = 96;
  bool primary = false;
  float Scale() const { return float(dpi) / 96.f; }
};

std::vector<MonitorInfo> EnumerateMonitors();
RectI VirtualScreen();
int MonitorAt(const std::vector<MonitorInfo>& monitors, POINT pt);
inline RectI ToRectI(const RECT& r) { return {r.left, r.top, r.right, r.bottom}; }
inline RECT ToRECT(const RectI& r) { return {r.left, r.top, r.right, r.bottom}; }

}
