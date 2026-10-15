#pragma once
#include <d2d1_3.h>
#include <dwrite_3.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ui/Icons.h"
#include "ui/Theme.h"

namespace opal {

using Microsoft::WRL::ComPtr;

inline D2D1_RECT_F RX(float x, float y, float w, float h) { return {x, y, x + w, y + h}; }
inline float RW(const D2D1_RECT_F& r) { return r.right - r.left; }
inline float RH(const D2D1_RECT_F& r) { return r.bottom - r.top; }
inline bool InRect(const D2D1_RECT_F& r, D2D1_POINT_2F p) {
  return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}
inline D2D1_RECT_F Inset(const D2D1_RECT_F& r, float dx, float dy) {
  return {r.left + dx, r.top + dy, r.right - dx, r.bottom - dy};
}
inline D2D1_RECT_F Inset(const D2D1_RECT_F& r, float d) { return Inset(r, d, d); }
inline D2D1_POINT_2F Center(const D2D1_RECT_F& r) {
  return {(r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f};
}
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

ComPtr<ID2D1PathGeometry> MakeRoundedPath(ID2D1DeviceContext* dc, const D2D1_RECT_F& r, float tl, float tr,
                                          float br, float bl);

enum class FontWeight { Regular = 400, Medium = 500, Semibold = 600, Bold = 700 };
enum class HAlign { Left, Center, Right };
enum class VAlign { Top, Center, Bottom };

class Painter {
 public:
  void Begin(ID2D1DeviceContext5* dc, IDWriteFactory3* dw);
  ID2D1DeviceContext5* dc() const { return dc_; }
  IDWriteFactory3* dwrite() const { return dw_; }
  ID2D1SolidColorBrush* Brush(const D2D1_COLOR_F& c);

  void FillRect(const D2D1_RECT_F& r, const D2D1_COLOR_F& c);
  void FillRound(const D2D1_RECT_F& r, float rad, const D2D1_COLOR_F& c);
  void StrokeRound(const D2D1_RECT_F& r, float rad, const D2D1_COLOR_F& c, float w = 1.f);
  void FillCircle(D2D1_POINT_2F c, float rad, const D2D1_COLOR_F& col);
  void Line(D2D1_POINT_2F a, D2D1_POINT_2F b, const D2D1_COLOR_F& c, float w = 1.f);
  void Text(std::wstring_view s, const D2D1_RECT_F& r, float size, const D2D1_COLOR_F& c,
            FontWeight w = FontWeight::Regular, HAlign h = HAlign::Left, VAlign v = VAlign::Center,
            bool wrap = false);
  D2D1_SIZE_F Measure(std::wstring_view s, float size, FontWeight w = FontWeight::Regular,
                      float maxWidth = 100000.f);
  void DrawIcon(Icon icon, const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float stroke = 1.5f);
  void SoftShadow(const D2D1_RECT_F& r, float rad, float spread, float alpha, float dy);

  ComPtr<IDWriteTextLayout> Layout(std::wstring_view s, float size, FontWeight w, float maxW, float maxH,
                                   HAlign h, VAlign v, bool wrap);
  const std::wstring& FontFamily();

 private:
  IDWriteTextFormat* Format(float size, FontWeight w);
  ID2D1DeviceContext5* dc_ = nullptr;
  IDWriteFactory3* dw_ = nullptr;
  ComPtr<ID2D1SolidColorBrush> brush_;
  ID2D1DeviceContext5* brushOwner_ = nullptr;
  std::unordered_map<uint32_t, ComPtr<IDWriteTextFormat>> formats_;
  std::wstring family_;
};

using UiId = uint64_t;
constexpr UiId MakeId(std::string_view s, uint64_t salt = 0) {
  uint64_t h = 1469598103934665603ull ^ (salt * 0x9E3779B97F4A7C15ull);
  for (char c : s) {
    h ^= uint8_t(c);
    h *= 1099511628211ull;
  }
  return h;
}

struct UiInput {
  D2D1_POINT_2F mouse{-1e6f, -1e6f};
  bool mouseDown = false;
  bool pressed = false;
  bool released = false;
  bool rightPressed = false;
  bool doubleClick = false;
  float wheel = 0.f;
  std::wstring chars;
  std::vector<unsigned> keys;
  bool ctrl = false, shift = false, alt = false;

  void ClearEdges() {
    pressed = released = rightPressed = doubleClick = false;
    wheel = 0.f;
    chars.clear();
    keys.clear();
  }
  bool KeyPressed(unsigned vk) const {
    for (unsigned k : keys) {
      if (k == vk) return true;
    }
    return false;
  }
};

class Ui {
 public:
  Painter p;
  const Theme* t = nullptr;
  UiInput in;
  double now = 0;
  float dt = 1.f / 60.f;

  void Begin(ID2D1DeviceContext5* dc, IDWriteFactory3* dw, const Theme& theme, const UiInput& input,
             double timeSec);
  bool End();
  double WakeIn() const { return wakeIn_; }

  bool MouseIn(const D2D1_RECT_F& r) const;
  bool Press(UiId id, const D2D1_RECT_F& r, bool* hovered = nullptr, bool* held = nullptr);
  float Anim(UiId id, float target, float speed = 18.f);
  float AnimFrom(UiId id, float initial, float target, float speed = 18.f);
  void RequestFrame() { needFrame_ = true; }
  bool AnyActive() const { return active_ != 0; }
  bool PopupOpen() const { return popupId_ != 0; }
  void ClosePopup() { popupId_ = 0; }
  void Defer(std::function<void()> fn) { deferred_.push_back(std::move(fn)); }

  UiId Focus() const { return focus_; }
  void SetFocus(UiId id) { focus_ = id; }
  void ClearFocus() { focus_ = 0; }

  bool Button(UiId id, const D2D1_RECT_F& r, const wchar_t* label, bool primary = false);
  bool IconButton(UiId id, const D2D1_RECT_F& r, Icon icon, bool selected = false,
                  const wchar_t* tooltip = nullptr, bool hud = false, float iconSize = 18.f);
  bool HudTextButton(UiId id, const D2D1_RECT_F& r, const wchar_t* label, bool primary,
                     Icon* trailing = nullptr);
  bool Toggle(UiId id, const D2D1_RECT_F& r, bool* value);
  bool Segmented(UiId id, const D2D1_RECT_F& r, std::span<const wchar_t* const> items, int* sel,
                 bool hud = false);
  bool Dropdown(UiId id, const D2D1_RECT_F& r, std::span<const wchar_t* const> items, int* sel);
  bool Slider(UiId id, const D2D1_RECT_F& r, float* value, float lo, float hi, float step = 0.f);
  bool TextField(UiId id, const D2D1_RECT_F& r, std::wstring* text, size_t maxLen = 64);
  bool SidebarItem(UiId id, const D2D1_RECT_F& r, Icon icon, const wchar_t* label, bool selected);
  void Tooltip(UiId id, const D2D1_RECT_F& anchor, const wchar_t* text);

  int CaptionButtons(UiId id, float right, float top, float height, bool maximized, bool showMaximize,
                     float* outLeft);

  int Menu(UiId id, const D2D1_RECT_F& anchor, std::span<const wchar_t* const> items,
           std::span<const bool> checked, bool hud, bool* open);

 private:
  struct PopupResult {
    UiId id = 0;
    int index = -1;
  };
  void DrawPopupList(const D2D1_RECT_F& box, const std::vector<std::wstring>& items,
                     const std::vector<bool>& checked, int current, bool hud, UiId id);

  UiId active_ = 0, focus_ = 0;
  bool pressConsumed_ = false;
  bool needFrame_ = false;
  bool inPopup_ = false;
  double wakeIn_ = -1;
  std::unordered_map<UiId, float> anims_;
  UiId popupId_ = 0;
  UiId menuOpenedId_ = 0;
  bool popupSeen_ = false;
  D2D1_RECT_F popupRect_{};
  PopupResult popupResult_;
  std::vector<std::function<void()>> deferred_;
  UiId tipId_ = 0;
  bool tipSeen_ = false;
  double tipStart_ = 0;
};

}
