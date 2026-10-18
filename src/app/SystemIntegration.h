#pragma once
#include <string>

namespace opal::sys {

std::wstring ExePath();

bool IsLaunchAtStartup();
std::wstring LaunchAtStartupTarget();
void SetLaunchAtStartup(bool enable, const std::wstring& exe = ExePath());

bool IsWindowsSnipKeyEnabled();
void SetWindowsSnipKeyEnabled(bool enable);

bool EnsureDirectory(const std::wstring& path);
void OpenFolder(const std::wstring& path);
void RevealFile(const std::wstring& path);
void OpenFile(const std::wstring& path);

void SetDarkMenus(bool dark);

}
