#include "settingsui/SettingsWindow.h"

#include <shellscalingapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>

#include "app/App.h"
#include "app/Hotkeys.h"
#include "app/Settings.h"
#include "app/Sound.h"
#include "app/SystemIntegration.h"
#include "core/Strings.h"
#include "Version.h"

namespace opal {
namespace {

constexpr float kW = 780.f, kH = 580.f;
constexpr float kSidebar = 220.f;
constexpr float kTop = 58.f;
constexpr UINT kMsgRecordKey = WM_APP + 0x230;

constexpr S kActionLabels[kActionCount] = {S::ShortcutBar, S::ShortcutScreen, S::ShortcutRegion, S::ShortcutWindow,
                                           S::ShortcutElement};

}

bool SettingsWindow::Init() {
  POINT pt;
  GetCursorPos(&pt);
  HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi{sizeof(mi)};
  GetMonitorInfoW(mon, &mi);
  UINT dpiX = 96, dpiY = 96;
  GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
  const float s = float(dpiX) / 96.f;
  const int w = int(kW * s), h = int(kH * s);
  CreateParams p;
  p.style = WS_OVERLAPPED | WS_CAPTION | WS_MINIMIZEBOX;
  p.title = L"Opal";
  p.rect = {mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2,
            mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - h) / 2, 0, 0};
  p.rect.right = p.rect.left + w;
  p.rect.bottom = p.rect.top + h;
  p.customFrame = true;
  p.mica = true;
  if (!Create(p)) return false;
  ApplyDarkFrame(Theme::Current().dark);
  snipKeyOff_ = !sys::IsWindowsSnipKeyEnabled();
  RenderNow();
  ShowWindow(hwnd(), SW_SHOW);
  SetForegroundWindow(hwnd());
  return true;
}

void SettingsWindow::Activate() {
  if (IsIconic(hwnd())) ShowWindow(hwnd(), SW_RESTORE);
  SetForegroundWindow(hwnd());
}

void SettingsWindow::OnDestroyed() {
  if (recording_ >= 0) HotkeyManager::Get().EndRecording();
}

D2D1_COLOR_F SettingsWindow::ClearColor() {
  if (MicaSupported()) return {0, 0, 0, 0};
  return Theme::Current().windowBg;
}

LRESULT SettingsWindow::HitTestClient(POINT pt) {
  const float s = Scale();
  const D2D1_POINT_2F dip{pt.x / s, pt.y / s};
  if (dip.y >= kTop) return HTCLIENT;
  for (const auto& r : hitRects_) {
    if (InRect(r, dip)) return HTCLIENT;
  }
  return HTCAPTION;
}

