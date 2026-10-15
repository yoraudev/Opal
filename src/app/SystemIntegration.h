#pragma once
#include <string>

namespace opal::sys {

std::wstring ExePath();

bool IsLaunchAtStartup();
void SetLaunchAtStartup(bool enable);

bool IsWindowsSnipKeyEnabled();
void SetWindowsSnipKeyEnabled(bool enable);

bool EnsureDirectory(const std::wstring& path);
void OpenFolder(const std::wstring& path);
void RevealFile(const std::wstring& path);
void OpenFile(const std::wstring& path);

void SetDarkMenus(bool dark);

}
