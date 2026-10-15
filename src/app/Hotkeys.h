#pragma once
#include <windows.h>

#include <array>
#include <atomic>
#include <mutex>
#include <thread>

#include "app/Settings.h"

namespace opal {

class HotkeyManager {
 public:
  static HotkeyManager& Get();

  bool Start(HWND target, UINT notifyMsg);
  void Stop();
  void SetBindings(const std::array<Hotkey, kActionCount>& keys, bool usePrintScreen);

  void BeginRecording(HWND target, UINT msg);
  void EndRecording();

  static constexpr LPARAM kKeyUpFlag = 0x10000;
  void SetCaptureTarget(HWND target, UINT msg);

 private:
  HotkeyManager() = default;
  static LRESULT CALLBACK HookProc(int code, WPARAM wp, LPARAM lp);
  LRESULT OnKey(WPARAM wp, const KBDLLHOOKSTRUCT& k);
  void ThreadMain();

  std::thread thread_;
  DWORD threadId_ = 0;
  HHOOK hook_ = nullptr;
  HWND target_ = nullptr;
  UINT msg_ = 0;
  std::mutex mutex_;
  std::array<Hotkey, kActionCount> keys_{};
  bool usePrintScreen_ = true;
  HWND recordTarget_ = nullptr;
  UINT recordMsg_ = 0;
  HWND captureTarget_ = nullptr;
  UINT captureMsg_ = 0;
  std::array<bool, 256> swallowedDown_{};
};

uint32_t CurrentModifiers();

}
