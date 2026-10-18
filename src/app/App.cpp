#include "app/App.h"

#include <shellapi.h>
#include <wrl/client.h>
#include <wtsapi32.h>

#include <algorithm>
#include <thread>

#include "app/Hotkeys.h"
#include "app/Sound.h"
#include "app/SystemIntegration.h"
#include "app/Updater.h"
#include "core/Log.h"
#include "core/Strings.h"
#include "editor/EditorWindow.h"
#include "output/Output.h"
#include "render/D2DWindow.h"
#include "render/Gpu.h"
#include "settingsui/SettingsWindow.h"
#include "setup/InstallRules.h"
#include "setup/Installer.h"
#include "thumbnail/ThumbnailWindow.h"
#include "ui/Theme.h"
#include "Version.h"

namespace opal {
namespace {

constexpr UINT kMsgHotkey = WM_APP + 1;
constexpr UINT kMsgTray = WM_APP + 2;
constexpr UINT kMsgSessionDone = WM_APP + 3;
constexpr UINT kMsgDelivered = WM_APP + 4;
constexpr UINT kMsgUpdate = WM_APP + 5;
constexpr UINT_PTR kTrimTimer = 1;
constexpr UINT_PTR kUpdateTimer = 2;
constexpr UINT kFirstUpdateCheckMs = 60 * 1000;
constexpr UINT kUpdateTimerMs = 60 * 60 * 1000;

std::wstring Formatted(S fmt, const std::wstring& arg) {
  wchar_t buf[256];
  swprintf_s(buf, T(fmt), arg.c_str());
  return buf;
}

struct Delivery {
  ThumbnailWindow* thumbnail;
  std::wstring path;
  bool saved;
};

}

App& App::Get() {
  static App app;
  return app;
}

int App::Run(const std::wstring& args) {
  Settings& s = AppSettings();
  s.Load();
  SetLanguage(s.language.c_str());
  Theme::Refresh(s.theme);

  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = &App::WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = kAppWindowClass;
  wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
  RegisterClassExW(&wc);
  hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kAppWindowClass, L"Opal", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                          wc.hInstance, nullptr);
  if (!hwnd_) return 1;
  taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
  ChangeWindowMessageFilterEx(hwnd_, taskbarCreated_, MSGFLT_ALLOW, nullptr);
  ChangeWindowMessageFilterEx(hwnd_, WM_COPYDATA, MSGFLT_ALLOW, nullptr);

  D2DWindow::SetDestroyedHandler([this](D2DWindow* w) { OnWindowDestroyed(w); });
  tray_.Create(hwnd_, kMsgTray);
  WTSRegisterSessionNotification(hwnd_, NOTIFY_FOR_THIS_SESSION);
  if (!HotkeyManager::Get().Start(hwnd_, kMsgHotkey)) Log(L"Keyboard hook unavailable");
  HotkeyManager::Get().SetBindings(s.hotkeys, s.usePrintScreen, s.useWinShiftS);
  SetTimer(hwnd_, kTrimTimer, 10000, nullptr);
  Updater::Get().Init(hwnd_, kMsgUpdate);
  if (Updater::Get().Supported()) SetTimer(hwnd_, kUpdateTimer, kFirstUpdateCheckMs, nullptr);

  setup::CleanUpAfterUpdate();
  SyncLaunchAtStartup();
  if (s.firstRun) s.Save();
  Log(L"Opal %s started from %s (first run %d, installed %d, startup %d)", OPAL_VERSION_WTEXT, sys::ExePath().c_str(),
      int(s.firstRun), int(setup::IsInstalledCopy()), int(s.launchAtStartup));
  HandleCommandLine(args, false);
  if (s.firstRun && args.find(L"--") == std::wstring::npos) OpenSettings();

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  HotkeyManager::Get().Stop();
  WTSUnRegisterSessionNotification(hwnd_);
  session_.reset();
  windows_.clear();
  tray_.Destroy();
  return 0;
}

