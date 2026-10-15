#pragma once
#include <array>
#include <string>

#include "app/Hotkey.h"
#include "app/Sound.h"
#include "core/Geometry.h"
#include "ui/Theme.h"

namespace opal {

enum class Action : int { Bar, Screen, Region, Window, Element, Count };
constexpr int kActionCount = int(Action::Count);

enum class CaptureMode : int { Screen = 0, Window = 1, Region = 2, Element = 3 };

struct Settings {
  bool launchAtStartup = true;
  bool usePrintScreen = true;
  bool useWinShiftS = true;
  bool playSound = true;
  ShutterSound shutterSound = ShutterSound::Modern;
  std::wstring language = L"auto";
  ThemePref theme = ThemePref::System;

  std::array<Hotkey, kActionCount> hotkeys{};

  bool windowShadow = true;
  bool showPointer = false;
  bool showLoupe = true;
  int timerSeconds = 0;
  bool snapEdges = true;
  bool autoCorners = true;
  bool elementShadow = false;
  CaptureMode lastBarMode = CaptureMode::Region;
  RectI lastRegion{};

  bool showThumbnail = true;
  int thumbnailSeconds = 5;

  bool copyToClipboard = true;
  bool saveToFile = true;
  std::wstring folder;
  std::wstring filePrefix = L"Opal";

  bool firstRun = true;

  static Settings Defaults();
  static std::wstring FilePath();
  void Load();
  void Save() const;
  std::wstring ResolvedFolder() const;
};

Settings& AppSettings();
const wchar_t* ActionKey(Action a);

}
