#include "app/Ini.h"

#include <cwctype>

namespace opal {
namespace {

std::wstring_view Trim(std::wstring_view s) {
  while (!s.empty() && std::iswspace(s.front())) s.remove_prefix(1);
  while (!s.empty() && std::iswspace(s.back())) s.remove_suffix(1);
  return s;
}

}

void Ini::Parse(std::wstring_view text) {
  sections_.clear();
  if (!text.empty() && text.front() == 0xFEFF) text.remove_prefix(1);
  std::wstring section;
  while (!text.empty()) {
    size_t nl = text.find(L'\n');
    std::wstring_view line = Trim(text.substr(0, nl));
    text = nl == std::wstring_view::npos ? std::wstring_view{} : text.substr(nl + 1);
    if (line.empty() || line.front() == L';' || line.front() == L'#') continue;
    if (line.front() == L'[' && line.back() == L']') {
      section = std::wstring(Trim(line.substr(1, line.size() - 2)));
      continue;
    }
    size_t eq = line.find(L'=');
    if (eq == std::wstring_view::npos) continue;
    sections_[section][std::wstring(Trim(line.substr(0, eq)))] =
        std::wstring(Trim(line.substr(eq + 1)));
  }
}

std::wstring Ini::Serialize() const {
  std::wstring out;
  for (const auto& [name, keys] : sections_) {
    if (!out.empty()) out += L"\r\n";
    out += L"[" + name + L"]\r\n";
    for (const auto& [k, v] : keys) out += k + L"=" + v + L"\r\n";
  }
  return out;
}

std::wstring Ini::Get(const std::wstring& section, const std::wstring& key,
                      const std::wstring& fallback) const {
  auto s = sections_.find(section);
  if (s == sections_.end()) return fallback;
  auto k = s->second.find(key);
  return k == s->second.end() ? fallback : k->second;
}

bool Ini::Has(const std::wstring& section, const std::wstring& key) const {
  auto s = sections_.find(section);
  return s != sections_.end() && s->second.contains(key);
}

bool Ini::GetBool(const std::wstring& section, const std::wstring& key, bool fallback) const {
  std::wstring v = Get(section, key);
  if (v == L"1" || v == L"true" || v == L"yes") return true;
  if (v == L"0" || v == L"false" || v == L"no") return false;
  return fallback;
}

int Ini::GetInt(const std::wstring& section, const std::wstring& key, int fallback) const {
  std::wstring v = Get(section, key);
  if (v.empty()) return fallback;
  try {
    size_t used = 0;
    int n = std::stoi(v, &used);
    return used == v.size() ? n : fallback;
  } catch (...) {
    return fallback;
  }
}

void Ini::Set(const std::wstring& section, const std::wstring& key, const std::wstring& value) {
  sections_[section][key] = value;
}
void Ini::SetBool(const std::wstring& section, const std::wstring& key, bool value) {
  Set(section, key, value ? L"1" : L"0");
}
void Ini::SetInt(const std::wstring& section, const std::wstring& key, int value) {
  Set(section, key, std::to_wstring(value));
}

}
