#include "app/SystemIntegration.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

namespace opal::sys {
namespace {

constexpr int kAppModeForceDark = 2;
constexpr int kAppModeForceLight = 3;
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"Opal";

}

std::wstring ExePath() {
  wchar_t buf[MAX_PATH * 2];
  DWORD n = GetModuleFileNameW(nullptr, buf, ARRAYSIZE(buf));
  return std::wstring(buf, n);
}

bool IsLaunchAtStartup() {
  wchar_t buf[MAX_PATH * 2];
  DWORD size = sizeof(buf);
  return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS;
}

void SetLaunchAtStartup(bool enable) {
  HKEY key;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
  if (enable) {
    std::wstring cmd = L"\"" + ExePath() + L"\" --background";
    RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                   DWORD((cmd.size() + 1) * sizeof(wchar_t)));
  } else {
    RegDeleteValueW(key, kRunValue);
  }
  RegCloseKey(key);
}

bool IsWindowsSnipKeyEnabled() {
  DWORD v = 1, size = sizeof(v);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Keyboard", L"PrintScreenKeyForSnippingEnabled",
                   RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS) {
    return true;
  }
  return v != 0;
}

void SetWindowsSnipKeyEnabled(bool enable) {
  HKEY key;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Keyboard", 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
  DWORD v = enable ? 1 : 0;
  RegSetValueExW(key, L"PrintScreenKeyForSnippingEnabled", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
  RegCloseKey(key);
}

bool EnsureDirectory(const std::wstring& path) {
  const int r = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
  return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS;
}

void OpenFolder(const std::wstring& path) {
  EnsureDirectory(path);
  ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void RevealFile(const std::wstring& path) {
  PIDLIST_ABSOLUTE pidl = nullptr;
  if (SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr))) {
    SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
    CoTaskMemFree(pidl);
  }
}

void OpenFile(const std::wstring& path) {
  ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void SetDarkMenus(bool dark) {
  using SetPreferredAppModeFn = int(WINAPI*)(int);
  using FlushMenuThemesFn = void(WINAPI*)();
  HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
  if (!ux) ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!ux) return;
  auto setMode = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(ux, MAKEINTRESOURCEA(135)));
  auto flush = reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(ux, MAKEINTRESOURCEA(136)));
  if (setMode) setMode(dark ? kAppModeForceDark : kAppModeForceLight);
  if (flush) flush();
}

}
