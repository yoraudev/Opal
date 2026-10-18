#include <windows.h>

#include <string>

namespace {

constexpr int kPayloadId = 2;

bool WritePayload(const std::wstring& path) {
  HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(kPayloadId), RT_RCDATA);
  HGLOBAL handle = res ? LoadResource(nullptr, res) : nullptr;
  const void* data = handle ? LockResource(handle) : nullptr;
  const DWORD size = res ? SizeofResource(nullptr, res) : 0;
  if (!data || size == 0) return false;
  HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const BOOL ok = WriteFile(f, data, size, &written, nullptr);
  CloseHandle(f);
  return ok && written == size;
}

void Fail(const wchar_t* what) {
  wchar_t msg[512];
  swprintf_s(msg, L"Opal Setup could not start: %s (error %lu).", what, GetLastError());
  MessageBoxW(nullptr, msg, L"Opal Setup", MB_ICONERROR);
}

}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdLine, int) {
  wchar_t temp[MAX_PATH];
  if (!GetTempPathW(MAX_PATH, temp)) {
    Fail(L"no temporary folder");
    return 1;
  }
  const std::wstring dir = std::wstring(temp) + L"Opal-Setup-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                           std::to_wstring(GetTickCount64());
  const std::wstring exe = dir + L"\\Opal.exe";
  if (!CreateDirectoryW(dir.c_str(), nullptr) || !WritePayload(exe)) {
    Fail(L"extracting the installer failed");
    RemoveDirectoryW(dir.c_str());
    return 1;
  }
  std::wstring cmd = L"\"" + exe + L"\" --setup " + (cmdLine ? cmdLine : L"");
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION pi{};
  DWORD code = 1;
  if (CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
  } else {
    Fail(L"running the installer failed");
  }
  for (int i = 0; i < 20 && !DeleteFileW(exe.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND; ++i) Sleep(100);
  RemoveDirectoryW(dir.c_str());
  return int(code);
}