void App::OnUpdateEvent(Updater::Event e) {
  Settings& s = AppSettings();
  const Updater& updater = Updater::Get();
  if (e == Updater::Event::Available && s.notifiedVersion != updater.Version()) {
    tray_.Notify(Formatted(S::UpdateBalloonTitle, updater.Version()), T(S::UpdateBalloonText));
    s.notifiedVersion = updater.Version();
    s.Save();
  } else if (e == Updater::Event::Failed) {
    tray_.Notify(L"Opal", T(S::UpdateFailedBalloon));
  }
  RefreshAllWindows();
}

void App::SyncLaunchAtStartup() {
  if (sys::IsLaunchAtStartup()) {
    const std::wstring target = sys::LaunchAtStartupTarget();
    const std::wstring self = sys::ExePath();
    if (!setup::SamePath(target, self) &&
        (setup::IsInstalledCopy() || GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES)) {
      Log(L"Startup entry pointed to %s, moving it to this copy", target.c_str());
      sys::SetLaunchAtStartup(true, self);
    }
  }
  AppSettings().launchAtStartup = sys::IsLaunchAtStartup();
}

void App::HandleCommandLine(const std::wstring& args, bool fromOtherInstance) {
  auto has = [&](const wchar_t* flag) { return args.find(flag) != std::wstring::npos; };
  if (has(L"--quit")) {
    Quit();
  } else if (has(L"--bar")) {
    StartCapture(Action::Bar);
  } else if (has(L"--region")) {
    StartCapture(Action::Region);
  } else if (has(L"--window")) {
    StartCapture(Action::Window);
  } else if (has(L"--element")) {
    StartCapture(Action::Element);
  } else if (has(L"--screen")) {
    StartCapture(Action::Screen);
  } else if (has(L"--settings") || (fromOtherInstance && !has(L"--background"))) {
    OpenSettings();
  }
}

void App::Quit() { DestroyWindow(hwnd_); }

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  App& app = Get();
  if (!app.hwnd_) app.hwnd_ = hwnd;
  return app.Handle(msg, wp, lp);
}