LRESULT SettingsWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp, bool& handled) {
  handled = false;
  switch (msg) {
    case kMsgRecordKey: {
      handled = true;
      if (recording_ < 0) return 0;
      const uint32_t vk = uint32_t(wp), mods = uint32_t(lp);
      Settings& s = AppSettings();
      if (vk == VK_ESCAPE && mods == 0) {
        StopRecording();
      } else if ((vk == VK_BACK || vk == VK_DELETE) && mods == 0) {
        s.hotkeys[size_t(recording_)] = {};
        StopRecording();
        Changed();
      } else {
        const Hotkey hk{mods, vk};
        conflict_.clear();
        for (int i = 0; i < kActionCount; ++i) {
          if (i != recording_ && s.hotkeys[size_t(i)] == hk) {
            s.hotkeys[size_t(i)] = {};
            conflict_ = FormatHotkey(hk) + L" — " + T(S::ShortcutInUse) + L": " + T(kActionLabels[i]);
          }
        }
        s.hotkeys[size_t(recording_)] = hk;
        StopRecording();
        Changed();
      }
      Invalidate();
      return 0;
    }
    case WM_ACTIVATE:
      if (LOWORD(wp) == WA_INACTIVE && recording_ >= 0) {
        StopRecording();
        Invalidate();
      }
      break;
    case WM_MOUSEWHEEL:
      scroll_ -= float(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA * 64.f;
      Invalidate();
      break;
    default:
      break;
  }
  (void)lp;
  return 0;
}

void SettingsWindow::StartRecording(int action) {
  recording_ = action;
  conflict_.clear();
  HotkeyManager::Get().BeginRecording(hwnd(), kMsgRecordKey);
}

void SettingsWindow::StopRecording() {
  recording_ = -1;
  HotkeyManager::Get().EndRecording();
}

void SettingsWindow::Changed() {
  App::Get().ApplySettings();
  ApplyDarkFrame(Theme::Current().dark);
  Invalidate();
}

void SettingsWindow::PickFolder() {
  ComPtr<IFileOpenDialog> dlg;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
  DWORD opts = 0;
  dlg->GetOptions(&opts);
  dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
  ComPtr<IShellItem> start;
  if (SUCCEEDED(SHCreateItemFromParsingName(AppSettings().ResolvedFolder().c_str(), nullptr, IID_PPV_ARGS(&start)))) {
    dlg->SetFolder(start.Get());
  }
  if (FAILED(dlg->Show(hwnd()))) return;
  ComPtr<IShellItem> item;
  PWSTR path = nullptr;
  if (SUCCEEDED(dlg->GetResult(&item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
    AppSettings().folder = path;
    CoTaskMemFree(path);
    Changed();
  }
}

float SettingsWindow::Section(float y, const wchar_t* title) {
  ui_.p.Text(title, RX(contentLeft_ + 4.f, y, contentWidth_, 20.f), 13.f, Theme::Current().text, FontWeight::Semibold);
  return y + 26.f;
}

float SettingsWindow::Card(float y, std::vector<Row> rows) {
  const Theme& t = Theme::Current();
  Painter& p = ui_.p;
  std::vector<float> heights;
  float total = 0;
  const float textW = contentWidth_ - 48.f;
  for (const auto& r : rows) {
    float h = 44.f;
    if (!r.desc.empty()) {
      const float dw = textW - r.controlWidth - 16.f;
      h = std::max(44.f, 16.f + 18.f + p.Measure(r.desc, 11.5f, FontWeight::Regular, dw).height + 12.f);
    }
    heights.push_back(h);
    total += h;
  }
  const D2D1_RECT_F card = RX(contentLeft_, y, contentWidth_, total);
  p.FillRound(card, 10.f, t.cardBg);
  p.StrokeRound(card, 10.f, t.cardBorder, 1.f);
  float ry = y;
  for (size_t i = 0; i < rows.size(); ++i) {
    const Row& r = rows[i];
    const float h = heights[i];
    const float dw = textW - r.controlWidth - 16.f;
    if (r.desc.empty()) {
      p.Text(r.title, RX(contentLeft_ + 16.f, ry, dw, h), 13.f, t.text);
    } else {
      p.Text(r.title, RX(contentLeft_ + 16.f, ry + 12.f, dw, 18.f), 13.f, t.text);
      p.Text(r.desc, RX(contentLeft_ + 16.f, ry + 31.f, dw, h - 36.f), 11.5f, t.textSecondary, FontWeight::Regular,
             HAlign::Left, VAlign::Top, true);
    }
    const D2D1_RECT_F ctl = RX(contentLeft_ + contentWidth_ - 16.f - r.controlWidth, ry + (h - r.controlHeight) / 2,
                               r.controlWidth, r.controlHeight);
    if (r.control) r.control(ctl);
    if (i + 1 < rows.size()) {
      p.Line({contentLeft_ + 16.f, ry + h - 0.5f}, {contentLeft_ + contentWidth_ - 16.f, ry + h - 0.5f}, t.separator, 1.f);
    }
    ry += h;
  }
  return y + total + 22.f;
}

void SettingsWindow::OnRender(ID2D1DeviceContext5* dc) {
  if (!logo_) {
    IWICImagingFactory* wic = Gpu::Get().Wic();
    HICON icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, 256, 256, 0));
    ComPtr<IWICBitmap> bmp;
    ComPtr<IWICFormatConverter> conv;
    if (wic && icon && SUCCEEDED(wic->CreateBitmapFromHICON(icon, &bmp)) && SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(bmp.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                   WICBitmapPaletteTypeCustom))) {
      dc->CreateBitmapFromWicBitmap(conv.Get(), nullptr, &logo_);
    }
    if (icon) DestroyIcon(icon);
  }
  BeginUiFrame(dc);
  hitRects_.clear();
  DrawSidebar();
  DrawContent(dc);
  float capLeft = 0;
  const D2D1_SIZE_F sz = ClientSizeDip();
  const int cap = ui_.CaptionButtons(MakeId("set.caption"), sz.width, 0.f, 34.f, false, false, &capLeft);
  hitRects_.push_back({capLeft, 0, sz.width, 34.f});
  if (cap == 1) ShowWindow(hwnd(), SW_MINIMIZE);
  else if (cap == 3) PostMessageW(hwnd(), WM_CLOSE, 0, 0);
  EndUiFrame(dc);
}

