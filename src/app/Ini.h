#pragma once
#include <map>
#include <string>
#include <string_view>

namespace opal {

class Ini {
 public:
  void Parse(std::wstring_view text);
  std::wstring Serialize() const;

  std::wstring Get(const std::wstring& section, const std::wstring& key,
                   const std::wstring& fallback = {}) const;
  bool Has(const std::wstring& section, const std::wstring& key) const;
  bool GetBool(const std::wstring& section, const std::wstring& key, bool fallback) const;
  int GetInt(const std::wstring& section, const std::wstring& key, int fallback) const;

  void Set(const std::wstring& section, const std::wstring& key, const std::wstring& value);
  void SetBool(const std::wstring& section, const std::wstring& key, bool value);
  void SetInt(const std::wstring& section, const std::wstring& key, int value);

 private:
  std::map<std::wstring, std::map<std::wstring, std::wstring>> sections_;
};

}
