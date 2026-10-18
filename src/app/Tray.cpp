#include "app/Tray.h"

#include <commctrl.h>
#include <shellapi.h>

#include <string>

#include "app/Settings.h"
#include "app/SystemIntegration.h"
#include "core/Strings.h"
#include "ui/Theme.h"

namespace opal {
namespace {

constexpr GUID kTrayGuid = {0x6c1f3a52, 0x8d3b, 0x4e0e, {0x9a, 0x51, 0x0f, 0x3e, 0x51, 0x7a, 0x2d, 0x11}};

std::wstring WithShortcut(S label, Action a) {
  std::wstring s = T(label);
  const Hotkey& hk = AppSettings().hotkeys[size_t(a)];
  if (hk.Valid()) s += L"\t" + FormatHotkey(hk);
  return s;
}

}

bool Tray::Create(HWND owner, UINT callbackMsg) {
  owner_ = owner;
  msg_ = callbackMsg;
  NOTIFYICONDATAW nid{sizeof(nid)};
  nid.hWnd = owner_;
  nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_GUID | NIF_SHOWTIP;
  nid.guidItem = kTrayGuid;
  nid.uCallbackMessage = msg_;
  LoadIconMetric(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), LIM_SMALL, &nid.hIcon);
  wcscpy_s(nid.szTip, L"Opal");
  Shell_NotifyIconW(NIM_DELETE, &nid);
  added_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
  if (added_) {
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
  }
  if (nid.hIcon) DestroyIcon(nid.hIcon);
  return added_;
}

void Tray::Destroy() {
  if (!added_) return;
  NOTIFYICONDATAW nid{sizeof(nid)};
  nid.hWnd = owner_;
  nid.uFlags = NIF_GUID;
  nid.guidItem = kTrayGuid;
  Shell_NotifyIconW(NIM_DELETE, &nid);
  added_ = false;
}

void Tray::Recreate() {
  added_ = false;
  Create(owner_, msg_);
}

void Tray::Notify(const std::wstring& title, const std::wstring& text) {
  if (!added_) return;
  NOTIFYICONDATAW nid{sizeof(nid)};
  nid.hWnd = owner_;
  nid.uFlags = NIF_INFO | NIF_GUID;
  nid.guidItem = kTrayGuid;
  wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
  wcsncpy_s(nid.szInfo, text.c_str(), _TRUNCATE);
  nid.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
  LoadIconMetric(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), LIM_LARGE, &nid.hBalloonIcon);
  Shell_NotifyIconW(NIM_MODIFY, &nid);
  if (nid.hBalloonIcon) DestroyIcon(nid.hBalloonIcon);
}

UINT Tray::ShowMenu(const std::wstring& updateLabel, bool updateEnabled) {
  sys::SetDarkMenus(Theme::Current().dark);
  HMENU menu = CreatePopupMenu();
  if (!updateLabel.empty()) {
    AppendMenuW(menu, MF_STRING | (updateEnabled ? 0 : MF_GRAYED), kCmdUpdate, updateLabel.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  }
  AppendMenuW(menu, MF_STRING, kCmdBar, WithShortcut(S::TrayCapture, Action::Bar).c_str());
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kCmdScreen, WithShortcut(S::TrayScreen, Action::Screen).c_str());
  AppendMenuW(menu, MF_STRING, kCmdRegion, WithShortcut(S::TrayRegion, Action::Region).c_str());
  AppendMenuW(menu, MF_STRING, kCmdWindow, WithShortcut(S::TrayWindow, Action::Window).c_str());
  AppendMenuW(menu, MF_STRING, kCmdElement, WithShortcut(S::TrayElement, Action::Element).c_str());
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kCmdOpenFolder, T(S::TrayOpenFolder));
  AppendMenuW(menu, MF_STRING, kCmdSettings, T(S::TraySettings));
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kCmdQuit, T(S::TrayQuit));
  SetMenuDefaultItem(menu, kCmdBar, FALSE);

  POINT pt;
  GetCursorPos(&pt);
  SetForegroundWindow(owner_);
  UINT cmd = UINT(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                                   pt.x, pt.y, owner_, nullptr));
  PostMessageW(owner_, WM_NULL, 0, 0);
  DestroyMenu(menu);
  return cmd;
}

}