void SettingsWindow::DrawSidebar() {
  const Theme& t = Theme::Current();
  Painter& p = ui_.p;
  const D2D1_SIZE_F sz = ClientSizeDip();
  p.FillRect({0, 0, kSidebar, sz.height}, t.sidebarBg);
  p.Line({kSidebar - 0.5f, 0}, {kSidebar - 0.5f, sz.height}, t.separator, 1.f);
  if (logo_) ui_.p.dc()->DrawBitmap(logo_.Get(), RX(18.f, 16.f, 28.f, 28.f), 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
  p.Text(L"Opal", RX(54.f, 16.f, 140.f, 28.f), 15.f, t.text, FontWeight::Semibold);

  struct Item {
    Page page;
    Icon icon;
    S label;
  };
  const Item items[] = {{Page::General, Icon::Gear, S::PageGeneral},
                        {Page::Shortcuts, Icon::Keyboard, S::PageShortcuts},
                        {Page::Capture, Icon::Camera, S::PageCapture},
                        {Page::Output, Icon::Folder, S::PageOutput},
                        {Page::About, Icon::Info, S::PageAbout}};
  float y = kTop + 8.f;
  for (const auto& it : items) {
    const D2D1_RECT_F r = RX(10.f, y, kSidebar - 20.f, 32.f);
    if (ui_.SidebarItem(MakeId("set.page", uint64_t(it.page)), r, it.icon, T(it.label), page_ == it.page)) {
      if (page_ != it.page) {
        page_ = it.page;
        scroll_ = 0;
        if (recording_ >= 0) StopRecording();
      }
    }
    y += 34.f;
  }
}

void SettingsWindow::DrawContent(ID2D1DeviceContext5* dc) {
  const Theme& t = Theme::Current();
  Painter& p = ui_.p;
  const D2D1_SIZE_F sz = ClientSizeDip();
  contentLeft_ = kSidebar + 28.f;
  contentWidth_ = sz.width - kSidebar - 56.f;
  p.FillRect({kSidebar, 0, sz.width, sz.height}, t.contentBg);

  static constexpr S kTitles[] = {S::PageGeneral, S::PageShortcuts, S::PageCapture, S::PageOutput, S::PageAbout};
  p.Text(T(kTitles[int(page_)]), RX(contentLeft_, 14.f, contentWidth_ - 100.f, 30.f), 20.f, t.text, FontWeight::Semibold);

  contentRect_ = {kSidebar, kTop, sz.width, sz.height};
  const float visible = RH(contentRect_);
  scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, contentHeight_ - visible + 16.f));
  const float sc = ui_.Anim(MakeId("set.scroll"), scroll_, 22.f);

  const D2D1_POINT_2F realMouse = ui_.in.mouse;
  if (!InRect(contentRect_, realMouse)) ui_.in.mouse = {-1e6f, -1e6f};
  dc->PushAxisAlignedClip(contentRect_, D2D1_ANTIALIAS_MODE_ALIASED);
  float y = kTop + 6.f - sc;
  const float start = y;
  switch (page_) {
    case Page::General: PageGeneral(y); break;
    case Page::Shortcuts: PageShortcuts(y); break;
    case Page::Capture: PageCapture(y); break;
    case Page::Output: PageOutput(y); break;
    case Page::About: PageAbout(dc, y); break;
  }
  contentHeight_ = y - start;
  dc->PopAxisAlignedClip();
  ui_.in.mouse = realMouse;
}