LRESULT App::Handle(UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == taskbarCreated_ && taskbarCreated_) {
    tray_.Recreate();
    return 0;
  }
  switch (msg) {
    case kMsgHotkey:
      StartCapture(Action(wp));
      return 0;
    case kMsgTray:
      switch (LOWORD(lp)) {
        case NIN_BALLOONUSERCLICK:
          Updater::Get().Install();
          RefreshAllWindows();
          break;
        case NIN_SELECT:
        case NIN_KEYSELECT:
        case WM_CONTEXTMENU: {
          Updater& updater = Updater::Get();
          std::wstring updateLabel;
          if (updater.GetState() == Updater::State::Available || updater.GetState() == Updater::State::DownloadFailed) {
            updateLabel = Formatted(S::TrayUpdate, updater.Version());
          } else if (updater.GetState() == Updater::State::Downloading ||
                     updater.GetState() == Updater::State::Installing) {
            updateLabel = T(S::TrayUpdating);
          }
          const UINT cmd = tray_.ShowMenu(updateLabel, !updater.Busy());
          switch (cmd) {
            case kCmdBar: StartCapture(Action::Bar); break;
            case kCmdScreen: StartCapture(Action::Screen); break;
            case kCmdRegion: StartCapture(Action::Region); break;
            case kCmdWindow: StartCapture(Action::Window); break;
            case kCmdElement: StartCapture(Action::Element); break;
            case kCmdOpenFolder: sys::OpenFolder(AppSettings().ResolvedFolder()); break;
            case kCmdSettings: OpenSettings(); break;
            case kCmdQuit: Quit(); break;
            case kCmdUpdate:
              updater.Install();
              RefreshAllWindows();
              break;
            default: break;
          }
          break;
        }
        default:
          break;
      }
      return 0;
    case kMsgSessionDone:
      OnSessionDone();
      return 0;
    case kMsgUpdate:
      OnUpdateEvent(Updater::Get().OnMessage(wp, lp));
      return 0;
    case kMsgDelivered: {
      std::unique_ptr<Delivery> d(reinterpret_cast<Delivery*>(lp));
      if (d->thumbnail && d->thumbnail == thumbnail_ && d->saved) thumbnail_->SetPath(d->path);
      return 0;
    }
    case WM_COPYDATA: {
      const auto* cds = reinterpret_cast<const COPYDATASTRUCT*>(lp);
      std::wstring args;
      if (cds && cds->lpData && cds->cbData) {
        args.assign(static_cast<const wchar_t*>(cds->lpData), cds->cbData / sizeof(wchar_t));
      }
      HandleCommandLine(args, true);
      return TRUE;
    }
    case WM_SETTINGCHANGE:
      if (lp && wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0) {
        Theme::Refresh(AppSettings().theme);
        RefreshAllWindows();
      }
      return 0;
    case WM_WTSSESSION_CHANGE:
      if (wp == WTS_SESSION_UNLOCK || wp == WTS_CONSOLE_CONNECT || wp == WTS_REMOTE_CONNECT) {
        HotkeyManager::Get().Refresh();
      }
      return 0;
    case WM_POWERBROADCAST:
      if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) HotkeyManager::Get().Refresh();
      return TRUE;
    case WM_DISPLAYCHANGE:
      HotkeyManager::Get().Refresh();
      break;
    case WM_TIMER:
      if (wp == kUpdateTimer) {
        SetTimer(hwnd_, kUpdateTimer, kUpdateTimerMs, nullptr);
        Updater::Get().CheckIfDue();
      } else if (wp == kTrimTimer) {
        Gpu::Get().TrimIfIdle(20000);
        if (session_ || !windows_.empty() || Gpu::Get().D3D()) {
          trimmed_ = false;
        } else if (!trimmed_) {
          SetProcessWorkingSetSize(GetCurrentProcess(), SIZE_T(-1), SIZE_T(-1));
          trimmed_ = true;
        }
      }
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd_, msg, wp, lp);
}

void App::StartCapture(Action a) {
  if (session_) {
    if (session_->Finished()) return;
    switch (a) {
      case Action::Region: session_->SwitchMode(CaptureMode::Region); break;
      case Action::Window: session_->SwitchMode(CaptureMode::Window); break;
      case Action::Element: session_->SwitchMode(CaptureMode::Element); break;
      case Action::Screen: session_->SwitchMode(CaptureMode::Screen); break;
      default: break;
    }
    return;
  }
  const double t0 = D2DWindow::Now();
  if (a == Action::Screen) {
    if (auto r = CaptureSession::CaptureScreenNow()) HandleResult(std::move(*r));
    return;
  }
  CaptureMode mode = CaptureMode::Region;
  switch (a) {
    case Action::Bar: mode = AppSettings().lastBarMode; break;
    case Action::Window: mode = CaptureMode::Window; break;
    case Action::Element: mode = CaptureMode::Element; break;
    default: break;
  }
  pending_.reset();
  sessionDone_ = false;
  session_ = std::make_unique<CaptureSession>(mode, a == Action::Bar, [this](std::optional<CaptureResult> r) {
    pending_ = std::move(r);
    sessionDone_ = true;
    PostMessageW(hwnd_, kMsgSessionDone, 0, 0);
  });
  if (!session_->Start()) {
    Log(L"Capture session failed to start");
    session_.reset();
    MessageBeep(MB_ICONWARNING);
    return;
  }
  Log(L"Overlay visible after %.1f ms", (D2DWindow::Now() - t0) * 1000.0);
}

void App::OnSessionDone() {
  if (!sessionDone_) return;
  sessionDone_ = false;
  std::unique_ptr<CaptureSession> done = std::move(session_);
  std::optional<CaptureResult> r = std::move(pending_);
  pending_.reset();
  done.reset();
  if (r) HandleResult(std::move(*r));
}

