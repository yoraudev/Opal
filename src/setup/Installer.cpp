#include "setup/Installer.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <optional>
#include <vector>

#include "Version.h"
#include "app/App.h"
#include "app/SystemIntegration.h"
#include "core/Log.h"
#include "core/Strings.h"
#include "setup/InstallRules.h"

namespace opal::setup {
namespace {

using Microsoft::WRL::ComPtr;

constexpr wchar_t kUninstallKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Opal";
constexpr wchar_t kExeName[] = L"Opal.exe";

struct Cleanup {
  std::vector<std::wstring> files;
  std::vector<std::wstring> dirs;
};
Cleanup g_cleanup;

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
  PWSTR p = nullptr;
  std::wstring out;
  if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p))) out = p;
  CoTaskMemFree(p);
  return out;
}

std::wstring DirOf(const std::wstring& path) {
  const size_t slash = path.find_last_of(L'\\');
  return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

bool Exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::wstring ReadString(HKEY root, const wchar_t* key, const wchar_t* name) {
  wchar_t buf[MAX_PATH * 2];
  DWORD size = sizeof(buf);
  if (RegGetValueW(root, key, name, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return {};
  return buf;
}

void SetString(HKEY key, const wchar_t* name, const std::wstring& value) {
  RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                 DWORD((value.size() + 1) * sizeof(wchar_t)));
}

void SetDword(HKEY key, const wchar_t* name, DWORD value) {
  RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

std::wstring StartMenuShortcut() { return KnownFolder(FOLDERID_Programs) + L"\\Opal.lnk"; }
std::wstring DesktopShortcut() { return KnownFolder(FOLDERID_Desktop) + L"\\Opal.lnk"; }

std::wstring ShortcutTarget(const std::wstring& lnk) {
  if (!Exists(lnk)) return {};
  ComPtr<IShellLinkW> link;
  ComPtr<IPersistFile> file;
  wchar_t target[MAX_PATH] = {};
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) ||
      FAILED(link.As(&file)) || FAILED(file->Load(lnk.c_str(), STGM_READ)) ||
      FAILED(link->GetPath(target, MAX_PATH, nullptr, SLGP_RAWPATH))) {
    return {};
  }
  return target;
}

bool CreateShortcut(const std::wstring& lnk, const std::wstring& exe) {
  ComPtr<IShellLinkW> link;
  ComPtr<IPersistFile> file;
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return false;
  link->SetPath(exe.c_str());
  link->SetWorkingDirectory(DirOf(exe).c_str());
  link->SetIconLocation(exe.c_str(), 0);
  link->SetDescription(T(S::Tagline));
  return SUCCEEDED(link.As(&file)) && SUCCEEDED(file->Save(lnk.c_str(), TRUE));
}

void ApplyShortcut(const std::wstring& lnk, const std::wstring& exe, bool want) {
  if (want) {
    if (!CreateShortcut(lnk, exe)) Log(L"Creating %s failed", lnk.c_str());
  } else if (SamePath(ShortcutTarget(lnk), exe)) {
    DeleteFileW(lnk.c_str());
  }
}

void WriteUninstallKey(const std::wstring& dir, const std::wstring& exe) {
  HKEY key;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kUninstallKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
      ERROR_SUCCESS) {
    Log(L"Creating the uninstall entry failed");
    return;
  }
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t date[16];
  swprintf_s(date, L"%04d%02d%02d", t.wYear, t.wMonth, t.wDay);
  WIN32_FILE_ATTRIBUTE_DATA fa{};
  GetFileAttributesExW(exe.c_str(), GetFileExInfoStandard, &fa);
  const std::wstring quoted = L"\"" + exe + L"\"";
  SetString(key, L"DisplayName", L"Opal");
  SetString(key, L"DisplayVersion", OPAL_VERSION_WTEXT);
  SetString(key, L"Publisher", L"yoraudev");
  SetString(key, L"DisplayIcon", exe + L",0");
  SetString(key, L"InstallLocation", dir);
  SetString(key, L"InstallDate", date);
  SetString(key, L"UninstallString", quoted + L" --uninstall");
  SetString(key, L"QuietUninstallString", quoted + L" --uninstall --silent");
  SetString(key, L"URLInfoAbout", L"https://github.com/yoraudev/Opal");
  SetDword(key, L"EstimatedSize", (fa.nFileSizeLow + 1023) / 1024);
  SetDword(key, L"NoModify", 1);
  SetDword(key, L"NoRepair", 1);
  RegCloseKey(key);
}

std::wstring Quoted(const std::wstring& s) { return L"\"" + s + L"\""; }

std::wstring NearestExisting(std::wstring dir) {
  while (!dir.empty() && !Exists(dir)) {
    const std::wstring parent = DirOf(dir);
    if (parent == dir) break;
    dir = parent;
  }
  return dir;
}

