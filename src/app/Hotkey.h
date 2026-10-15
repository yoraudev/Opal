#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opal {

enum HotkeyMod : uint32_t { kModCtrl = 1, kModAlt = 2, kModShift = 4, kModWin = 8 };

struct Hotkey {
  uint32_t mods = 0;
  uint32_t vk = 0;
  bool Valid() const { return vk != 0; }
  bool operator==(const Hotkey&) const = default;
};

std::wstring FormatHotkey(const Hotkey& hk);
std::optional<Hotkey> ParseHotkey(std::wstring_view text);
std::vector<std::wstring> HotkeyParts(const Hotkey& hk);
std::wstring KeyName(uint32_t vk);
bool IsModifierKey(uint32_t vk);

}
