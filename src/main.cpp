#include <windows.h>
#include <dbghelp.h>
#include <objbase.h>
#include <ole2.h>
#include <shlobj.h>

#include <cstdlib>
#include <exception>
#include <string>

#include "app/App.h"
#include "core/Log.h"
#include "render/Gpu.h"
#include "setup/InstallRules.h"
#include "setup/Installer.h"
#include "setup/SetupWindow.h"

namespace {

constexpr DWORD kFatalErrorCode = 0xE04F5041;

LONG WINAPI OnCrash(EXCEPTION_POINTERS* ep) {
  HMODULE mod = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     static_cast<LPCWSTR>(ep->ExceptionRecord->ExceptionAddress), &mod);
  opal::Log(L"CRASH: code 0x%08X at %p (module base %p, offset 0x%llX, thread %lu)",
            ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress, mod,
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress) -
                                            reinterpret_cast<uintptr_t>(mod)),
            GetCurrentThreadId());
  PWSTR base = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) {
    const std::wstring path = std::wstring(base) + L"\\Opal\\crash.dmp";
    CoTaskMemFree(base);
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
      MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), ep, FALSE};
      MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f,
                        MINIDUMP_TYPE(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory),
                        &mei, nullptr, nullptr);
      CloseHandle(f);
    }
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

[[noreturn]] void FailFast(const wchar_t* reason) {
  opal::Log(L"FATAL: %s", reason);
  RaiseException(kFatalErrorCode, EXCEPTION_NONCONTINUABLE, 0, nullptr);
  std::abort();
}

void InstallCrashHandlers() {
  SetUnhandledExceptionFilter(&OnCrash);
  std::set_terminate([] { FailFast(L"std::terminate"); });
  _set_purecall_handler([] { FailFast(L"pure virtual call"); });
  _set_invalid_parameter_handler(
      [](const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) { FailFast(L"invalid CRT parameter"); });
  _set_abort_behavior(0, _CALL_REPORTFAULT);
}

int ForwardToRunningCopy(const std::wstring& args) {
  HWND other = FindWindowW(opal::kAppWindowClass, nullptr);
  if (!other) return 0;
  DWORD pid = 0;
  GetWindowThreadProcessId(other, &pid);
  HANDLE proc = opal::setup::HasArg(args, L"--quit") ? OpenProcess(SYNCHRONIZE, FALSE, pid) : nullptr;
  COPYDATASTRUCT cds{1, DWORD((args.size() + 1) * sizeof(wchar_t)), const_cast<wchar_t*>(args.c_str())};
  DWORD_PTR result = 0;
  AllowSetForegroundWindow(ASFW_ANY);
  SendMessageTimeoutW(other, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds), SMTO_ABORTIFHUNG, 3000, &result);
  if (proc) {
    WaitForSingleObject(proc, 5000);
    CloseHandle(proc);
  }
  return 0;
}

template <typename Fn>
int WithOle(Fn&& fn) {
  if (FAILED(OleInitialize(nullptr))) return 1;
  const int code = fn();
  opal::Gpu::Get().Shutdown();
  OleUninitialize();
  return code;
}

}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdLine, int) {
  const std::wstring args = cmdLine ? cmdLine : L"";
  InstallCrashHandlers();
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  if (opal::setup::HasArg(args, L"--setup")) return WithOle([&] { return opal::RunSetup(args); });
  if (opal::setup::HasArg(args, L"--copy-to")) return opal::setup::RunCopyHelper(args);
  if (opal::setup::HasArg(args, L"--uninstall")) return WithOle([&] { return opal::RunUninstall(args); });

  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\Opal.SingleInstance.6c1f3a52");
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    if (mutex) CloseHandle(mutex);
    return ForwardToRunningCopy(args);
  }
  if (opal::setup::HasArg(args, L"--quit")) {
    if (mutex) CloseHandle(mutex);
    return 0;
  }

  opal::LogInit();
  const int code = WithOle([&] { return opal::App::Get().Run(args); });
  opal::Log(L"Opal exiting (%d)", code);
  if (mutex) {
    ReleaseMutex(mutex);
    CloseHandle(mutex);
  }
  return code;
}
