#include "setup/SetupWindow.h"

#include <shellscalingapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>

#include <algorithm>

#include "Version.h"
#include "app/Settings.h"
#include "core/Log.h"
#include "core/Strings.h"
#include "setup/InstallRules.h"

namespace opal {
namespace {

constexpr float kW = 600.f, kH = 472.f;
constexpr float kHeader = 128.f;
constexpr float kFooter = 68.f;
constexpr float kCaption = 36.f;
constexpr float kMargin = 28.f;
constexpr double kMinWorkSeconds = 0.9;
constexpr UINT kMsgStep = WM_APP + 0x240;
constexpr UINT kMsgFinished = WM_APP + 0x241;
constexpr UINT_PTR kResultTimer = 0x0FB2;

std::wstring Format(S fmt, const std::wstring& a, const std::wstring& b = {}) {
  wchar_t buf[1024];
  swprintf_s(buf, T(fmt), a.c_str(), b.c_str());
  return buf;
}

std::wstring Ellipsize(Painter& p, const std::wstring& s, float size, float maxW) {
  if (p.Measure(s, size).width <= maxW) return s;
  size_t lo = 0, hi = s.size();
  std::wstring best = L"…";
  while (lo < hi) {
    const size_t keep = (lo + hi + 1) / 2;
    const size_t head = keep / 2, tail = keep - head;
    std::wstring cand = s.substr(0, head) + L"…" + s.substr(s.size() - tail);
    if (p.Measure(cand, size).width <= maxW) {
      best = std::move(cand);
      lo = keep;
    } else {
      hi = keep - 1;
    }
  }
  return best;
}

float StepFraction(setup::Step s) {
  switch (s) {
    case setup::Step::Stopping: return 0.18f;
    case setup::Step::Copying: return 0.5f;
    case setup::Step::Registering: return 0.82f;
    case setup::Step::Removing: return 0.6f;
    case setup::Step::Finished: return 1.f;
  }
  return 0.f;
}

S StepLabel(setup::Step s) {
  switch (s) {
    case setup::Step::Stopping: return S::SetupStepStopping;
    case setup::Step::Copying: return S::SetupStepCopying;
    case setup::Step::Registering: return S::SetupStepRegistering;
    case setup::Step::Removing: return S::SetupStepRemoving;
    case setup::Step::Finished: return S::SetupStepRegistering;
  }
  return S::SetupStepCopying;
}

SetupWindow::Kind KindFor(const setup::Installed& inst) {
  if (!inst.present) return SetupWindow::Kind::Fresh;
  const int c = setup::CompareVersions(inst.version, OPAL_VERSION_WTEXT);
  if (c < 0) return SetupWindow::Kind::Update;
  if (c == 0) return SetupWindow::Kind::Reinstall;
  return SetupWindow::Kind::Downgrade;
}

void LoadPreferences() {
  Settings& s = AppSettings();
  s.Load();
  SetLanguage(s.language.c_str());
  Theme::Refresh(s.theme);
}

int RunWindow(std::unique_ptr<SetupWindow> w) {
  D2DWindow::SetDestroyedHandler([](D2DWindow*) { PostQuitMessage(0); });
  if (!w->Init()) return 1;
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  const int code = w->ExitCode();
  w.reset();
  setup::ScheduleCleanup();
  return code;
}

}

SetupWindow::SetupWindow(Kind kind, setup::Installed installed)
    : kind_(kind), installed_(std::move(installed)), options_(setup::CurrentOptions(installed_)) {}

SetupWindow::~SetupWindow() {
  if (worker_.joinable()) worker_.join();
}

D2D1_SIZE_F SetupWindow::Size() { return {kW, kH}; }

bool SetupWindow::Init() {
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
  p.title = kind_ == Kind::Uninstall ? T(S::UninstallTitle) : T(S::SetupTitleInstall);
  p.rect = {mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2,
            mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - h) / 2, 0, 0};
  p.rect.right = p.rect.left + w;
  p.rect.bottom = p.rect.top + h;
  p.customFrame = true;
  p.mica = true;
  if (!Create(p)) return false;
  ApplyDarkFrame(Theme::Current().dark);
  RenderNow();
  ShowWindow(hwnd(), SW_SHOW);
  SetForegroundWindow(hwnd());
  return true;
}

