#pragma once
#include <windows.h>

#include <string>

namespace opal {

enum TrayCommand : UINT {
  kCmdBar = 100,
  kCmdScreen,
  kCmdRegion,
  kCmdWindow,
  kCmdElement,
  kCmdOpenFolder,
  kCmdSettings,
  kCmdQuit,
  kCmdUpdate,
};

class Tray {
 public:
  bool Create(HWND owner, UINT callbackMsg);
  void Destroy();
  void Recreate();
  UINT ShowMenu(const std::wstring& updateLabel, bool updateEnabled);
  void Notify(const std::wstring& title, const std::wstring& text);

 private:
  HWND owner_ = nullptr;
  UINT msg_ = 0;
  bool added_ = false;
};

}