DWORD CopySelfTo(const std::wstring& dir) {
  const int created = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
  if (created != ERROR_SUCCESS && created != ERROR_ALREADY_EXISTS && created != ERROR_FILE_EXISTS) return DWORD(created);
  const std::wstring self = sys::ExePath();
  const std::wstring target = dir + L"\\" + kExeName;
  const std::wstring old = target + L".old";
  if (SamePath(self, target)) return ERROR_SUCCESS;
  DeleteFileW(old.c_str());
  const bool moved = Exists(target) && MoveFileExW(target.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
  if (!CopyFileW(self.c_str(), target.c_str(), FALSE)) {
    const DWORD err = GetLastError();
    if (moved) MoveFileExW(old.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING);
    return err;
  }
  DeleteFileW(old.c_str());
  return ERROR_SUCCESS;
}

void RemoveUserData() {
  const std::wstring roaming = KnownFolder(FOLDERID_RoamingAppData) + L"\\Opal";
  const std::wstring local = KnownFolder(FOLDERID_LocalAppData) + L"\\Opal";
  for (const wchar_t* f : {L"settings.ini", L"settings.ini.tmp"}) DeleteFileW((roaming + L"\\" + f).c_str());
  for (const wchar_t* f : {L"opal.log", L"opal.prev.log", L"setup.log", L"setup.prev.log", L"crash.dmp"}) {
    DeleteFileW((local + L"\\" + f).c_str());
  }
  for (const auto& d : {roaming, local}) {
    if (!RemoveDirectoryW(d.c_str()) && Exists(d)) g_cleanup.dirs.push_back(d);
  }
}

}

Installed FindInstalled() {
  Installed out;
  out.dir = ReadString(HKEY_CURRENT_USER, kUninstallKey, L"InstallLocation");
  out.version = ReadString(HKEY_CURRENT_USER, kUninstallKey, L"DisplayVersion");
  out.present = !out.dir.empty() && Exists(out.dir + L"\\" + kExeName);
  return out;
}

std::wstring DefaultInstallDir() { return KnownFolder(FOLDERID_UserProgramFiles) + L"\\Opal"; }

bool IsInstalledCopy() {
  const Installed inst = FindInstalled();
  return inst.present && SamePath(DirOf(sys::ExePath()), inst.dir);
}

InstallOptions CurrentOptions(const Installed& installed) {
  InstallOptions o;
  if (!installed.present) {
    o.dir = DefaultInstallDir();
    return o;
  }
  const std::wstring exe = installed.dir + L"\\" + kExeName;
  o.dir = installed.dir;
  o.autostart = sys::IsLaunchAtStartup();
  o.startMenu = SamePath(ShortcutTarget(StartMenuShortcut()), exe);
  o.desktop = SamePath(ShortcutTarget(DesktopShortcut()), exe);
  return o;
}

bool IsElevated() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  TOKEN_ELEVATION elevation{};
  DWORD size = 0;
  const bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) != FALSE;
  CloseHandle(token);
  return ok && elevation.TokenIsElevated;
}

bool CanWriteTo(const std::wstring& dir) {
  const std::wstring probe = NearestExisting(dir) + L"\\.opal-" + std::to_wstring(GetCurrentProcessId()) + L".tmp";
  HANDLE f = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                         FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
  if (f != INVALID_HANDLE_VALUE) {
    CloseHandle(f);
    return true;
  }
  return GetLastError() != ERROR_ACCESS_DENIED;
}