void SetupWindow::Preview(Page page, setup::Step step, const std::wstring& error) {
  page_ = page;
  step_ = step;
  error_ = error;
}

D2D1_COLOR_F SetupWindow::ClearColor() {
  if (MicaSupported()) return {0, 0, 0, 0};
  return Theme::Current().windowBg;
}

LRESULT SetupWindow::HitTestClient(POINT pt) {
  const float s = Scale();
  const D2D1_POINT_2F dip{pt.x / s, pt.y / s};
  for (const auto& r : hitRects_) {
    if (InRect(r, dip)) return HTCLIENT;
  }
  return dip.y < kCaption ? HTCAPTION : HTCLIENT;
}

LRESULT SetupWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp, bool& handled) {
  handled = false;
  switch (msg) {
    case kMsgStep:
      step_ = setup::Step(wp);
      Invalidate();
      handled = true;
      return 0;
    case kMsgFinished: {
      result_.reset(reinterpret_cast<Result*>(lp));
      const double wait = std::max(0.0, kMinWorkSeconds - (Now() - workStart_));
      SetTimer(hwnd(), kResultTimer, UINT(wait * 1000.0) + 1, nullptr);
      handled = true;
      return 0;
    }
    case WM_TIMER:
      if (wp == kResultTimer) {
        KillTimer(hwnd(), kResultTimer);
        ApplyResult();
        handled = true;
        return 0;
      }
      break;
    case WM_CLOSE:
      if (page_ == Page::Working) {
        handled = true;
        return 0;
      }
      break;
    default:
      break;
  }
  return 0;
}