void App::HandleResult(CaptureResult&& r) {
  Settings& s = AppSettings();
  if (s.playSound) PlayShutterSound(s.shutterSound);
  std::wstring path;
  if (s.saveToFile) {
    const std::wstring folder = s.ResolvedFolder();
    sys::EnsureDirectory(folder);
    path = output::MakeCapturePath(folder, s.filePrefix);
  }
  const float scale = float(r.monitorDpi) / 96.f;
  ThumbnailWindow* thumb = nullptr;
  if (s.showThumbnail) {
    if (thumbnail_) thumbnail_->Dismiss(false);
    MONITORINFO mi{sizeof(mi)};
    RECT mr = ToRECT(r.monitorRect);
    HMONITOR mon = MonitorFromRect(&mr, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfoW(mon, &mi);
    auto w = std::make_unique<ThumbnailWindow>(r.image, ToRectI(mi.rcWork), r.monitorDpi, scale);
    if (w->Init()) {
      thumb = w.get();
      thumbnail_ = thumb;
      Adopt(std::move(w));
    }
  }
  Deliver(r.image, path, s.copyToClipboard, thumb);
  Log(L"Captured %dx%d (mode %d)", r.image.width, r.image.height, int(r.mode));
}

void App::Deliver(const Image& img, const std::wstring& path, bool clipboard, ThumbnailWindow* notify) {
  if (path.empty() && !clipboard) return;
  HWND owner = hwnd_;
  std::thread([img, path, clipboard, notify, owner] {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
      Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
      CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
      std::vector<uint8_t> png;
      const bool encoded = wic && output::EncodePng(img, &png, wic.Get());
      bool saved = false;
      if (encoded && !path.empty()) {
        saved = output::WriteFileBytes(path, png);
        if (!saved) Log(L"Writing %s failed: %lu", path.c_str(), GetLastError());
      }
      if (clipboard) output::CopyImage(nullptr, img, encoded ? &png : nullptr);
      PostMessageW(owner, kMsgDelivered, 0, reinterpret_cast<LPARAM>(new Delivery{notify, path, saved}));
    }
    CoUninitialize();
  }).detach();
}

void App::OpenSettings() {
  if (settings_) {
    settings_->Activate();
    return;
  }
  auto w = std::make_unique<SettingsWindow>();
  if (w->Init()) {
    settings_ = w.get();
    Adopt(std::move(w));
  }
}

void App::OpenEditor(const Image& image, const std::wstring& path, float captureScale) {
  POINT pt;
  GetCursorPos(&pt);
  MONITORINFO mi{sizeof(mi)};
  GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
  auto w = std::make_unique<EditorWindow>(image, path, captureScale);
  if (w->Init(ToRectI(mi.rcWork))) Adopt(std::move(w));
}

void App::ApplySettings() {
  Settings& s = AppSettings();
  SetLanguage(s.language.c_str());
  Theme::Refresh(s.theme);
  HotkeyManager::Get().SetBindings(s.hotkeys, s.usePrintScreen, s.useWinShiftS);
  if (s.launchAtStartup != sys::IsLaunchAtStartup()) sys::SetLaunchAtStartup(s.launchAtStartup);
  s.Save();
  RefreshAllWindows();
}

void App::RefreshAllWindows() {
  for (auto& w : windows_) w->Invalidate();
}

void App::Adopt(std::unique_ptr<D2DWindow> w) { windows_.push_back(std::move(w)); }

void App::OnWindowDestroyed(D2DWindow* w) {
  auto it = std::find_if(windows_.begin(), windows_.end(), [w](const auto& p) { return p.get() == w; });
  if (it == windows_.end() || (*it)->Alive()) return;
  if (w == thumbnail_) thumbnail_ = nullptr;
  if (w == settings_) settings_ = nullptr;
  windows_.erase(it);
}

}