uint64_t NeededBytes() {
  WIN32_FILE_ATTRIBUTE_DATA fa{};
  if (!GetFileAttributesExW(sys::ExePath().c_str(), GetFileExInfoStandard, &fa)) return 0;
  return (uint64_t(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
}

bool FreeBytes(const std::wstring& dir, uint64_t* bytes) {
  ULARGE_INTEGER available{};
  if (!GetDiskFreeSpaceExW(NearestExisting(dir).c_str(), &available, nullptr, nullptr)) return false;
  *bytes = available.QuadPart;
  return true;
}

DWORD RunElevated(const std::wstring& args, HWND owner) {
  const std::wstring self = sys::ExePath();
  SHELLEXECUTEINFOW sei{sizeof(sei)};
  sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
  sei.hwnd = owner;
  sei.lpVerb = L"runas";
  sei.lpFile = self.c_str();
  sei.lpParameters = args.c_str();
  sei.nShow = SW_SHOWNORMAL;
  if (!ShellExecuteExW(&sei)) return GetLastError();
  DWORD code = ERROR_GEN_FAILURE;
  if (sei.hProcess) {
    WaitForSingleObject(sei.hProcess, INFINITE);
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
  }
  return code;
}

int RunCopyHelper(const std::wstring& args) {
  const std::optional<std::wstring> dir = ArgValue(args, L"--copy-to");
  if (!dir || DriveOf(*dir).empty()) return int(ERROR_INVALID_PARAMETER);
  return int(CopySelfTo(*dir));
}

void CleanUpAfterUpdate() { DeleteFileW((sys::ExePath() + L".old").c_str()); }

bool StopRunningOpal(bool* wasRunning) {
  *wasRunning = false;
  for (int attempt = 0; attempt < 3; ++attempt) {
    HWND w = FindWindowW(kAppWindowClass, nullptr);
    if (!w) return true;
    *wasRunning = true;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    HANDLE proc = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
    const std::wstring args = L"--quit";
    COPYDATASTRUCT cds{1, DWORD((args.size() + 1) * sizeof(wchar_t)), const_cast<wchar_t*>(args.c_str())};
    DWORD_PTR result = 0;
    SendMessageTimeoutW(w, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds), SMTO_ABORTIFHUNG, 3000, &result);
    if (!proc) {
      Sleep(500);
      continue;
    }
    if (WaitForSingleObject(proc, 5000) == WAIT_TIMEOUT) {
      Log(L"Opal (pid %lu) did not quit, terminating it", pid);
      TerminateProcess(proc, 1);
      WaitForSingleObject(proc, 3000);
    }
    CloseHandle(proc);
  }
  return FindWindowW(kAppWindowClass, nullptr) == nullptr;
}

bool Install(const InstallOptions& o, const std::function<void(Step)>& progress, std::wstring* error,
             bool* wasRunning, HWND owner) {
  wchar_t msg[1024];
  progress(Step::Stopping);
  if (!StopRunningOpal(wasRunning)) {
    *error = T(S::SetupErrorRunning);
    return false;
  }
  progress(Step::Copying);
  DWORD err = CanWriteTo(o.dir) ? CopySelfTo(o.dir) : ERROR_ACCESS_DENIED;
  if (err == ERROR_ACCESS_DENIED && !IsElevated()) {
    Log(L"%s needs administrator rights, asking Windows", o.dir.c_str());
    err = RunElevated(L"--copy-to " + Quoted(o.dir), owner);
  }
  if (err == ERROR_CANCELLED) {
    swprintf_s(msg, T(S::SetupErrorDeclined), o.dir.c_str());
    *error = msg;
    return false;
  }
  if (err != ERROR_SUCCESS) {
    swprintf_s(msg, T(S::SetupErrorCopy), o.dir.c_str(), err);
    *error = msg;
    return false;
  }
  const std::wstring target = o.dir + L"\\" + kExeName;
  progress(Step::Registering);
  WriteUninstallKey(o.dir, target);
  ApplyShortcut(StartMenuShortcut(), target, o.startMenu);
  ApplyShortcut(DesktopShortcut(), target, o.desktop);
  sys::SetLaunchAtStartup(o.autostart, target);
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  Log(L"Installed %s to %s (autostart %d, start menu %d, desktop %d)", OPAL_VERSION_WTEXT, o.dir.c_str(),
      int(o.autostart), int(o.startMenu), int(o.desktop));
  progress(Step::Finished);
  return true;
}

bool Uninstall(const Installed& installed, bool removeUserData, const std::function<void(Step)>& progress) {
  progress(Step::Stopping);
  bool wasRunning = false;
  if (!StopRunningOpal(&wasRunning)) Log(L"Opal is still running during uninstall");
  progress(Step::Removing);
  const std::wstring exe = installed.dir + L"\\" + kExeName;
  if (SamePath(sys::LaunchAtStartupTarget(), exe)) sys::SetLaunchAtStartup(false);
  ApplyShortcut(StartMenuShortcut(), exe, false);
  ApplyShortcut(DesktopShortcut(), exe, false);
  RegDeleteTreeW(HKEY_CURRENT_USER, kUninstallKey);
  if (removeUserData) RemoveUserData();
  for (const auto& f : {exe, exe + L".old"}) {
    if (!DeleteFileW(f.c_str()) && Exists(f)) g_cleanup.files.push_back(f);
  }
  if (!RemoveDirectoryW(installed.dir.c_str()) && Exists(installed.dir)) {
    g_cleanup.dirs.insert(g_cleanup.dirs.begin(), installed.dir);
  }
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  Log(L"Uninstalled from %s (user data %d)", installed.dir.c_str(), int(removeUserData));
  progress(Step::Finished);
  return true;
}

void ScheduleCleanup() {
  if (g_cleanup.files.empty() && g_cleanup.dirs.empty()) return;
  std::wstring script = L"ping -n 3 127.0.0.1 >nul";
  for (const auto& f : g_cleanup.files) script += L" & del /f /q \"" + f + L"\" 2>nul";
  for (const auto& d : g_cleanup.dirs) script += L" & rmdir \"" + d + L"\" 2>nul";
  wchar_t sys32[MAX_PATH];
  GetSystemDirectoryW(sys32, MAX_PATH);
  const std::wstring cmdExe = std::wstring(sys32) + L"\\cmd.exe";
  std::wstring cmd = L"\"" + cmdExe + L"\" /d /s /c \"" + script + L"\"";
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION pi{};
  if (CreateProcessW(cmdExe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, sys32, &si, &pi)) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
}

bool Launch(const std::wstring& dir, bool background) {
  const std::wstring exe = dir + L"\\" + kExeName;
  std::wstring cmd = L"\"" + exe + L"\"" + (background ? L" --background" : L"");
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
    Log(L"Starting %s failed: %lu", exe.c_str(), GetLastError());
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

}