void SetupWindow::EnsureLogo(ID2D1DeviceContext5* dc) {
  if (logo_) return;
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

std::wstring SetupWindow::Title() const {
  const bool uninstall = kind_ == Kind::Uninstall;
  switch (page_) {
    case Page::Working:
      if (uninstall) return T(S::SetupWorkingUninstall);
      return T(kind_ == Kind::Fresh ? S::SetupWorkingInstall : S::SetupWorkingUpdate);
    case Page::Done:
      if (uninstall) return T(S::UninstallDone);
      return T(kind_ == Kind::Fresh ? S::SetupDoneInstall : S::SetupDoneUpdate);
    case Page::Failed:
      return T(uninstall && !installed_.present ? S::UninstallTitle : S::SetupFailed);
    case Page::Options:
      break;
  }
  switch (kind_) {
    case Kind::Fresh: return T(S::SetupTitleInstall);
    case Kind::Update: return T(S::SetupTitleUpdate);
    case Kind::Reinstall: return T(S::SetupTitleReinstall);
    case Kind::Downgrade: return T(S::SetupTitleDowngrade);
    case Kind::Uninstall: return T(S::UninstallTitle);
  }
  return {};
}

std::wstring SetupWindow::Subtitle() const {
  const std::wstring version = OPAL_VERSION_WTEXT;
  if (page_ == Page::Failed) return error_;
  if (kind_ == Kind::Uninstall) {
    if (page_ == Page::Done) return T(S::UninstallDoneLine);
    return Format(S::UninstallLine, installed_.version, installed_.dir);
  }
  if (page_ == Page::Done) return T(S::SetupDoneLine);
  if (page_ == Page::Options) {
    switch (kind_) {
      case Kind::Update: return Format(S::SetupLineUpdate, installed_.version, version);
      case Kind::Reinstall: return Format(S::SetupLineReinstall, version);
      case Kind::Downgrade: return Format(S::SetupLineDowngrade, installed_.version, version);
      default: break;
    }
  }
  return Format(S::SetupLineInstall, version);
}

void SetupWindow::DrawHeader(ID2D1DeviceContext5* dc) {
  const Theme& t = Theme::Current();
  Painter& p = ui_.p;
  p.FillRect({0, 0, kW, kHeader}, t.sidebarBg);
  p.Line({0, kHeader - 0.5f}, {kW, kHeader - 0.5f}, t.separator, 1.f);
  const D2D1_RECT_F logoRect = RX(kMargin, 38.f, 64.f, 64.f);
  if (logo_) dc->DrawBitmap(logo_.Get(), logoRect, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
  if (page_ == Page::Done) {
    const D2D1_RECT_F badge = RX(logoRect.right - 20.f, logoRect.bottom - 20.f, 24.f, 24.f);
    p.SoftShadow(badge, 12.f, 3.f, 0.25f, 1.f);
    p.FillRound(badge, 12.f, t.accent);
    p.StrokeRound(Inset(badge, -1.f), 13.f, t.dark ? Hex(0x1E1E1E) : Hex(0xFFFFFF), 2.f);
    p.DrawIcon(Icon::Check, Inset(badge, 5.f), t.onAccent, 2.f);
  }
  const float x = logoRect.right + 20.f;
  const float w = kW - x - kMargin;
  p.Text(Title(), RX(x, 40.f, w, 32.f), 22.f, t.text, FontWeight::Semibold);
  const std::wstring line = Subtitle();
  p.Text(line, RX(x, 76.f, w, kHeader - 82.f), 12.5f, page_ == Page::Failed ? t.destructive : t.textSecondary,
         FontWeight::Regular, HAlign::Left, VAlign::Top, true);
}

float SetupWindow::Card(float y, const std::vector<Row>& rows) {
  const Theme& t = Theme::Current();
  Painter& p = ui_.p;
  const float left = kMargin, width = kW - 2 * kMargin;
  std::vector<float> heights;
  float total = 0;
  for (const auto& r : rows) {
    float h = 44.f;
    if (!r.desc.empty()) {
      const float dw = width - 48.f - r.controlWidth;
      h = std::max(44.f, 16.f + 18.f + p.Measure(r.desc, 11.5f, FontWeight::Regular, dw).height + 12.f);
    }
    heights.push_back(h);
    total += h;
  }
  const D2D1_RECT_F card = RX(left, y, width, total);
  p.FillRound(card, 10.f, t.cardBg);
  p.StrokeRound(card, 10.f, t.cardBorder, 1.f);
  float ry = y;
  for (size_t i = 0; i < rows.size(); ++i) {
    const Row& r = rows[i];
    const float h = heights[i];
    const float dw = width - 48.f - r.controlWidth;
    if (r.desc.empty()) {
      p.Text(r.title, RX(left + 16.f, ry, dw, h), 13.f, t.text);
    } else {
      p.Text(r.title, RX(left + 16.f, ry + 12.f, dw, 18.f), 13.f, t.text);
      p.Text(r.desc, RX(left + 16.f, ry + 31.f, dw, h - 36.f), 11.5f, t.textSecondary, FontWeight::Regular,
             HAlign::Left, VAlign::Top, true);
    }
    if (r.control) {
      r.control(RX(left + width - 16.f - r.controlWidth, ry + (h - r.controlHeight) / 2, r.controlWidth,
                   r.controlHeight));
    }
    if (i + 1 < rows.size()) {
      p.Line({left + 16.f, ry + h - 0.5f}, {left + width - 16.f, ry + h - 0.5f}, t.separator, 1.f);
    }
    ry += h;
  }
  return y + total + 12.f;
}

void SetupWindow::PageOptions() {
  float y = kHeader + 20.f;
  if (kind_ == Kind::Uninstall) {
    Card(y, {{T(S::UninstallUserData), T(S::UninstallUserDataDesc), 38.f, 22.f, [this](const D2D1_RECT_F& r) {
                ui_.Toggle(MakeId("s.data"), r, &removeData_);
              }}});
    return;
  }
  const bool movable = kind_ == Kind::Fresh;
  const float pathW = kW - 2 * kMargin - 48.f - (movable ? 104.f : 0.f);
  y = Card(y, {{T(S::SetupLocation), Ellipsize(ui_.p, options_.dir, 11.5f, pathW) + L"\n" + SpaceText(),
                movable ? 104.f : 0.f, 28.f,
                movable ? std::function<void(const D2D1_RECT_F&)>([this](const D2D1_RECT_F& r) {
                  if (ui_.Button(MakeId("s.change"), r, T(S::Change))) AfterFrame([this] { PickFolder(); });
                })
                        : nullptr}});
  Card(y, {
              {T(S::SetupAutostart), T(S::SetupAutostartDesc), 38.f, 22.f,
               [this](const D2D1_RECT_F& r) { ui_.Toggle(MakeId("s.auto"), r, &options_.autostart); }},
              {T(S::SetupStartMenu), L"", 38.f, 22.f,
               [this](const D2D1_RECT_F& r) { ui_.Toggle(MakeId("s.start"), r, &options_.startMenu); }},
              {T(S::SetupDesktop), L"", 38.f, 22.f,
               [this](const D2D1_RECT_F& r) { ui_.Toggle(MakeId("s.desk"), r, &options_.desktop); }},
          });
}

void SetupWindow::PageWorking() {
  const Theme& t = Theme::Current();
  Painter& p = ui_.p;
  const float top = kHeader + (kH - kHeader - kFooter) / 2 - 24.f;
  p.Text(T(StepLabel(step_)), RX(kMargin + 20.f, top, kW - 2 * kMargin - 40.f, 20.f), 13.f, t.text);
  const D2D1_RECT_F track = RX(kMargin + 20.f, top + 30.f, kW - 2 * kMargin - 40.f, 6.f);
  p.FillRound(track, 3.f, t.toggleOff);
  const float frac = ui_.AnimFrom(MakeId("s.progress"), 0.f, StepFraction(step_), 6.f);
  if (frac > 0.005f) p.FillRound(RX(track.left, track.top, RW(track) * frac, RH(track)), 3.f, t.accent);
}

void SetupWindow::PageDone() {
  if (kind_ == Kind::Uninstall) return;
  const Theme& t = Theme::Current();
  float y = kHeader + 20.f;
  const std::wstring key = CaptureKey();
  if (!key.empty()) {
    y = Card(y, {{T(S::ShortcutBar), L"", 160.f, 26.f, [this, &t, &key](const D2D1_RECT_F& r) {
                  Painter& p = ui_.p;
                  const float w = p.Measure(key, 11.5f, FontWeight::Medium).width + 16.f;
                  const D2D1_RECT_F cap = RX(r.right - w, r.top + 1.f, w, RH(r) - 2.f);
                  p.FillRound(cap, 5.f, t.dark ? Hex(0xFFFFFF, 0.12f) : Hex(0x000000, 0.06f));
                  p.StrokeRound(cap, 5.f, t.dark ? Hex(0xFFFFFF, 0.08f) : Hex(0x000000, 0.08f), 0.75f);
                  p.Text(key, cap, 11.5f, t.text, FontWeight::Medium, HAlign::Center);
                }}});
  }
  Card(y, {{T(S::SetupLaunch), L"", 38.f, 22.f,
            [this](const D2D1_RECT_F& r) { ui_.Toggle(MakeId("s.launch"), r, &launch_); }}});
}

void SetupWindow::PageFailed() {}

const std::wstring& SetupWindow::SpaceText() {
  if (spaceDir_ == options_.dir && !spaceText_.empty()) return spaceText_;
  spaceDir_ = options_.dir;
  const wchar_t point = CurrentLanguage() == Lang::Tr ? L',' : L'.';
  const std::wstring needed = setup::FormatSize(setup::NeededBytes(), point);
  wchar_t buf[256];
  uint64_t free = 0;
  if (setup::FreeBytes(options_.dir, &free)) {
    const std::wstring drive = setup::DriveOf(options_.dir);
    _swprintf_p(buf, ARRAYSIZE(buf), T(S::SetupSpace), needed.c_str(), setup::FormatSize(free, point).c_str(),
                drive.empty() ? options_.dir.c_str() : drive.c_str());
  } else {
    swprintf_s(buf, T(S::SetupSpaceNeeded), needed.c_str());
  }
  spaceText_ = buf;
  return spaceText_;
}

std::wstring SetupWindow::CaptureKey() const {
  const Settings& s = AppSettings();
  const Hotkey& hk = s.hotkeys[size_t(Action::Bar)];
  if (hk.Valid() && (hk.vk != VK_SNAPSHOT || hk.mods != 0 || s.usePrintScreen)) return FormatHotkey(hk);
  if (s.useWinShiftS) return L"Win+Shift+S";
  return {};
}

void SetupWindow::DrawFooter() {
  const Theme& t = Theme::Current();
  Painter& p = ui_.p;
  const float top = kH - kFooter;
  p.FillRect({0, top, kW, kH}, t.sidebarBg);
  p.Line({0, top + 0.5f}, {kW, top + 0.5f}, t.separator, 1.f);
  if (page_ == Page::Working) return;

  struct Btn {
    const wchar_t* label;
    bool primary;
    int action;
  };
  std::vector<Btn> buttons;
  const bool uninstall = kind_ == Kind::Uninstall;
  if (page_ == Page::Options) {
    if (kind_ == Kind::Reinstall) buttons.push_back({T(S::SetupUninstall), false, 3});
    buttons.push_back({T(S::Cancel), false, 2});
    const S primary = uninstall               ? S::SetupUninstall
                      : kind_ == Kind::Update ? S::SetupUpdate
                      : kind_ == Kind::Reinstall ? S::SetupReinstall
                                                 : S::SetupInstall;
    buttons.push_back({T(primary), true, 1});
  } else {
    buttons.push_back({T(page_ == Page::Done && !uninstall ? S::Done : S::Close), true, 1});
  }

  const float h = 32.f;
  const float by = top + (kFooter - h) / 2;
  float x = kW - 24.f;
  int clicked = 0;
  for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
    const float w = std::max(96.f, p.Measure(it->label, 13.f, FontWeight::Medium).width + 36.f);
    x -= w;
    if (ui_.Button(MakeId("s.btn", uint64_t(it->action)), RX(x, by, w, h), it->label, it->primary)) {
      clicked = it->action;
    }
    x -= 8.f;
  }
  if (ui_.in.KeyPressed(VK_RETURN)) clicked = 1;
  if (ui_.in.KeyPressed(VK_ESCAPE)) clicked = 2;
  if (clicked == 1) {
    AfterFrame([this] { Primary(); });
  } else if (clicked == 2) {
    AfterFrame([this] { Cancel(); });
  } else if (clicked == 3) {
    kind_ = Kind::Uninstall;
    removeData_ = false;
  }
}

void SetupWindow::OnRender(ID2D1DeviceContext5* dc) {
  EnsureLogo(dc);
  BeginUiFrame(dc);
  hitRects_.clear();
  const Theme& t = Theme::Current();
  ui_.p.FillRect({0, 0, kW, kH}, t.contentBg);
  DrawHeader(dc);
  switch (page_) {
    case Page::Options: PageOptions(); break;
    case Page::Working: PageWorking(); break;
    case Page::Done: PageDone(); break;
    case Page::Failed: PageFailed(); break;
  }
  DrawFooter();
  if (page_ != Page::Working) {
    float capLeft = 0;
    const int cap = ui_.CaptionButtons(MakeId("s.caption"), kW, 0.f, 32.f, false, false, &capLeft);
    hitRects_.push_back({capLeft, 0, kW, 32.f});
    if (cap == 1 && hwnd()) ShowWindow(hwnd(), SW_MINIMIZE);
    else if (cap == 3) AfterFrame([this] { Cancel(); });
  }
  EndUiFrame(dc);
}

void SetupWindow::PickFolder() {
  ModalScope modal(this);
  ComPtr<IFileOpenDialog> dlg;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
  DWORD opts = 0;
  dlg->GetOptions(&opts);
  dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
  const size_t slash = options_.dir.find_last_of(L'\\');
  const std::wstring parent = slash == std::wstring::npos ? options_.dir : options_.dir.substr(0, slash);
  ComPtr<IShellItem> start;
  if (SUCCEEDED(SHCreateItemFromParsingName(parent.c_str(), nullptr, IID_PPV_ARGS(&start)))) dlg->SetFolder(start.Get());
  if (FAILED(dlg->Show(hwnd()))) return;
  ComPtr<IShellItem> item;
  PWSTR path = nullptr;
  if (SUCCEEDED(dlg->GetResult(&item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
    options_.dir = setup::NormalizeInstallDir(path);
    CoTaskMemFree(path);
  }
  Invalidate();
}

void SetupWindow::StartWork() {
  page_ = Page::Working;
  step_ = setup::Step::Stopping;
  workStart_ = Now();
  HWND h = hwnd();
  worker_ = std::thread([h, kind = kind_, options = options_, inst = installed_, removeData = removeData_] {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    auto progress = [h](setup::Step s) { PostMessageW(h, kMsgStep, WPARAM(s), 0); };
    auto* r = new Result;
    if (kind == Kind::Uninstall) {
      r->ok = setup::Uninstall(inst, removeData, progress);
    } else {
      r->ok = setup::Install(options, progress, &r->error, &r->wasRunning, h);
    }
    CoUninitialize();
    PostMessageW(h, kMsgFinished, 0, reinterpret_cast<LPARAM>(r));
  });
  Invalidate();
}

void SetupWindow::ApplyResult() {
  if (worker_.joinable()) worker_.join();
  if (!result_) return;
  if (result_->ok) {
    page_ = Page::Done;
    if (kind_ != Kind::Fresh) launch_ = result_->wasRunning;
  } else {
    page_ = Page::Failed;
    error_ = result_->error;
    Log(L"Setup failed: %s", error_.c_str());
  }
  result_.reset();
  Invalidate();
}

void SetupWindow::Primary() {
  switch (page_) {
    case Page::Options:
      StartWork();
      return;
    case Page::Done:
      exitCode_ = 0;
      if (kind_ != Kind::Uninstall && launch_) setup::Launch(options_.dir, kind_ != Kind::Fresh);
      Close();
      return;
    case Page::Failed:
      exitCode_ = 1;
      Close();
      return;
    case Page::Working:
      return;
  }
}

void SetupWindow::Cancel() {
  if (page_ == Page::Working) return;
  if (page_ == Page::Done) {
    exitCode_ = 0;
  } else if (page_ == Page::Failed) {
    exitCode_ = 1;
  }
  Close();
}

int RunSetup(const std::wstring& args) {
  LogInit(L"setup");
  LoadPreferences();
  const setup::Installed inst = setup::FindInstalled();
  Log(L"Setup %s, installed: %s %s", OPAL_VERSION_WTEXT, inst.present ? inst.version.c_str() : L"none",
      inst.dir.c_str());
  if (setup::HasArg(args, L"--silent")) {
    setup::InstallOptions o = setup::CurrentOptions(inst);
    if (!inst.present) {
      if (auto dir = setup::ArgValue(args, L"--dir")) o.dir = setup::NormalizeInstallDir(*dir);
    }
    std::wstring error;
    bool wasRunning = false;
    if (!setup::Install(o, [](setup::Step) {}, &error, &wasRunning, nullptr)) {
      Log(L"Setup failed: %s", error.c_str());
      return 1;
    }
    if (wasRunning || setup::HasArg(args, L"--launch")) setup::Launch(o.dir, true);
    return 0;
  }
  return RunWindow(std::make_unique<SetupWindow>(KindFor(inst), inst));
}

int RunUninstall(const std::wstring& args) {
  const setup::Installed inst = setup::FindInstalled();
  if (inst.present && !setup::IsElevated() && !setup::CanWriteTo(inst.dir)) {
    return int(setup::RunElevated(args, nullptr));
  }
  LogInit(L"setup");
  LoadPreferences();
  Log(L"Uninstall, installed: %s %s", inst.present ? inst.version.c_str() : L"none", inst.dir.c_str());
  if (setup::HasArg(args, L"--silent")) {
    if (!inst.present) return 1;
    setup::Uninstall(inst, setup::HasArg(args, L"--remove-data"), [](setup::Step) {});
    setup::ScheduleCleanup();
    return 0;
  }
  auto w = std::make_unique<SetupWindow>(SetupWindow::Kind::Uninstall, inst);
  if (!inst.present) w->Preview(SetupWindow::Page::Failed, setup::Step::Finished, T(S::UninstallMissing));
  return RunWindow(std::move(w));
}

}
