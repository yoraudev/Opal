#include "capture/Monitors.h"

#include <shellscalingapi.h>

#include <climits>

namespace opal {

std::vector<MonitorInfo> EnumerateMonitors() {
  std::vector<MonitorInfo> out;
  EnumDisplayMonitors(
      nullptr, nullptr,
      [](HMONITOR mon, HDC, LPRECT, LPARAM lp) -> BOOL {
        auto* list = reinterpret_cast<std::vector<MonitorInfo>*>(lp);
        MONITORINFO mi{sizeof(mi)};
        if (!GetMonitorInfoW(mon, &mi)) return TRUE;
        MonitorInfo m;
        m.handle = mon;
        m.rect = ToRectI(mi.rcMonitor);
        m.work = ToRectI(mi.rcWork);
        m.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        UINT dx = 96, dy = 96;
        if (SUCCEEDED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy))) m.dpi = dx;
        list->push_back(m);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&out));
  return out;
}

RectI VirtualScreen() {
  const int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
  return RectI::FromXYWH(x, y, GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
}

int MonitorAt(const std::vector<MonitorInfo>& monitors, POINT pt) {
  int best = 0;
  long long bestDist = LLONG_MAX;
  for (size_t i = 0; i < monitors.size(); ++i) {
    const RectI& r = monitors[i].rect;
    if (r.Contains(pt.x, pt.y)) return int(i);
    const long long dx = pt.x < r.left ? r.left - pt.x : (pt.x >= r.right ? pt.x - r.right + 1 : 0);
    const long long dy = pt.y < r.top ? r.top - pt.y : (pt.y >= r.bottom ? pt.y - r.bottom + 1 : 0);
    if (dx * dx + dy * dy < bestDist) {
      bestDist = dx * dx + dy * dy;
      best = int(i);
    }
  }
  return best;
}

}