void SettingsWindow::PageGeneral(float& y) {
  Settings& s = AppSettings();
  y = Card(y, {
                  {T(S::LaunchAtStartup), T(S::LaunchAtStartupDesc), 38.f, 22.f,
                   [&](const D2D1_RECT_F& r) {
                     if (ui_.Toggle(MakeId("g.startup"), r, &s.launchAtStartup)) Changed();
                   }},
                  {T(S::UsePrintScreen), T(S::UsePrintScreenDesc), 38.f, 22.f,
                   [&](const D2D1_RECT_F& r) {
                     if (ui_.Toggle(MakeId("g.prtsc"), r, &s.usePrintScreen)) Changed();
                   }},
                  {T(S::DisableSnipKey), T(S::DisableSnipKeyDesc), 38.f, 22.f,
                   [&](const D2D1_RECT_F& r) {
                     if (ui_.Toggle(MakeId("g.snip"), r, &snipKeyOff_)) sys::SetWindowsSnipKeyEnabled(!snipKeyOff_);
                   }},
                  {T(S::PlaySound), L"", 38.f, 22.f,
                   [&](const D2D1_RECT_F& r) {
                     if (ui_.Toggle(MakeId("g.sound"), r, &s.playSound)) Changed();
                   }},
                  {T(S::ShutterSoundLabel), L"", 220.f, 26.f,
                   [&](const D2D1_RECT_F& r) {
                     const wchar_t* items[] = {T(S::SoundModern), T(S::SoundVintage)};
                     int sel = int(s.shutterSound);
                     if (ui_.Segmented(MakeId("g.shutter"), r, items, &sel)) {
                       s.shutterSound = ShutterSound(sel);
                       PlayShutterSound(s.shutterSound);
                       Changed();
                     }
                   }},
              });
  y = Card(y, {
                  {T(S::Appearance), L"", 240.f, 26.f,
                   [&](const D2D1_RECT_F& r) {
                     const wchar_t* items[] = {T(S::ThemeSystem), T(S::ThemeLight), T(S::ThemeDark)};
                     int sel = int(s.theme);
                     if (ui_.Segmented(MakeId("g.theme"), r, items, &sel)) {
                       s.theme = ThemePref(sel);
                       Changed();
                     }
                   }},
                  {T(S::Language), L"", 170.f, 26.f,
                   [&](const D2D1_RECT_F& r) {
                     const wchar_t* items[] = {T(S::LangSystem), L"English", L"Türkçe"};
                     int sel = s.language == L"en" ? 1 : (s.language == L"tr" ? 2 : 0);
                     if (ui_.Dropdown(MakeId("g.lang"), r, items, &sel)) {
                       s.language = sel == 1 ? L"en" : (sel == 2 ? L"tr" : L"auto");
                       Changed();
                     }
                   }},
              });
}

