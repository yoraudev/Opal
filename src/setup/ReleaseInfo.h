#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opal::setup {

struct ReleaseAsset {
  std::wstring name;
  std::wstring url;
  uint64_t size = 0;
};

struct ReleaseInfo {
  std::wstring version;
  bool draft = false;
  bool prerelease = false;
  std::vector<ReleaseAsset> assets;
};

std::optional<ReleaseInfo> ParseRelease(std::string_view json);
std::wstring VersionFromTag(std::wstring_view tag);
const ReleaseAsset* FindSetupAsset(const ReleaseInfo& release);
bool IsTrustedDownload(std::wstring_view url);

}
