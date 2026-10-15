#include <windows.h>
#include <dbghelp.h>
#include <objbase.h>
#include <ole2.h>
#include <shlobj.h>

#include <string>

#include "app/App.h"
#include "core/Log.h"

namespace {

LONG WINAPI OnCrash(EXCEPTION_POINTERS* ep) {
  HMODULE mod = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     static_cast<LPCWSTR>(ep->ExceptionRecord->ExceptionAddress), &mod);
  opal::Log(L"CRASH: code 0x%08X at %p (module base %p, offset 0x%llX)", ep->ExceptionRecord->ExceptionCode,
            ep->ExceptionRecord->ExceptionAddress, mod,
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress) -
                                            reinterpret_cast<uintptr_t>(mod)));
  PWSTR base = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) {
    const std::wstring path = std::wstring(base) + L"\\Opal\\crash.dmp";
    CoTaskMemFree(base);
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
      MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), ep, FALSE};
      MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f, MiniDumpNormal, &mei, nullptr, nullptr);
      CloseHandle(f);
    }
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdLine, int) {
  const std::wstring args = cmdLine ? cmdLine : L"";

  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\Opal.SingleInstance.6c1f3a52");
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    if (HWND other = FindWindowW(opal::kAppWindowClass, nullptr)) {
      COPYDATASTRUCT cds{1, DWORD((args.size() + 1) * sizeof(wchar_t)), const_cast<wchar_t*>(args.c_str())};
      DWORD_PTR result = 0;
      AllowSetForegroundWindow(ASFW_ANY);
      SendMessageTimeoutW(other, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds), SMTO_ABORTIFHUNG, 3000, &result);
    }
    if (mutex) CloseHandle(mutex);
    return 0;
  }

  opal::LogInit();
  SetUnhandledExceptionFilter(&OnCrash);
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  if (FAILED(OleInitialize(nullptr))) return 1;
  const int code = opal::App::Get().Run(args);
  opal::Log(L"Opal exiting (%d)", code);
  OleUninitialize();
  if (mutex) {
    ReleaseMutex(mutex);
    CloseHandle(mutex);
  }
  return code;
}
