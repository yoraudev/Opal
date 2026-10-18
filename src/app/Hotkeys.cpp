#include "app/Hotkeys.h"

#include <algorithm>

#include "core/Log.h"

namespace opal {

constexpr WORD kUnassignedVirtualKey = 0xE8;
constexpr ULONG_PTR kInjectedTag = 0x4F50414C;
constexpr UINT kMsgRebind = WM_APP + 1;
constexpr UINT kMsgRefreshHook = WM_APP + 2;
constexpr UINT kHookRefreshMs = 60000;
constexpr ULONGLONG kDuplicateMs = 500;

uint32_t CurrentModifiers() {
  uint32_t m = 0;
  if (GetAsyncKeyState(VK_CONTROL) < 0) m |= kModCtrl;
  if (GetAsyncKeyState(VK_MENU) < 0) m |= kModAlt;
  if (GetAsyncKeyState(VK_SHIFT) < 0) m |= kModShift;
  if (GetAsyncKeyState(VK_LWIN) < 0 || GetAsyncKeyState(VK_RWIN) < 0) m |= kModWin;
  return m;
}

HotkeyManager& HotkeyManager::Get() {
  static HotkeyManager m;
  return m;
}

bool HotkeyManager::Start(HWND target, UINT notifyMsg) {
  target_ = target;
  msg_ = notifyMsg;
  HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  thread_ = std::thread([this, ready] {
    threadId_ = GetCurrentThreadId();
    MSG m;
    PeekMessageW(&m, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    THREAD_POWER_THROTTLING_STATE throttling{THREAD_POWER_THROTTLING_CURRENT_VERSION,
                                             THREAD_POWER_THROTTLING_EXECUTION_SPEED, 0};
    SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &throttling, sizeof(throttling));
    InstallHook();
    SetTimer(nullptr, 0, kHookRefreshMs, nullptr);
    SetEvent(ready);
    ThreadMain();
  });
  WaitForSingleObject(ready, 3000);
  CloseHandle(ready);
  return hook_ != nullptr;
}

void HotkeyManager::Stop() {
  if (!thread_.joinable()) return;
  PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
  thread_.join();
}

void HotkeyManager::Refresh() {
  if (const DWORD id = threadId_) PostThreadMessageW(id, kMsgRefreshHook, 0, 0);
}

void HotkeyManager::ThreadMain() {
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
  MSG m;
  while (GetMessageW(&m, nullptr, 0, 0) > 0) {
    if (!m.hwnd) {
      if (m.message == WM_HOTKEY) {
        OnFallback(int(m.wParam));
        continue;
      }
      if (m.message == WM_TIMER || m.message == kMsgRefreshHook) {
        InstallHook();
        continue;
      }
      if (m.message == kMsgRebind) {
        RegisterFallbacks();
        continue;
      }
    }
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
  UnregisterFallbacks();
  if (hook_) UnhookWindowsHookEx(hook_);
  hook_ = nullptr;
}

bool HotkeyManager::InstallHook() {
  HHOOK fresh = SetWindowsHookExW(WH_KEYBOARD_LL, &HookProc, GetModuleHandleW(nullptr), 0);
  if (!fresh) {
    Log(L"SetWindowsHookEx failed: %lu", GetLastError());
    return hook_ != nullptr;
  }
  if (hook_) UnhookWindowsHookEx(hook_);
  hook_ = fresh;
  return true;
}

void HotkeyManager::RegisterFallbacks() {
  UnregisterFallbacks();
  std::array<Hotkey, kActionCount> keys;
  bool usePrintScreen = true, useWinShiftS = true;
  {
    std::lock_guard lock(mutex_);
    keys = keys_;
    usePrintScreen = usePrintScreen_;
    useWinShiftS = useWinShiftS_;
  }
  auto add = [this](int id, const Hotkey& hk) {
    UINT mods = MOD_NOREPEAT;
    if (hk.mods & kModCtrl) mods |= MOD_CONTROL;
    if (hk.mods & kModAlt) mods |= MOD_ALT;
    if (hk.mods & kModShift) mods |= MOD_SHIFT;
    if (hk.mods & kModWin) mods |= MOD_WIN;
    if (RegisterHotKey(nullptr, id, mods, hk.vk)) {
      registered_[size_t(id)] = true;
    } else {
      Log(L"No fallback for %s: %lu", FormatHotkey(hk).c_str(), GetLastError());
    }
  };
  for (int i = 0; i < kActionCount; ++i) {
    const Hotkey& hk = keys[size_t(i)];
    if (!hk.Valid() || (hk.vk == VK_SNAPSHOT && hk.mods == 0 && !usePrintScreen)) continue;
    add(i, hk);
  }
  const Hotkey winShiftS{kModWin | kModShift, 'S'};
  if (useWinShiftS && std::find(keys.begin(), keys.end(), winShiftS) == keys.end()) add(kActionCount, winShiftS);
}

void HotkeyManager::UnregisterFallbacks() {
  for (size_t id = 0; id < registered_.size(); ++id) {
    if (registered_[id]) UnregisterHotKey(nullptr, int(id));
    registered_[id] = false;
  }
}

void HotkeyManager::OnFallback(int id) {
  if (id < 0 || id > kActionCount) return;
  const int action = id == kActionCount ? int(Action::Bar) : id;
  InstallHook();
  {
    std::lock_guard lock(mutex_);
    if (recordTarget_) return;
    const ULONGLONG now = GetTickCount64();
    if (now - lastTrigger_[size_t(action)] < kDuplicateMs) return;
    lastTrigger_[size_t(action)] = now;
  }
  Log(L"Shortcut %d reached Opal without the keyboard hook", id);
  PostMessageW(target_, msg_, WPARAM(action), 0);
}

void HotkeyManager::SetBindings(const std::array<Hotkey, kActionCount>& keys, bool usePrintScreen, bool useWinShiftS) {
  {
    std::lock_guard lock(mutex_);
    keys_ = keys;
    usePrintScreen_ = usePrintScreen;
    useWinShiftS_ = useWinShiftS;
  }
  if (const DWORD id = threadId_) PostThreadMessageW(id, kMsgRebind, 0, 0);
}

void HotkeyManager::BeginRecording(HWND target, UINT msg) {
  std::lock_guard lock(mutex_);
  recordTarget_ = target;
  recordMsg_ = msg;
}

void HotkeyManager::SetCaptureTarget(HWND target, UINT msg) {
  std::lock_guard lock(mutex_);
  captureTarget_ = target;
  captureMsg_ = msg;
}

static bool IsCaptureKey(uint32_t vk) {
  switch (vk) {
    case VK_ESCAPE: case VK_RETURN: case VK_SPACE: case VK_TAB:
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case '1': case '2': case '3': case '4':
      return true;
    default:
      return false;
  }
}

void HotkeyManager::EndRecording() {
  std::lock_guard lock(mutex_);
  recordTarget_ = nullptr;
}

LRESULT CALLBACK HotkeyManager::HookProc(int code, WPARAM wp, LPARAM lp) {
  if (code == HC_ACTION) {
    const auto& k = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
    if (Get().OnKey(wp, k)) return 1;
  }
  return CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT HotkeyManager::OnKey(WPARAM wp, const KBDLLHOOKSTRUCT& k) {
  const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
  const bool up = wp == WM_KEYUP || wp == WM_SYSKEYUP;
  const uint32_t vk = k.vkCode & 0xFF;
  if (IsModifierKey(vk) || k.dwExtraInfo == kInjectedTag) return 0;

  std::unique_lock lock(mutex_);
  if (captureTarget_ && IsCaptureKey(vk) && !recordTarget_) {
    if (up) swallowedDown_[vk] = false;
    PostMessageW(captureTarget_, captureMsg_, vk, LPARAM(CurrentModifiers()) | (up ? kKeyUpFlag : 0));
    return 1;
  }
  if (up) {
    if (swallowedDown_[vk]) {
      swallowedDown_[vk] = false;
      return 1;
    }
    return 0;
  }
  if (!down) return 0;
  const uint32_t mods = CurrentModifiers();

  if (recordTarget_) {
    PostMessageW(recordTarget_, recordMsg_, vk, mods);
    swallowedDown_[vk] = true;
    return 1;
  }

  for (int i = 0; i < kActionCount; ++i) {
    const Hotkey& hk = keys_[size_t(i)];
    if (!hk.Valid() || hk.vk != vk || hk.mods != mods) continue;
    if (hk.vk == VK_SNAPSHOT && hk.mods == 0 && !usePrintScreen_) continue;
    return Trigger(lock, i, vk, mods);
  }
  if (useWinShiftS_ && vk == 'S' && mods == (kModWin | kModShift)) return Trigger(lock, int(Action::Bar), vk, mods);
  return 0;
}

bool HotkeyManager::Trigger(std::unique_lock<std::mutex>& lock, int action, uint32_t vk, uint32_t mods) {
  const bool repeat = swallowedDown_[vk];
  swallowedDown_[vk] = true;
  if (repeat) return true;
  lastTrigger_[size_t(action)] = GetTickCount64();
  PostMessageW(target_, msg_, WPARAM(action), 0);
  if (mods & kModWin) {
    lock.unlock();
    INPUT in[2]{};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = kUnassignedVirtualKey;
    in[0].ki.dwExtraInfo = in[1].ki.dwExtraInfo = kInjectedTag;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
  }
  return true;
}

}