void SettingsWindow::PageShortcuts(float& y) {
  const Theme& t = Theme::Current();
  Painter& p = ui_.p;
  p.Text(T(S::ShortcutsDesc), RX(contentLeft_ + 4.f, y, contentWidth_, 36.f), 12.f, t.textSecondary,
         FontWeight::Regular, HAlign::Left, VAlign::Top, true);
  y += 34.f;
  Settings& s = AppSettings();
  std::vector<Row> rows;
  for (int i = 0; i < kActionCount; ++i) {
    rows.push_back({T(kActionLabels[i]), L"", 236.f, 28.f, [this, i, &s](const D2D1_RECT_F& r) {
                      const Theme& th = Theme::Current();
                      Painter& pp = ui_.p;
                      const Hotkey& hk = s.hotkeys[size_t(i)];
                      const D2D1_RECT_F field = RX(r.left, r.top, RW(r) - 32.f, RH(r));
                      bool hov = false;
                      if (ui_.Press(MakeId("k.field", uint64_t(i)), field, &hov)) {
                        if (recording_ == i) StopRecording();
                        else StartRecording(i);
                      }
                      const bool rec = recording_ == i;
                      if (rec) pp.FillRound(Inset(field, -3.f), 9.f, WithAlpha(th.accent, 0.3f));
                      pp.FillRound(field, 6.f, hov && !rec ? th.controlHover : th.controlBg);
                      pp.StrokeRound(field, 6.f, rec ? th.accent : th.controlBorder, 1.f);
                      if (rec) {
                        pp.Text(T(S::PressKeys), field, 12.5f, th.textSecondary, FontWeight::Regular, HAlign::Center);
                      } else if (!hk.Valid()) {
                        pp.Text(T(S::NotSet), field, 12.5f, th.textTertiary, FontWeight::Regular, HAlign::Center);
                      } else {
                        auto parts = HotkeyParts(hk);
                        std::vector<float> widths;
                        float total = 0;
                        for (const auto& part : parts) {
                          widths.push_back(std::max(20.f, pp.Measure(part, 11.5f, FontWeight::Medium).width + 12.f));
                          total += widths.back() + 4.f;
                        }
                        float x = field.left + (RW(field) - total + 4.f) / 2;
                        for (size_t k = 0; k < parts.size(); ++k) {
                          const D2D1_RECT_F cap = RX(x, field.top + 4.f, widths[k], RH(field) - 8.f);
                          pp.FillRound(cap, 4.f, th.dark ? Hex(0xFFFFFF, 0.12f) : Hex(0x000000, 0.06f));
                          pp.StrokeRound(cap, 4.f, th.dark ? Hex(0xFFFFFF, 0.08f) : Hex(0x000000, 0.08f), 0.75f);
                          pp.Text(parts[k], cap, 11.5f, th.text, FontWeight::Medium, HAlign::Center);
                          x += widths[k] + 4.f;
                        }
                      }
                      if (hk.Valid() && !rec) {
                        if (ui_.IconButton(MakeId("k.clear", uint64_t(i)), RX(r.right - 26.f, r.top + 2.f, 24.f, 24.f),
                                           Icon::Close, false, T(S::Clear), false, 12.f)) {
                          s.hotkeys[size_t(i)] = {};
                          Changed();
                        }
                      }
                    }});
  }
  y = Card(y, std::move(rows));
  if (!conflict_.empty()) {
    p.Text(conflict_, RX(contentLeft_ + 4.f, y - 14.f, contentWidth_, 20.f), 12.f, Theme::Current().destructive);
    y += 14.f;
  }
}

void SettingsWindow::PageCapture(float& y) {
  Settings& s = AppSettings();
  auto toggleRow = [&](S title, const wchar_t* desc, const char* id, bool* v) {
    return Row{T(title), desc ? desc : L"", 38.f, 22.f, [this, id, v](const D2D1_RECT_F& r) {
                 if (ui_.Toggle(MakeId(id), r, v)) Changed();
               }};
  };
  y = Section(y, T(S::SectionWindows));
  y = Card(y, {toggleRow(S::WindowShadow, T(S::WindowShadowDesc), "c.shadow", &s.windowShadow)});
  y = Section(y, T(S::SectionElement));
  y = Card(y, {toggleRow(S::SnapEdges, T(S::SnapEdgesDesc), "c.snap", &s.snapEdges),
               toggleRow(S::AutoCorners, T(S::AutoCornersDesc), "c.corners", &s.autoCorners),
               toggleRow(S::ElementShadow, nullptr, "c.eshadow", &s.elementShadow)});
  y = Section(y, T(S::SectionGeneralCapture));
  y = Card(y, {toggleRow(S::ShowPointer, nullptr, "c.pointer", &s.showPointer),
               toggleRow(S::ShowLoupe, nullptr, "c.loupe", &s.showLoupe)});
  y = Section(y, T(S::SectionThumbnail));
  y = Card(y, {toggleRow(S::ShowThumbnail, nullptr, "c.thumb", &s.showThumbnail),
               Row{T(S::ThumbnailDuration), L"", 220.f, 24.f, [this, &s](const D2D1_RECT_F& r) {
                     float v = float(s.thumbnailSeconds);
                     if (ui_.Slider(MakeId("c.thumbsec"), RX(r.left, r.top, RW(r) - 48.f, RH(r)), &v, 2.f, 15.f, 1.f)) {
                       s.thumbnailSeconds = int(v);
                     }
                     if (!ui_.AnyActive() && ui_.in.released) s.Save();
                     ui_.p.Text(std::to_wstring(s.thumbnailSeconds) + T(S::Seconds),
                                RX(r.right - 42.f, r.top, 42.f, RH(r)), 12.5f, Theme::Current().textSecondary,
                                FontWeight::Regular, HAlign::Right);
                   }}});
}

