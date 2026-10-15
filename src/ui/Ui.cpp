#include "ui/Ui.h"

#include <windows.h>

#include <algorithm>

namespace opal {

ComPtr<ID2D1PathGeometry> MakeRoundedPath(ID2D1DeviceContext* dc, const D2D1_RECT_F& r, float tl, float tr,
                                          float br, float bl) {
  ComPtr<ID2D1Factory> f;
  dc->GetFactory(&f);
  ComPtr<ID2D1PathGeometry> g;
  if (FAILED(f->CreatePathGeometry(&g))) return g;
  ComPtr<ID2D1GeometrySink> sink;
  g->Open(&sink);
  const float maxR = std::min(RW(r), RH(r)) * 0.5f;
  tl = std::min(tl, maxR); tr = std::min(tr, maxR); br = std::min(br, maxR); bl = std::min(bl, maxR);
  auto arc = [&](D2D1_POINT_2F to, float rad) {
    if (rad <= 0.01f) {
      sink->AddLine(to);
    } else {
      sink->AddArc({to, {rad, rad}, 0.f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL});
    }
  };
  sink->BeginFigure({r.left + tl, r.top}, D2D1_FIGURE_BEGIN_FILLED);
  sink->AddLine({r.right - tr, r.top});
  arc({r.right, r.top + tr}, tr);
  sink->AddLine({r.right, r.bottom - br});
  arc({r.right - br, r.bottom}, br);
  sink->AddLine({r.left + bl, r.bottom});
  arc({r.left, r.bottom - bl}, bl);
  sink->AddLine({r.left, r.top + tl});
  arc({r.left + tl, r.top}, tl);
  sink->EndFigure(D2D1_FIGURE_END_CLOSED);
  sink->Close();
  return g;
}

void Painter::Begin(ID2D1DeviceContext5* dc, IDWriteFactory3* dw) {
  if (dc != brushOwner_) {
    brush_.Reset();
    dc->CreateSolidColorBrush({0, 0, 0, 1}, &brush_);
    brushOwner_ = dc;
  }
  dc_ = dc;
  dw_ = dw;
}

ID2D1SolidColorBrush* Painter::Brush(const D2D1_COLOR_F& c) {
  brush_->SetColor(c);
  return brush_.Get();
}

const std::wstring& Painter::FontFamily() {
  if (family_.empty()) {
    family_ = L"Segoe UI";
    ComPtr<IDWriteFontCollection> fonts;
    if (dw_ && SUCCEEDED(dw_->GetSystemFontCollection(&fonts))) {
      UINT32 index = 0;
      BOOL exists = FALSE;
      fonts->FindFamilyName(L"Segoe UI Variable Text", &index, &exists);
      if (exists) family_ = L"Segoe UI Variable Text";
    }
  }
  return family_;
}

IDWriteTextFormat* Painter::Format(float size, FontWeight w) {
  const uint32_t key = (uint32_t(size * 4.f) << 12) | uint32_t(w);
  auto it = formats_.find(key);
  if (it != formats_.end()) return it->second.Get();
  ComPtr<IDWriteTextFormat> fmt;
  dw_->CreateTextFormat(FontFamily().c_str(), nullptr, DWRITE_FONT_WEIGHT(w), DWRITE_FONT_STYLE_NORMAL,
                        DWRITE_FONT_STRETCH_NORMAL, size, L"", &fmt);
  IDWriteTextFormat* raw = fmt.Get();
  formats_[key] = std::move(fmt);
  return raw;
}

ComPtr<IDWriteTextLayout> Painter::Layout(std::wstring_view s, float size, FontWeight w, float maxW,
                                          float maxH, HAlign h, VAlign v, bool wrap) {
  ComPtr<IDWriteTextLayout> layout;
  IDWriteTextFormat* fmt = Format(size, w);
  if (!fmt || FAILED(dw_->CreateTextLayout(s.data(), UINT32(s.size()), fmt, std::max(0.f, maxW),
                                           std::max(0.f, maxH), &layout))) {
    return nullptr;
  }
  layout->SetTextAlignment(h == HAlign::Left     ? DWRITE_TEXT_ALIGNMENT_LEADING
                           : h == HAlign::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                                                 : DWRITE_TEXT_ALIGNMENT_TRAILING);
  layout->SetParagraphAlignment(v == VAlign::Top      ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR
                                : v == VAlign::Center ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                                                      : DWRITE_PARAGRAPH_ALIGNMENT_FAR);
  layout->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
  if (!wrap) {
    ComPtr<IDWriteInlineObject> ellipsis;
    if (SUCCEEDED(dw_->CreateEllipsisTrimmingSign(fmt, &ellipsis))) {
      DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
      layout->SetTrimming(&trim, ellipsis.Get());
    }
  }
  return layout;
}

void Painter::FillRect(const D2D1_RECT_F& r, const D2D1_COLOR_F& c) { dc_->FillRectangle(r, Brush(c)); }

void Painter::FillRound(const D2D1_RECT_F& r, float rad, const D2D1_COLOR_F& c) {
  dc_->FillRoundedRectangle({r, rad, rad}, Brush(c));
}

void Painter::StrokeRound(const D2D1_RECT_F& r, float rad, const D2D1_COLOR_F& c, float w) {
  const float h = w * 0.5f;
  dc_->DrawRoundedRectangle({Inset(r, h), std::max(0.f, rad - h), std::max(0.f, rad - h)}, Brush(c), w);
}

void Painter::FillCircle(D2D1_POINT_2F c, float rad, const D2D1_COLOR_F& col) {
  dc_->FillEllipse({c, rad, rad}, Brush(col));
}

void Painter::Line(D2D1_POINT_2F a, D2D1_POINT_2F b, const D2D1_COLOR_F& c, float w) {
  dc_->DrawLine(a, b, Brush(c), w);
}

void Painter::Text(std::wstring_view s, const D2D1_RECT_F& r, float size, const D2D1_COLOR_F& c,
                   FontWeight w, HAlign h, VAlign v, bool wrap) {
  if (s.empty()) return;
  auto layout = Layout(s, size, w, RW(r), RH(r), h, v, wrap);
  if (layout) dc_->DrawTextLayout({r.left, r.top}, layout.Get(), Brush(c), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

D2D1_SIZE_F Painter::Measure(std::wstring_view s, float size, FontWeight w, float maxWidth) {
  auto layout = Layout(s, size, w, maxWidth, 100000.f, HAlign::Left, VAlign::Top, maxWidth < 99999.f);
  DWRITE_TEXT_METRICS m{};
  if (layout) layout->GetMetrics(&m);
  return {m.widthIncludingTrailingWhitespace, m.height};
}

void Painter::DrawIcon(Icon icon, const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float stroke) {
  opal::DrawIcon(dc_, Brush(c), icon, r, stroke);
}

void Painter::SoftShadow(const D2D1_RECT_F& r, float rad, float spread, float alpha, float dy) {
  constexpr int kSteps = 7;
  for (int i = kSteps; i >= 1; --i) {
    const float t = float(i) / kSteps;
    const float grow = spread * t;
    D2D1_RECT_F s{r.left - grow, r.top - grow + dy, r.right + grow, r.bottom + grow + dy};
    FillRound(s, rad + grow, {0, 0, 0, alpha / kSteps});
  }
}

void Ui::Begin(ID2D1DeviceContext5* dc, IDWriteFactory3* dw, const Theme& theme, const UiInput& input,
               double timeSec) {
  p.Begin(dc, dw);
  t = &theme;
  in = input;
  dt = now > 0 ? float(std::clamp(timeSec - now, 0.0, 0.1)) : 1.f / 60.f;
  now = timeSec;
  needFrame_ = false;
  wakeIn_ = -1;
  pressConsumed_ = false;
  popupSeen_ = false;
  tipSeen_ = false;
  deferred_.clear();
  if (popupId_ && in.pressed && !InRect(popupRect_, in.mouse)) {
    popupId_ = 0;
    pressConsumed_ = true;
  }
}

bool Ui::End() {
  inPopup_ = true;
  for (auto& fn : deferred_) fn();
  inPopup_ = false;
  deferred_.clear();
  if (!popupSeen_) popupId_ = 0;
  if (!tipSeen_) tipId_ = 0;
  if (!in.mouseDown) active_ = 0;
  return needFrame_;
}

bool Ui::MouseIn(const D2D1_RECT_F& r) const {
  if (!InRect(r, in.mouse)) return false;
  if (popupId_ && !inPopup_ && InRect(popupRect_, in.mouse)) return false;
  return true;
}

bool Ui::Press(UiId id, const D2D1_RECT_F& r, bool* hovered, bool* held) {
  const bool hov = MouseIn(r);
  if (hov && in.pressed && !pressConsumed_) {
    active_ = id;
    pressConsumed_ = true;
  }
  bool clicked = false;
  if (active_ == id && in.released) {
    clicked = hov;
    active_ = 0;
  }
  if (hovered) *hovered = hov;
  if (held) *held = active_ == id && in.mouseDown && hov;
  return clicked;
}

float Ui::AnimFrom(UiId id, float initial, float target, float speed) {
  auto [it, inserted] = anims_.try_emplace(id, initial);
  float& v = it->second;
  const float k = 1.f - std::exp(-speed * dt);
  v += (target - v) * k;
  if (std::fabs(target - v) < 0.0015f) {
    v = target;
  } else {
    needFrame_ = true;
  }
  return v;
}

float Ui::Anim(UiId id, float target, float speed) { return AnimFrom(id, target, target, speed); }

bool Ui::Button(UiId id, const D2D1_RECT_F& r, const wchar_t* label, bool primary) {
  bool hov = false, held = false;
  const bool clicked = Press(id, r, &hov, &held);
  const float rad = 6.f;
  if (primary) {
    D2D1_COLOR_F bg = held ? Mix(t->accent, {0, 0, 0, 1}, 0.15f) : hov ? t->accentHover : t->accent;
    p.SoftShadow(r, rad, 1.5f, 0.12f, 0.5f);
    p.FillRound(r, rad, bg);
    p.Text(label, r, 13.f, t->onAccent, FontWeight::Medium, HAlign::Center);
  } else {
    D2D1_COLOR_F bg = held ? t->controlPressed : hov ? t->controlHover : t->controlBg;
    p.SoftShadow(r, rad, 1.0f, t->dark ? 0.05f : 0.08f, 0.5f);
    p.FillRound(r, rad, bg);
    p.StrokeRound(r, rad, t->controlBorder, 1.f);
    p.Text(label, r, 13.f, t->text, FontWeight::Regular, HAlign::Center);
  }
  return clicked;
}

bool Ui::IconButton(UiId id, const D2D1_RECT_F& r, Icon icon, bool selected, const wchar_t* tooltip,
                    bool hud, float iconSize) {
  bool hov = false, held = false;
  const bool clicked = Press(id, r, &hov, &held);
  const float hoverA = Anim(id ^ 0x51, hov ? 1.f : 0.f, 22.f);
  const float rad = 7.f;
  D2D1_COLOR_F fg;
  if (hud) {
    if (selected) p.FillRound(r, rad, t->hudSelected);
    else if (hoverA > 0) p.FillRound(r, rad, WithAlpha(t->hudHover, t->hudHover.a * hoverA));
    fg = t->hudText;
  } else {
    if (selected) {
      p.FillRound(r, rad, WithAlpha(t->accent, t->dark ? 0.28f : 0.16f));
      fg = t->dark ? t->accentHover : t->accent;
    } else {
      if (hoverA > 0) p.FillRound(r, rad, WithAlpha(t->text, 0.07f * hoverA));
      fg = t->text;
    }
  }
  if (held) fg = WithAlpha(fg, fg.a * 0.7f);
  const D2D1_POINT_2F c = Center(r);
  p.DrawIcon(icon, RX(c.x - iconSize / 2, c.y - iconSize / 2, iconSize, iconSize), fg, 1.5f);
  if (tooltip) Tooltip(id, r, tooltip);
  return clicked;
}

bool Ui::HudTextButton(UiId id, const D2D1_RECT_F& r, const wchar_t* label, bool primary, Icon* trailing) {
  bool hov = false, held = false;
  const bool clicked = Press(id, r, &hov, &held);
  const float rad = 7.f;
  if (primary) {
    D2D1_COLOR_F bg = held ? Mix(t->accent, {0, 0, 0, 1}, 0.15f) : hov ? t->accentHover : t->accent;
    p.FillRound(r, rad, bg);
  } else if (hov) {
    p.FillRound(r, rad, held ? t->hudSelected : t->hudHover);
  }
  D2D1_RECT_F tr = r;
  if (trailing) tr.right -= 14.f;
  p.Text(label, tr, 13.f, t->hudText, FontWeight::Medium, HAlign::Center);
  if (trailing) {
    const float s = 11.f;
    p.DrawIcon(*trailing, RX(r.right - 19.f, (r.top + r.bottom - s) / 2 + 0.5f, s, s), t->hudTextSecondary, 1.6f);
  }
  return clicked;
}

bool Ui::Toggle(UiId id, const D2D1_RECT_F& r, bool* value) {
  bool hov = false, held = false;
  const bool clicked = Press(id, r, &hov, &held);
  if (clicked) *value = !*value;
  const float a = Anim(id, *value ? 1.f : 0.f, 20.f);
  const float h = RH(r);
  p.FillRound(r, h / 2, Mix(t->toggleOff, t->accent, a));
  const float d = h - 4.f;
  const float grow = held ? 4.f : 0.f;
  const float kx = Lerp(r.left + 2.f, r.right - 2.f - d - grow, a);
  D2D1_RECT_F knob = RX(kx, r.top + 2.f, d + grow, d);
  p.SoftShadow(knob, d / 2, 1.5f, 0.18f, 0.6f);
  p.FillRound(knob, d / 2, t->knob);
  return clicked;
}

bool Ui::Segmented(UiId id, const D2D1_RECT_F& r, std::span<const wchar_t* const> items, int* sel, bool hud) {
  const int n = int(items.size());
  if (n == 0) return false;
  bool changed = false;
  const float segW = RW(r) / n;
  p.FillRound(r, 7.f, hud ? t->hudHover : t->segmentBg);
  const float x = Anim(id, float(*sel), 18.f);
  D2D1_RECT_F pill = Inset(RX(r.left + x * segW, r.top, segW, RH(r)), 2.f);
  if (!hud) p.SoftShadow(pill, 5.f, 1.2f, t->dark ? 0.2f : 0.1f, 0.6f);
  p.FillRound(pill, 5.f, hud ? t->hudSelected : t->segmentSelected);
  for (int i = 0; i < n; ++i) {
    D2D1_RECT_F seg = RX(r.left + i * segW, r.top, segW, RH(r));
    if (Press(id + 1 + uint64_t(i), seg) && *sel != i) {
      *sel = i;
      changed = true;
    }
    const bool on = *sel == i;
    D2D1_COLOR_F fg = hud ? (on ? t->hudText : t->hudTextSecondary) : (on ? t->text : t->textSecondary);
    p.Text(items[size_t(i)], seg, 12.5f, fg, on ? FontWeight::Semibold : FontWeight::Medium, HAlign::Center);
  }
  return changed;
}

void Ui::DrawPopupList(const D2D1_RECT_F& box, const std::vector<std::wstring>& items,
                       const std::vector<bool>& checked, int current, bool hud, UiId id) {
  const D2D1_COLOR_F bg = hud ? Hex(0x2A2A2C, 0.97f) : t->popoverBg;
  const D2D1_COLOR_F border = hud ? t->hudBorder : t->popoverBorder;
  const D2D1_COLOR_F fg = hud ? t->hudText : t->text;
  p.SoftShadow(box, 9.f, 14.f, 0.28f, 6.f);
  p.FillRound(box, 9.f, bg);
  p.StrokeRound(box, 9.f, border, 1.f);
  float y = box.top + 5.f;
  for (size_t i = 0; i < items.size(); ++i) {
    if (items[i].empty()) {
      p.Line({box.left + 10.f, y + 4.5f}, {box.right - 10.f, y + 4.5f}, hud ? t->hudBorder : t->separator, 1.f);
      y += 9.f;
      continue;
    }
    D2D1_RECT_F row = RX(box.left + 5.f, y, RW(box) - 10.f, 24.f);
    const bool hov = InRect(row, in.mouse);
    if (hov) p.FillRound(row, 5.f, t->accent);
    const bool isChecked = (i < checked.size() && checked[i]) || int(i) == current;
    if (isChecked) {
      p.DrawIcon(Icon::Check, RX(row.left + 4.f, row.top + 5.f, 14.f, 14.f), hov ? t->onAccent : fg, 1.8f);
    }
    p.Text(items[i], RX(row.left + 22.f, row.top, RW(row) - 26.f, 24.f), 13.f, hov ? t->onAccent : fg);
    if (hov && in.released) {
      popupResult_ = {id, int(i)};
      popupId_ = 0;
      needFrame_ = true;
    }
    y += 24.f;
  }
}

static float PopupHeight(std::span<const wchar_t* const> items) {
  float h = 10.f;
  for (auto* s : items) h += (s && *s) ? 24.f : 9.f;
  return h;
}

bool Ui::Dropdown(UiId id, const D2D1_RECT_F& r, std::span<const wchar_t* const> items, int* sel) {
  bool changed = false;
  if (popupResult_.id == id) {
    if (popupResult_.index != *sel) {
      *sel = popupResult_.index;
      changed = true;
    }
    popupResult_ = {};
  }
  bool hov = false, held = false;
  if (Press(id, r, &hov, &held)) {
    popupId_ = popupId_ == id ? 0 : id;
  }
  const float rad = 6.f;
  p.SoftShadow(r, rad, 1.0f, t->dark ? 0.05f : 0.08f, 0.5f);
  p.FillRound(r, rad, held ? t->controlPressed : hov ? t->controlHover : t->controlBg);
  p.StrokeRound(r, rad, t->controlBorder, 1.f);
  const wchar_t* label = (*sel >= 0 && *sel < int(items.size())) ? items[size_t(*sel)] : L"";
  p.Text(label, RX(r.left + 10.f, r.top, RW(r) - 36.f, RH(r)), 13.f, t->text);
  D2D1_RECT_F chip = RX(r.right - 21.f, r.top + (RH(r) - 16.f) / 2, 16.f, 16.f);
  p.FillRound(chip, 4.f, t->accent);
  p.DrawIcon(Icon::ChevronDown, Inset(chip, 3.f), t->onAccent, 1.7f);

  if (popupId_ == id) {
    popupSeen_ = true;
    float maxW = RW(r);
    for (auto* s : items) maxW = std::max(maxW, p.Measure(s ? s : L"", 13.f).width + 40.f);
    popupRect_ = RX(r.left, r.bottom + 4.f, maxW, PopupHeight(items));
    std::vector<std::wstring> copy;
    for (auto* s : items) copy.emplace_back(s ? s : L"");
    const int current = *sel;
    const D2D1_RECT_F box = popupRect_;
    Defer([this, box, copy = std::move(copy), current, id] { DrawPopupList(box, copy, {}, current, false, id); });
  }
  return changed;
}

int Ui::Menu(UiId id, const D2D1_RECT_F& anchor, std::span<const wchar_t* const> items,
             std::span<const bool> checked, bool hud, bool* open) {
  if (popupResult_.id == id) {
    const int idx = popupResult_.index;
    popupResult_ = {};
    menuOpenedId_ = 0;
    *open = false;
    return idx;
  }
  if (!*open) {
    if (popupId_ == id) popupId_ = 0;
    if (menuOpenedId_ == id) menuOpenedId_ = 0;
    return -1;
  }
  if (popupId_ != id) {
    if (menuOpenedId_ != id) {
      popupId_ = id;
      menuOpenedId_ = id;
    } else {
      menuOpenedId_ = 0;
      *open = false;
      return -1;
    }
  }
  popupSeen_ = true;
  float maxW = 160.f;
  for (auto* s : items) maxW = std::max(maxW, p.Measure(s ? s : L"", 13.f).width + 40.f);
  popupRect_ = RX(anchor.left, anchor.bottom + 6.f, maxW, PopupHeight(items));
  std::vector<std::wstring> copy;
  for (auto* s : items) copy.emplace_back(s ? s : L"");
  std::vector<bool> chk(checked.begin(), checked.end());
  const D2D1_RECT_F box = popupRect_;
  Defer([this, box, copy = std::move(copy), chk = std::move(chk), hud, id] {
    DrawPopupList(box, copy, chk, -1, hud, id);
  });
  return -1;
}

bool Ui::Slider(UiId id, const D2D1_RECT_F& r, float* value, float lo, float hi, float step) {
  bool hov = false, held = false;
  Press(id, r, &hov, &held);
  bool changed = false;
  const float knobD = 18.f;
  const float x0 = r.left + knobD / 2, x1 = r.right - knobD / 2;
  if (active_ == id && in.mouseDown) {
    float v = lo + std::clamp((in.mouse.x - x0) / std::max(1.f, x1 - x0), 0.f, 1.f) * (hi - lo);
    if (step > 0) v = lo + std::round((v - lo) / step) * step;
    if (v != *value) {
      *value = v;
      changed = true;
    }
  }
  const float tt = hi > lo ? std::clamp((*value - lo) / (hi - lo), 0.f, 1.f) : 0.f;
  const float cy = (r.top + r.bottom) / 2;
  p.FillRound({x0, cy - 2.f, x1, cy + 2.f}, 2.f, t->toggleOff);
  const float kx = Lerp(x0, x1, tt);
  p.FillRound({x0, cy - 2.f, kx, cy + 2.f}, 2.f, t->accent);
  D2D1_RECT_F knob = RX(kx - knobD / 2, cy - knobD / 2, knobD, knobD);
  p.SoftShadow(knob, knobD / 2, 1.5f, 0.2f, 0.6f);
  p.FillRound(knob, knobD / 2, t->knob);
  p.StrokeRound(knob, knobD / 2, t->controlBorder, 0.5f);
  return changed;
}

bool Ui::TextField(UiId id, const D2D1_RECT_F& r, std::wstring* text, size_t maxLen) {
  bool hov = false;
  if (Press(id, r, &hov)) focus_ = id;
  if (focus_ == id && in.pressed && !MouseIn(r)) focus_ = 0;
  bool changed = false;
  const bool focused = focus_ == id;
  if (focused) {
    for (wchar_t c : in.chars) {
      if (c >= 0x20 && c != 0x7F && text->size() < maxLen) {
        text->push_back(c);
        changed = true;
      }
    }
    for (unsigned k : in.keys) {
      if (k == VK_BACK && !text->empty()) {
        text->pop_back();
        changed = true;
      } else if (k == VK_RETURN || k == VK_ESCAPE || k == VK_TAB) {
        focus_ = 0;
      }
    }
  }
  const float rad = 6.f;
  if (focused) p.FillRound(Inset(r, -3.f), rad + 3.f, WithAlpha(t->accent, 0.35f));
  p.FillRound(r, rad, t->dark ? Hex(0xFFFFFF, 0.06f) : Hex(0xFFFFFF));
  p.StrokeRound(r, rad, focused ? t->accent : t->controlBorder, 1.f);
  D2D1_RECT_F tr = RX(r.left + 8.f, r.top, RW(r) - 16.f, RH(r));
  p.Text(*text, tr, 13.f, t->text);
  if (focused) {
    const float w = p.Measure(*text, 13.f).width;
    const float cx = std::min(tr.right, tr.left + w + 1.f);
    p.Line({cx, r.top + 5.f}, {cx, r.bottom - 5.f}, t->accent, 1.5f);
  }
  return changed;
}

bool Ui::SidebarItem(UiId id, const D2D1_RECT_F& r, Icon icon, const wchar_t* label, bool selected) {
  bool hov = false;
  const bool clicked = Press(id, r, &hov);
  if (selected) {
    p.FillRound(r, 6.f, t->dark ? Hex(0xFFFFFF, 0.1f) : Hex(0x000000, 0.08f));
  } else if (hov) {
    p.FillRound(r, 6.f, t->dark ? Hex(0xFFFFFF, 0.05f) : Hex(0x000000, 0.04f));
  }
  p.DrawIcon(icon, RX(r.left + 9.f, r.top + (RH(r) - 16.f) / 2, 16.f, 16.f), t->accent, 1.5f);
  p.Text(label, RX(r.left + 34.f, r.top, RW(r) - 40.f, RH(r)), 13.f, t->text,
         selected ? FontWeight::Medium : FontWeight::Regular);
  return clicked;
}

void Ui::Tooltip(UiId id, const D2D1_RECT_F& anchor, const wchar_t* text) {
  if (!MouseIn(anchor) || in.mouseDown) return;
  tipSeen_ = true;
  if (tipId_ != id) {
    tipId_ = id;
    tipStart_ = now;
  }
  const double elapsed = now - tipStart_;
  constexpr double kDelay = 0.55;
  if (elapsed < kDelay) {
    const double w = kDelay - elapsed;
    wakeIn_ = wakeIn_ < 0 ? w : std::min(wakeIn_, w);
    return;
  }
  std::wstring s = text;
  Defer([this, anchor, s] {
    const D2D1_SIZE_F sz = p.Measure(s, 11.5f);
    const float w = sz.width + 16.f, h = 22.f;
    const float cx = (anchor.left + anchor.right) / 2;
    D2D1_RECT_F box = RX(cx - w / 2, anchor.bottom + 6.f, w, h);
    p.SoftShadow(box, 5.f, 6.f, 0.2f, 2.f);
    p.FillRound(box, 5.f, t->dark ? Hex(0x3A3A3C) : Hex(0xFFFFFF));
    p.StrokeRound(box, 5.f, t->popoverBorder, 0.75f);
    p.Text(s, box, 11.5f, t->text, FontWeight::Regular, HAlign::Center);
  });
}

int Ui::CaptionButtons(UiId id, float right, float top, float height, bool maximized, bool showMaximize,
                       float* outLeft) {
  constexpr float kW = 46.f;
  const int n = showMaximize ? 3 : 2;
  float x = right - kW * n;
  if (outLeft) *outLeft = x;
  int result = 0;
  const D2D1_COLOR_F fg = t->text;
  for (int i = 0; i < n; ++i) {
    const int kind = (i == 0) ? 1 : (i == n - 1 ? 3 : 2);
    const D2D1_RECT_F r = RX(x, top, kW, height);
    bool hov = false, held = false;
    if (Press(id + uint64_t(kind), r, &hov, &held)) result = kind;
    D2D1_COLOR_F ic = fg;
    if (kind == 3 && (hov || held)) {
      p.FillRect(r, held ? Hex(0xC42B1C, 0.85f) : Hex(0xC42B1C));
      ic = Hex(0xFFFFFF);
    } else if (hov) {
      p.FillRect(r, WithAlpha(t->text, held ? 0.05f : 0.08f));
    }
    const D2D1_POINT_2F c = Center(r);
    const float s = 5.f;
    if (kind == 1) {
      p.Line({c.x - s, c.y + 0.5f}, {c.x + s, c.y + 0.5f}, ic, 1.f);
    } else if (kind == 2) {
      if (maximized) {
        p.StrokeRound(RX(c.x - s + 2, c.y - s, 2 * s - 2, 2 * s - 2), 1.5f, ic, 1.f);
        p.FillRect(RX(c.x - s, c.y - s + 2, 2 * s - 2, 2 * s - 2), hov ? Mix(t->windowBg, t->text, 0.08f) : t->windowBg);
        p.StrokeRound(RX(c.x - s, c.y - s + 2, 2 * s - 2, 2 * s - 2), 1.5f, ic, 1.f);
      } else {
        p.StrokeRound(RX(c.x - s, c.y - s, 2 * s, 2 * s), 1.5f, ic, 1.f);
      }
    } else {
      p.Line({c.x - s, c.y - s}, {c.x + s, c.y + s}, ic, 1.f);
      p.Line({c.x + s, c.y - s}, {c.x - s, c.y + s}, ic, 1.f);
    }
    x += kW;
  }
  return result;
}

}
