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
  void SetBindings(const std::array<Hotkey, kActionCount>& keys, bool usePrintScreen, bool useWinShiftS);

  void BeginRecording(HWND target, UINT msg);
  void EndRecording();

  static constexpr LPARAM kKeyUpFlag = 0x10000;
  void SetCaptureTarget(HWND target, UINT msg);

 private:
  HotkeyManager() = default;
  static LRESULT CALLBACK HookProc(int code, WPARAM wp, LPARAM lp);
  LRESULT OnKey(WPARAM wp, const KBDLLHOOKSTRUCT& k);
  void ThreadMain();
  bool Trigger(std::unique_lock<std::mutex>& lock, int action, uint32_t vk, uint32_t mods);

  std::thread thread_;
  DWORD threadId_ = 0;
  HHOOK hook_ = nullptr;
  HWND target_ = nullptr;
  UINT msg_ = 0;
  std::mutex mutex_;
  std::array<Hotkey, kActionCount> keys_{};
  bool usePrintScreen_ = true;
  bool useWinShiftS_ = true;
  HWND recordTarget_ = nullptr;
  UINT recordMsg_ = 0;
  HWND captureTarget_ = nullptr;
  UINT captureMsg_ = 0;
  std::array<bool, 256> swallowedDown_{};
};

uint32_t CurrentModifiers();

}
