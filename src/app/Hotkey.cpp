#include "app/Hotkey.h"

#include <windows.h>

#include <cwctype>

namespace opal {
namespace {

struct NamedKey {
  uint32_t vk;
  const wchar_t* name;
};

constexpr NamedKey kNamedKeys[] = {
    {VK_SNAPSHOT, L"PrintScreen"}, {VK_SPACE, L"Space"},       {VK_RETURN, L"Enter"},
    {VK_TAB, L"Tab"},              {VK_ESCAPE, L"Esc"},        {VK_BACK, L"Backspace"},
    {VK_INSERT, L"Insert"},        {VK_DELETE, L"Delete"},     {VK_HOME, L"Home"},
    {VK_END, L"End"},              {VK_PRIOR, L"PageUp"},      {VK_NEXT, L"PageDown"},
    {VK_LEFT, L"Left"},            {VK_RIGHT, L"Right"},       {VK_UP, L"Up"},
    {VK_DOWN, L"Down"},            {VK_PAUSE, L"Pause"},       {VK_SCROLL, L"ScrollLock"},
    {VK_OEM_3, L"`"},              {VK_OEM_MINUS, L"-"},       {VK_OEM_PLUS, L"="},
    {VK_OEM_4, L"["},              {VK_OEM_6, L"]"},           {VK_OEM_5, L"\\"},
    {VK_OEM_1, L";"},              {VK_OEM_7, L"'"},           {VK_OEM_COMMA, L","},
    {VK_OEM_PERIOD, L"."},         {VK_OEM_2, L"/"},           {VK_MULTIPLY, L"Num*"},
    {VK_ADD, L"Num+"},             {VK_SUBTRACT, L"Num-"},     {VK_DIVIDE, L"Num/"},
    {VK_DECIMAL, L"Num."},
};

bool IEquals(std::wstring_view a, std::wstring_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::towlower(a[i]) != std::towlower(b[i])) return false;
  }
  return true;
}

std::optional<uint32_t> ParseKey(std::wstring_view s) {
  if (s.size() == 1) {
    wchar_t c = wchar_t(std::towupper(s[0]));
    if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) return uint32_t(c);
  }
  for (const auto& nk : kNamedKeys) {
    if (IEquals(s, nk.name)) return nk.vk;
  }
  if (IEquals(s, L"PrtSc") || IEquals(s, L"PrtScn")) return uint32_t(VK_SNAPSHOT);
  if (s.size() >= 2 && (s[0] == L'F' || s[0] == L'f')) {
    int n = 0;
    for (size_t i = 1; i < s.size(); ++i) {
      if (s[i] < L'0' || s[i] > L'9') return std::nullopt;
      n = n * 10 + (s[i] - L'0');
    }
    if (n >= 1 && n <= 24) return uint32_t(VK_F1 + n - 1);
    return std::nullopt;
  }
  if (s.size() == 4 && IEquals(s.substr(0, 3), L"Num") && s[3] >= L'0' && s[3] <= L'9') {
    return uint32_t(VK_NUMPAD0 + (s[3] - L'0'));
  }
  return std::nullopt;
}

}

bool IsModifierKey(uint32_t vk) {
  switch (vk) {
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_MENU: case VK_LMENU: case VK_RMENU:
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case VK_LWIN: case VK_RWIN:
      return true;
    default:
      return false;
  }
}

std::wstring KeyName(uint32_t vk) {
  if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::wstring(1, wchar_t(vk));
  if (vk >= VK_F1 && vk <= VK_F24) return L"F" + std::to_wstring(vk - VK_F1 + 1);
  if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return L"Num" + std::to_wstring(vk - VK_NUMPAD0);
  for (const auto& nk : kNamedKeys) {
    if (nk.vk == vk) return nk.name;
  }
  return L"#" + std::to_wstring(vk);
}

std::vector<std::wstring> HotkeyParts(const Hotkey& hk) {
  std::vector<std::wstring> parts;
  if (!hk.Valid()) return parts;
  if (hk.mods & kModWin) parts.push_back(L"Win");
  if (hk.mods & kModCtrl) parts.push_back(L"Ctrl");
  if (hk.mods & kModAlt) parts.push_back(L"Alt");
  if (hk.mods & kModShift) parts.push_back(L"Shift");
  parts.push_back(KeyName(hk.vk));
  return parts;
}

std::wstring FormatHotkey(const Hotkey& hk) {
  std::wstring out;
  for (const auto& p : HotkeyParts(hk)) {
    if (!out.empty()) out += L'+';
    out += p;
  }
  return out;
}

std::optional<Hotkey> ParseHotkey(std::wstring_view text) {
  Hotkey hk;
  while (!text.empty() && std::iswspace(text.front())) text.remove_prefix(1);
  while (!text.empty() && std::iswspace(text.back())) text.remove_suffix(1);
  if (text.empty()) return hk;
  size_t start = 0;
  for (;;) {
    size_t plus = text.find(L'+', start);
    std::wstring_view tok =
        text.substr(start, plus == std::wstring_view::npos ? std::wstring_view::npos : plus - start);
    if (tok.empty()) return std::nullopt;
    if (IEquals(tok, L"Ctrl") || IEquals(tok, L"Control")) {
      hk.mods |= kModCtrl;
    } else if (IEquals(tok, L"Alt")) {
      hk.mods |= kModAlt;
    } else if (IEquals(tok, L"Shift")) {
      hk.mods |= kModShift;
    } else if (IEquals(tok, L"Win")) {
      hk.mods |= kModWin;
    } else {
      if (hk.vk != 0) return std::nullopt;
      auto vk = ParseKey(tok);
      if (!vk) return std::nullopt;
      hk.vk = *vk;
    }
    if (plus == std::wstring_view::npos) break;
    start = plus + 1;
  }
  if (hk.vk == 0) return std::nullopt;
  return hk;
}

}
