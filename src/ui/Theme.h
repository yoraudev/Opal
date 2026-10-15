#pragma once
#include <d2d1_1.h>

#include <cstdint>

namespace opal {

inline D2D1_COLOR_F Hex(uint32_t rgb, float a = 1.f) {
  return {float((rgb >> 16) & 0xFF) / 255.f, float((rgb >> 8) & 0xFF) / 255.f,
          float(rgb & 0xFF) / 255.f, a};
}
inline D2D1_COLOR_F WithAlpha(D2D1_COLOR_F c, float a) {
  c.a = a;
  return c;
}
inline D2D1_COLOR_F Mix(D2D1_COLOR_F a, D2D1_COLOR_F b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

enum class ThemePref { System, Light, Dark };

struct Theme {
  bool dark = false;
  D2D1_COLOR_F accent, accentHover, onAccent;
  D2D1_COLOR_F text, textSecondary, textTertiary;
  D2D1_COLOR_F separator;
  D2D1_COLOR_F windowBg;
  D2D1_COLOR_F sidebarBg;
  D2D1_COLOR_F contentBg;
  D2D1_COLOR_F cardBg, cardBorder;
  D2D1_COLOR_F controlBg, controlHover, controlPressed, controlBorder;
  D2D1_COLOR_F segmentBg, segmentSelected;
  D2D1_COLOR_F toggleOff, knob;
  D2D1_COLOR_F popoverBg, popoverBorder;
  D2D1_COLOR_F hudTint, hudBorder, hudText, hudTextSecondary, hudHover, hudSelected;
  D2D1_COLOR_F dim;
  D2D1_COLOR_F selection;
  D2D1_COLOR_F destructive;

  static const Theme& Current();
  static void Refresh(ThemePref pref);
  static Theme Make(bool dark);
};

bool SystemPrefersDark();

}
