#pragma once
#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>

namespace opal::setup {

struct Installed {
  bool present = false;
  std::wstring dir;
  std::wstring version;
};

struct InstallOptions {
  std::wstring dir;
  bool autostart = true;
  bool startMenu = true;
  bool desktop = false;
};

enum class Step { Stopping, Copying, Registering, Removing, Finished };

Installed FindInstalled();
std::wstring DefaultInstallDir();
bool IsInstalledCopy();
InstallOptions CurrentOptions(const Installed& installed);
void CleanUpAfterUpdate();
bool IsElevated();
bool CanWriteTo(const std::wstring& dir);
uint64_t NeededBytes();
bool FreeBytes(const std::wstring& dir, uint64_t* bytes);
DWORD RunElevated(const std::wstring& args, HWND owner);
int RunCopyHelper(const std::wstring& args);

bool StopRunningOpal(bool* wasRunning);
bool Install(const InstallOptions& options, const std::function<void(Step)>& progress, std::wstring* error,
             bool* wasRunning, HWND owner);
bool Uninstall(const Installed& installed, bool removeUserData, const std::function<void(Step)>& progress);
void ScheduleCleanup();
bool Launch(const std::wstring& dir, bool background);

}
