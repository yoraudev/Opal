#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace opal::setup {

int CompareVersions(std::wstring_view a, std::wstring_view b);
std::wstring NormalizeInstallDir(std::wstring_view picked);
bool SamePath(std::wstring_view a, std::wstring_view b);
std::wstring StartupCommand(std::wstring_view exe);
std::wstring CommandExe(std::wstring_view command);
bool StartupApproved(const uint8_t* data, size_t size);
bool HasArg(std::wstring_view args, std::wstring_view name);
std::optional<std::wstring> ArgValue(std::wstring_view args, std::wstring_view name);
std::wstring FormatSize(uint64_t bytes, wchar_t decimalPoint);
std::wstring DriveOf(std::wstring_view path);

}
