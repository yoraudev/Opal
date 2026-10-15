#include "app/Settings.h"

#include <windows.h>
#include <shlobj.h>

#include <vector>

#include "app/Ini.h"
#include "core/Log.h"

namespace opal {
namespace {

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
  PWSTR p = nullptr;
  std::wstring out;
  if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p))) out = p;
  CoTaskMemFree(p);
  return out;
}

std::wstring ReadUtf8File(const std::wstring& path, bool* ok) {
  *ok = false;
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return {};
  LARGE_INTEGER size{};
  GetFileSizeEx(f, &size);
  std::vector<char> buf(size_t(size.QuadPart));
  DWORD read = 0;
  if (!buf.empty()) ReadFile(f, buf.data(), DWORD(buf.size()), &read, nullptr);
  CloseHandle(f);
  *ok = true;
  if (read == 0) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, buf.data(), int(read), nullptr, 0);
  std::wstring w(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, buf.data(), int(read), w.data(), n);
  return w;
}

bool WriteUtf8File(const std::wstring& path, const std::wstring& text) {
  int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), s.data(), n, nullptr, nullptr);
  const std::wstring tmp = path + L".tmp";
  HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const BOOL ok = WriteFile(f, s.data(), DWORD(s.size()), &written, nullptr);
  CloseHandle(f);
  if (!ok) return false;
  return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
}

constexpr const wchar_t* kModeNames[] = {L"screen", L"window", L"region", L"element"};
constexpr const wchar_t* kThemeNames[] = {L"system", L"light", L"dark"};

}

const wchar_t* ActionKey(Action a) {
  switch (a) {
    case Action::Bar: return L"bar";
    case Action::Screen: return L"screen";
    case Action::Region: return L"region";
    case Action::Window: return L"window";
    case Action::Element: return L"element";
    default: return L"";
  }
}

Settings& AppSettings() {
  static Settings s = Settings::Defaults();
  return s;
}

Settings Settings::Defaults() {
  Settings s;
  s.hotkeys[int(Action::Bar)] = {0, VK_SNAPSHOT};
  s.hotkeys[int(Action::Screen)] = {kModCtrl | kModShift, '3'};
  s.hotkeys[int(Action::Region)] = {kModCtrl | kModShift, '4'};
  s.hotkeys[int(Action::Window)] = {kModCtrl | kModShift, '5'};
  s.hotkeys[int(Action::Element)] = {kModCtrl | kModShift, '6'};
  return s;
}

std::wstring Settings::FilePath() {
  std::wstring dir = KnownFolder(FOLDERID_RoamingAppData) + L"\\Opal";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir + L"\\settings.ini";
}

std::wstring Settings::ResolvedFolder() const {
  if (!folder.empty()) return folder;
  return KnownFolder(FOLDERID_Pictures) + L"\\Opal";
}