void SettingsWindow::PageOutput(float& y) {
  Settings& s = AppSettings();
  y = Section(y, T(S::SectionAfterCapture));
  y = Card(y, {
                  {T(S::CopyToClipboard), L"", 38.f, 22.f,
                   [this, &s](const D2D1_RECT_F& r) {
                     if (ui_.Toggle(MakeId("o.clip"), r, &s.copyToClipboard)) Changed();
                   }},
                  {T(S::SaveToFolder), L"", 38.f, 22.f,
                   [this, &s](const D2D1_RECT_F& r) {
                     if (ui_.Toggle(MakeId("o.file"), r, &s.saveToFile)) Changed();
                   }},
                  {T(S::Folder), s.ResolvedFolder(), 168.f, 26.f,
                   [this](const D2D1_RECT_F& r) {
                     if (ui_.Button(MakeId("o.change"), RX(r.left, r.top, 96.f, RH(r)), T(S::Change))) AfterFrame([this] { PickFolder(); });
                     if (ui_.Button(MakeId("o.open"), RX(r.left + 102.f, r.top, 66.f, RH(r)), T(S::Open))) {
                       sys::OpenFolder(AppSettings().ResolvedFolder());
                     }
                   }},
              });
  wchar_t example[256];
  swprintf_s(example, T(S::FilePrefixDesc), s.filePrefix.c_str());
  y = Card(y, {
                  {T(S::FilePrefix), example, 170.f, 26.f,
                   [this, &s](const D2D1_RECT_F& r) {
                     if (ui_.TextField(MakeId("o.prefix"), r, &s.filePrefix, 40)) {
                       std::erase_if(s.filePrefix, [](wchar_t c) { return wcschr(L"\\/:*?\"<>|", c) != nullptr; });
                       s.Save();
                     }
                     if (ui_.Focus() != MakeId("o.prefix") && s.filePrefix.empty()) {
                       s.filePrefix = L"Opal";
                       s.Save();
                     }
                   }},
              });
}

void SettingsWindow::PageAbout(ID2D1DeviceContext5* dc, float& y) {
  const Theme& t = Theme::Current();
  Painter& p = ui_.p;
  const float cx = contentLeft_ + contentWidth_ / 2;
  y += 16.f;
  if (logo_) dc->DrawBitmap(logo_.Get(), RX(cx - 48.f, y, 96.f, 96.f), 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
  y += 108.f;
  p.Text(L"Opal", RX(contentLeft_, y, contentWidth_, 34.f), 26.f, t.text, FontWeight::Semibold, HAlign::Center);
  y += 36.f;
  p.Text(std::wstring(T(S::Version)) + L" " + OPAL_VERSION_WTEXT, RX(contentLeft_, y, contentWidth_, 18.f), 12.f,
         t.textSecondary, FontWeight::Regular, HAlign::Center);
  y += 26.f;
  p.Text(T(S::Tagline), RX(contentLeft_, y, contentWidth_, 20.f), 14.f, t.text, FontWeight::Medium, HAlign::Center);
  y += 30.f;
  const float bw = std::min(contentWidth_, 440.f);
  const float bh = p.Measure(T(S::AboutBody), 12.5f, FontWeight::Regular, bw).height;
  p.Text(T(S::AboutBody), RX(cx - bw / 2, y, bw, bh + 4.f), 12.5f, t.textSecondary, FontWeight::Regular,
         HAlign::Center, VAlign::Top, true);
  y += bh + 24.f;
  const float w1 = p.Measure(T(S::TrayOpenFolder), 13.f).width + 28.f;
  const float w2 = p.Measure(T(S::SettingsLog), 13.f).width + 28.f;
  float x = cx - (w1 + w2 + 10.f) / 2;
  if (ui_.Button(MakeId("a.folder"), RX(x, y, w1, 28.f), T(S::TrayOpenFolder))) {
    sys::OpenFolder(AppSettings().ResolvedFolder());
  }
  if (ui_.Button(MakeId("a.log"), RX(x + w1 + 10.f, y, w2, 28.f), T(S::SettingsLog))) {
    PWSTR base = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) {
      sys::OpenFile(std::wstring(base) + L"\\Opal\\opal.log");
      CoTaskMemFree(base);
    }
  }
  y += 44.f;
}

}
