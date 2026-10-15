#pragma once
#include <windows.h>

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
};

class Tray {
 public:
  bool Create(HWND owner, UINT callbackMsg);
  void Destroy();
  void Recreate();
  UINT ShowMenu();

 private:
  HWND owner_ = nullptr;
  UINT msg_ = 0;
  bool added_ = false;
};

}