void Settings::Load() {
  *this = Defaults();
  bool exists = false;
  std::wstring text = ReadUtf8File(FilePath(), &exists);
  firstRun = !exists;
  if (!exists) return;
  Ini ini;
  ini.Parse(text);

  launchAtStartup = ini.GetBool(L"General", L"launchAtStartup", launchAtStartup);
  usePrintScreen = ini.GetBool(L"General", L"usePrintScreen", usePrintScreen);
  useWinShiftS = ini.GetBool(L"General", L"useWinShiftS", useWinShiftS);
  playSound = ini.GetBool(L"General", L"playSound", playSound);
  shutterSound = ini.Get(L"General", L"shutterSound") == L"vintage" ? ShutterSound::Vintage : ShutterSound::Modern;
  language = ini.Get(L"General", L"language", language);
  const std::wstring th = ini.Get(L"General", L"theme", L"system");
  for (int i = 0; i < 3; ++i) {
    if (th == kThemeNames[i]) theme = ThemePref(i);
  }

  for (int i = 0; i < kActionCount; ++i) {
    const wchar_t* key = ActionKey(Action(i));
    if (!ini.Has(L"Hotkeys", key)) continue;
    if (auto hk = ParseHotkey(ini.Get(L"Hotkeys", key))) hotkeys[size_t(i)] = *hk;
  }

  windowShadow = ini.GetBool(L"Capture", L"windowShadow", windowShadow);
  showPointer = ini.GetBool(L"Capture", L"showPointer", showPointer);
  showLoupe = ini.GetBool(L"Capture", L"showLoupe", showLoupe);
  timerSeconds = ini.GetInt(L"Capture", L"timer", timerSeconds);
  if (timerSeconds != 0 && timerSeconds != 5 && timerSeconds != 10) timerSeconds = 0;
  snapEdges = ini.GetBool(L"Capture", L"snapEdges", snapEdges);
  autoCorners = ini.GetBool(L"Capture", L"autoCorners", autoCorners);
  elementShadow = ini.GetBool(L"Capture", L"elementShadow", elementShadow);
  const std::wstring mode = ini.Get(L"Capture", L"lastBarMode", L"region");
  for (int i = 0; i < 4; ++i) {
    if (mode == kModeNames[i]) lastBarMode = CaptureMode(i);
  }
  lastRegion = {ini.GetInt(L"Capture", L"lastRegionLeft", 0), ini.GetInt(L"Capture", L"lastRegionTop", 0),
                ini.GetInt(L"Capture", L"lastRegionRight", 0), ini.GetInt(L"Capture", L"lastRegionBottom", 0)};

  showThumbnail = ini.GetBool(L"Thumbnail", L"show", showThumbnail);
  thumbnailSeconds = std::clamp(ini.GetInt(L"Thumbnail", L"seconds", thumbnailSeconds), 2, 30);

  copyToClipboard = ini.GetBool(L"Output", L"clipboard", copyToClipboard);
  saveToFile = ini.GetBool(L"Output", L"file", saveToFile);
  folder = ini.Get(L"Output", L"folder", folder);
  filePrefix = ini.Get(L"Output", L"prefix", filePrefix);
  if (filePrefix.empty()) filePrefix = L"Opal";
}

void Settings::Save() const {
  Ini ini;
  ini.SetBool(L"General", L"launchAtStartup", launchAtStartup);
  ini.SetBool(L"General", L"usePrintScreen", usePrintScreen);
  ini.SetBool(L"General", L"useWinShiftS", useWinShiftS);
  ini.SetBool(L"General", L"playSound", playSound);
  ini.Set(L"General", L"shutterSound", shutterSound == ShutterSound::Vintage ? L"vintage" : L"modern");
  ini.Set(L"General", L"language", language);
  ini.Set(L"General", L"theme", kThemeNames[int(theme)]);
  for (int i = 0; i < kActionCount; ++i) {
    ini.Set(L"Hotkeys", ActionKey(Action(i)), FormatHotkey(hotkeys[size_t(i)]));
  }
  ini.SetBool(L"Capture", L"windowShadow", windowShadow);
  ini.SetBool(L"Capture", L"showPointer", showPointer);
  ini.SetBool(L"Capture", L"showLoupe", showLoupe);
  ini.SetInt(L"Capture", L"timer", timerSeconds);
  ini.SetBool(L"Capture", L"snapEdges", snapEdges);
  ini.SetBool(L"Capture", L"autoCorners", autoCorners);
  ini.SetBool(L"Capture", L"elementShadow", elementShadow);
  ini.Set(L"Capture", L"lastBarMode", kModeNames[int(lastBarMode)]);
  ini.SetInt(L"Capture", L"lastRegionLeft", lastRegion.left);
  ini.SetInt(L"Capture", L"lastRegionTop", lastRegion.top);
  ini.SetInt(L"Capture", L"lastRegionRight", lastRegion.right);
  ini.SetInt(L"Capture", L"lastRegionBottom", lastRegion.bottom);
  ini.SetBool(L"Thumbnail", L"show", showThumbnail);
  ini.SetInt(L"Thumbnail", L"seconds", thumbnailSeconds);
  ini.SetBool(L"Output", L"clipboard", copyToClipboard);
  ini.SetBool(L"Output", L"file", saveToFile);
  ini.Set(L"Output", L"folder", folder);
  ini.Set(L"Output", L"prefix", filePrefix);
  if (!WriteUtf8File(FilePath(), L"; Opal settings\r\n" + ini.Serialize())) {
    Log(L"Saving settings failed: %lu", GetLastError());
  }
}

}
