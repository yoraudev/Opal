#include "ui/Theme.h"

#include <windows.h>

namespace opal {
namespace {

Theme g_theme = Theme::Make(false);

}

bool SystemPrefersDark() {
  DWORD value = 1, size = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER,
                   L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                   L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
    return false;
  }
  return value == 0;
}

const Theme& Theme::Current() { return g_theme; }

void Theme::Refresh(ThemePref pref) {
  const bool dark = pref == ThemePref::Dark || (pref == ThemePref::System && SystemPrefersDark());
  g_theme = Make(dark);
}

Theme Theme::Make(bool dark) {
  Theme t;
  t.dark = dark;
  if (dark) {
    t.accent = Hex(0x8B7CFF);
    t.accentHover = Hex(0xA195FF);
    t.onAccent = Hex(0xFFFFFF);
    t.text = Hex(0xFFFFFF, 0.88f);
    t.textSecondary = Hex(0xFFFFFF, 0.55f);
    t.textTertiary = Hex(0xFFFFFF, 0.3f);
    t.separator = Hex(0xFFFFFF, 0.09f);
    t.windowBg = Hex(0x1E1E1E);
    t.sidebarBg = Hex(0x000000, 0.18f);
    t.contentBg = Hex(0x1E1E1E, 0.55f);
    t.cardBg = Hex(0xFFFFFF, 0.045f);
    t.cardBorder = Hex(0xFFFFFF, 0.07f);
    t.controlBg = Hex(0xFFFFFF, 0.1f);
    t.controlHover = Hex(0xFFFFFF, 0.15f);
    t.controlPressed = Hex(0xFFFFFF, 0.07f);
    t.controlBorder = Hex(0xFFFFFF, 0.08f);
    t.segmentBg = Hex(0x000000, 0.25f);
    t.segmentSelected = Hex(0x5A5A5E);
    t.toggleOff = Hex(0xFFFFFF, 0.16f);
    t.knob = Hex(0xF2F2F2);
    t.popoverBg = Hex(0x2C2C2E, 0.98f);
    t.popoverBorder = Hex(0xFFFFFF, 0.12f);
    t.destructive = Hex(0xFF453A);
  } else {
    t.accent = Hex(0x6B5BF2);
    t.accentHover = Hex(0x7E70F5);
    t.onAccent = Hex(0xFFFFFF);
    t.text = Hex(0x000000, 0.85f);
    t.textSecondary = Hex(0x000000, 0.5f);
    t.textTertiary = Hex(0x000000, 0.26f);
    t.separator = Hex(0x000000, 0.08f);
    t.windowBg = Hex(0xF3F3F3);
    t.sidebarBg = Hex(0x000000, 0.035f);
    t.contentBg = Hex(0xFFFFFF, 0.55f);
    t.cardBg = Hex(0xFFFFFF, 0.75f);
    t.cardBorder = Hex(0x000000, 0.06f);
    t.controlBg = Hex(0xFFFFFF);
    t.controlHover = Hex(0xF7F7F7);
    t.controlPressed = Hex(0xE8E8E8);
    t.controlBorder = Hex(0x000000, 0.12f);
    t.segmentBg = Hex(0x000000, 0.06f);
    t.segmentSelected = Hex(0xFFFFFF);
    t.toggleOff = Hex(0x000000, 0.1f);
    t.knob = Hex(0xFFFFFF);
    t.popoverBg = Hex(0xF6F6F6, 0.98f);
    t.popoverBorder = Hex(0x000000, 0.12f);
    t.destructive = Hex(0xFF3B30);
  }
  t.hudTint = Hex(0x1C1C1E, 0.72f);
  t.hudBorder = Hex(0xFFFFFF, 0.14f);
  t.hudText = Hex(0xFFFFFF, 0.92f);
  t.hudTextSecondary = Hex(0xFFFFFF, 0.55f);
  t.hudHover = Hex(0xFFFFFF, 0.1f);
  t.hudSelected = Hex(0xFFFFFF, 0.2f);
  t.dim = Hex(0x000000, 0.32f);
  t.selection = WithAlpha(t.accent, 0.22f);
  return t;
}

}
