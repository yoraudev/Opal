#include "app/Hotkeys.h"

#include "core/Log.h"

namespace opal {

constexpr WORD kUnassignedVirtualKey = 0xE8;

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
    hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, &HookProc, GetModuleHandleW(nullptr), 0);
    if (!hook_) Log(L"SetWindowsHookEx failed: %lu", GetLastError());
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

void HotkeyManager::ThreadMain() {
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
  MSG m;
  while (GetMessageW(&m, nullptr, 0, 0) > 0) {
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
  if (hook_) UnhookWindowsHookEx(hook_);
  hook_ = nullptr;
}

void HotkeyManager::SetBindings(const std::array<Hotkey, kActionCount>& keys, bool usePrintScreen) {
  std::lock_guard lock(mutex_);
  keys_ = keys;
  usePrintScreen_ = usePrintScreen;
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
  if (IsModifierKey(vk)) return 0;

  std::lock_guard lock(mutex_);
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
    if (!swallowedDown_[vk]) {
      PostMessageW(target_, msg_, WPARAM(i), 0);
      if (mods & kModWin) {
        INPUT in[2]{};
        in[0].type = in[1].type = INPUT_KEYBOARD;
        in[0].ki.wVk = in[1].ki.wVk = kUnassignedVirtualKey;
        in[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, in, sizeof(INPUT));
      }
    }
    swallowedDown_[vk] = true;
    return 1;
  }
  return 0;
}

}
