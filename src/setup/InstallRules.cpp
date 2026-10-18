#include "setup/InstallRules.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <vector>

namespace opal::setup {
namespace {

std::wstring_view Trim(std::wstring_view s) {
  while (!s.empty() && std::iswspace(s.front())) s.remove_prefix(1);
  while (!s.empty() && std::iswspace(s.back())) s.remove_suffix(1);
  return s;
}

bool IEquals(std::wstring_view a, std::wstring_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::towupper(a[i]) != std::towupper(b[i])) return false;
  }
  return true;
}

std::wstring CleanPath(std::wstring_view p) {
  std::wstring out(Trim(p));
  for (auto& c : out) {
    if (c == L'/') c = L'\\';
  }
  while (out.size() > 1 && out.back() == L'\\') out.pop_back();
  return out;
}

std::vector<std::wstring> Tokens(std::wstring_view args) {
  std::vector<std::wstring> out;
  size_t i = 0;
  while (i < args.size()) {
    while (i < args.size() && std::iswspace(args[i])) ++i;
    if (i >= args.size()) break;
    std::wstring tok;
    bool quoted = false;
    while (i < args.size() && (quoted || !std::iswspace(args[i]))) {
      if (args[i] == L'"') {
        quoted = !quoted;
      } else {
        tok.push_back(args[i]);
      }
      ++i;
    }
    out.push_back(std::move(tok));
  }
  return out;
}

}

int CompareVersions(std::wstring_view a, std::wstring_view b) {
  auto next = [](std::wstring_view& s) {
    unsigned long long v = 0;
    while (!s.empty() && s.front() >= L'0' && s.front() <= L'9') {
      v = v * 10 + unsigned(s.front() - L'0');
      s.remove_prefix(1);
    }
    while (!s.empty() && s.front() != L'.') s.remove_prefix(1);
    if (!s.empty()) s.remove_prefix(1);
    return v;
  };
  a = Trim(a);
  b = Trim(b);
  for (int i = 0; i < 4; ++i) {
    const unsigned long long x = next(a), y = next(b);
    if (x != y) return x < y ? -1 : 1;
  }
  return 0;
}

std::wstring NormalizeInstallDir(std::wstring_view picked) {
  std::wstring dir = CleanPath(picked);
  if (dir.empty()) return dir;
  if (dir.size() == 2 && dir[1] == L':') return dir + L"\\Opal";
  const size_t slash = dir.find_last_of(L'\\');
  const std::wstring_view last = slash == std::wstring::npos ? std::wstring_view(dir) : std::wstring_view(dir).substr(slash + 1);
  if (IEquals(last, L"Opal")) return dir;
  return dir + L"\\Opal";
}

bool SamePath(std::wstring_view a, std::wstring_view b) {
  const std::wstring x = CleanPath(a), y = CleanPath(b);
  return !x.empty() && IEquals(x, y);
}

std::wstring StartupCommand(std::wstring_view exe) { return L"\"" + std::wstring(exe) + L"\" --background"; }

std::wstring CommandExe(std::wstring_view command) {
  command = Trim(command);
  if (command.empty()) return {};
  if (command.front() == L'"') {
    command.remove_prefix(1);
    const size_t end = command.find(L'"');
    return std::wstring(command.substr(0, end));
  }
  for (size_t i = 0; i + 4 <= command.size(); ++i) {
    if (IEquals(command.substr(i, 4), L".exe") && (i + 4 == command.size() || std::iswspace(command[i + 4]))) {
      return std::wstring(command.substr(0, i + 4));
    }
  }
  const size_t space = command.find(L' ');
  return std::wstring(command.substr(0, space));
}

bool StartupApproved(const uint8_t* data, size_t size) { return !data || size == 0 || (data[0] & 1) == 0; }

bool HasArg(std::wstring_view args, std::wstring_view name) {
  for (const auto& t : Tokens(args)) {
    if (IEquals(t, name)) return true;
  }
  return false;
}

std::wstring FormatSize(uint64_t bytes, wchar_t decimalPoint) {
  static constexpr const wchar_t* kUnits[] = {L"MB", L"GB", L"TB"};
  if (bytes < 1024ull * 1024) return std::to_wstring(std::max<uint64_t>(1, (bytes + 512) / 1024)) + L" KB";
  double v = double(bytes) / (1024.0 * 1024.0);
  int unit = 0;
  while (v >= 1024.0 && unit < 2) {
    v /= 1024.0;
    ++unit;
  }
  std::wstring out;
  if (v < 100.0) {
    const long long tenths = std::llround(v * 10.0);
    out = std::to_wstring(tenths / 10) + decimalPoint + std::to_wstring(tenths % 10);
  } else {
    out = std::to_wstring(std::llround(v));
  }
  return out + L" " + kUnits[unit];
}

std::wstring DriveOf(std::wstring_view path) {
  const std::wstring p = CleanPath(path);
  if (p.size() >= 2 && p[1] == L':') return std::wstring(1, wchar_t(std::towupper(p[0]))) + L":";
  if (p.size() > 2 && p[0] == L'\\' && p[1] == L'\\') {
    const size_t server = p.find(L'\\', 2);
    if (server == std::wstring::npos) return p;
    const size_t share = p.find(L'\\', server + 1);
    return share == std::wstring::npos ? p : p.substr(0, share);
  }
  return {};
}

std::optional<std::wstring> ArgValue(std::wstring_view args, std::wstring_view name) {
  const std::vector<std::wstring> tokens = Tokens(args);
  for (size_t i = 0; i < tokens.size(); ++i) {
    const std::wstring_view t = tokens[i];
    if (IEquals(t, name)) {
      if (i + 1 < tokens.size()) return tokens[i + 1];
      return std::nullopt;
    }
    if (t.size() > name.size() && t[name.size()] == L'=' && IEquals(t.substr(0, name.size()), name)) {
      return std::wstring(t.substr(name.size() + 1));
    }
  }
  return std::nullopt;
}

}
