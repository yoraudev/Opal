#include "core/Log.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace opal {
namespace {

std::mutex g_mutex;
HANDLE g_file = INVALID_HANDLE_VALUE;
LARGE_INTEGER g_start{}, g_freq{};

}

void LogInit(const wchar_t* name) {
  QueryPerformanceFrequency(&g_freq);
  QueryPerformanceCounter(&g_start);
  PWSTR base = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) return;
  std::wstring dir = std::wstring(base) + L"\\Opal";
  CoTaskMemFree(base);
  CreateDirectoryW(dir.c_str(), nullptr);
  const std::wstring path = dir + L"\\" + name + L".log";
  MoveFileExW(path.c_str(), (dir + L"\\" + name + L".prev.log").c_str(), MOVEFILE_REPLACE_EXISTING);
  g_file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
  SYSTEMTIME t;
  GetLocalTime(&t);
  Log(L"Log opened %04d-%02d-%02d %02d:%02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
}

void Log(const wchar_t* fmt, ...) {
  wchar_t msg[1024];
  va_list args;
  va_start(args, fmt);
  _vsnwprintf_s(msg, _TRUNCATE, fmt, args);
  va_end(args);

  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  const double ms = g_freq.QuadPart ? double(now.QuadPart - g_start.QuadPart) * 1000.0 / g_freq.QuadPart : 0;
  wchar_t line[1200];
  swprintf_s(line, L"[%9.1f] %s\r\n", ms, msg);
  OutputDebugStringW(line);

  std::lock_guard lock(g_mutex);
  if (g_file == INVALID_HANDLE_VALUE) return;
  char utf8[3600];
  int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8), nullptr, nullptr);
  if (n > 1) {
    DWORD written = 0;
    WriteFile(g_file, utf8, DWORD(n - 1), &written, nullptr);
  }
}

}
