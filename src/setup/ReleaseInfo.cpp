#include "setup/ReleaseInfo.h"

#include <algorithm>
#include <utility>

namespace opal::setup {
namespace {

struct Json {
  enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
  bool boolean = false;
  double number = 0;
  std::wstring text;
  std::vector<Json> items;
  std::vector<std::pair<std::wstring, Json>> members;

  const Json* Get(std::wstring_view key) const {
    for (const auto& [k, v] : members) {
      if (k == key) return &v;
    }
    return nullptr;
  }
};

class Parser {
 public:
  explicit Parser(std::string_view s) : s_(s) {}

  bool Parse(Json* out) {
    if (!Value(out, 0)) return false;
    Space();
    return pos_ == s_.size();
  }

 private:
  static constexpr int kMaxDepth = 64;

  void Space() {
    while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) ++pos_;
  }

  bool Literal(std::string_view word) {
    if (s_.substr(pos_, word.size()) != word) return false;
    pos_ += word.size();
    return true;
  }

  static void AppendUtf32(std::wstring* out, uint32_t cp) {
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out->push_back(wchar_t(0xD800 + (cp >> 10)));
      out->push_back(wchar_t(0xDC00 + (cp & 0x3FF)));
    } else {
      out->push_back(wchar_t(cp));
    }
  }

  bool Hex4(uint32_t* v) {
    if (pos_ + 4 > s_.size()) return false;
    *v = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = s_[pos_++];
      *v <<= 4;
      if (c >= '0' && c <= '9') *v |= uint32_t(c - '0');
      else if (c >= 'a' && c <= 'f') *v |= uint32_t(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') *v |= uint32_t(c - 'A' + 10);
      else return false;
    }
    return true;
  }

  bool String(std::wstring* out) {
    if (pos_ >= s_.size() || s_[pos_] != '"') return false;
    ++pos_;
    while (pos_ < s_.size()) {
      const unsigned char c = static_cast<unsigned char>(s_[pos_]);
      if (c == '"') {
        ++pos_;
        return true;
      }
      if (c < 0x20) return false;
      if (c == '\\') {
        if (++pos_ >= s_.size()) return false;
        const char e = s_[pos_++];
        switch (e) {
          case '"': out->push_back(L'"'); break;
          case '\\': out->push_back(L'\\'); break;
          case '/': out->push_back(L'/'); break;
          case 'b': out->push_back(L'\b'); break;
          case 'f': out->push_back(L'\f'); break;
          case 'n': out->push_back(L'\n'); break;
          case 'r': out->push_back(L'\r'); break;
          case 't': out->push_back(L'\t'); break;
          case 'u': {
            uint32_t cp = 0;
            if (!Hex4(&cp)) return false;
            if (cp >= 0xD800 && cp < 0xDC00 && s_.substr(pos_, 2) == "\\u") {
              pos_ += 2;
              uint32_t lo = 0;
              if (!Hex4(&lo)) return false;
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            }
            AppendUtf32(out, cp);
            break;
          }
          default:
            return false;
        }
        continue;
      }
      uint32_t cp = c;
      int extra = 0;
      if (c >= 0xF0) {
        cp = c & 0x07;
        extra = 3;
      } else if (c >= 0xE0) {
        cp = c & 0x0F;
        extra = 2;
      } else if (c >= 0xC0) {
        cp = c & 0x1F;
        extra = 1;
      }
      ++pos_;
      for (int i = 0; i < extra; ++i) {
        if (pos_ >= s_.size()) return false;
        cp = (cp << 6) | (static_cast<unsigned char>(s_[pos_++]) & 0x3F);
      }
      AppendUtf32(out, cp);
    }
    return false;
  }

  bool Number(double* out) {
    const size_t start = pos_;
    if (pos_ < s_.size() && s_[pos_] == '-') ++pos_;
    double v = 0;
    bool digits = false;
    while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') {
      v = v * 10 + (s_[pos_++] - '0');
      digits = true;
    }
    if (pos_ < s_.size() && s_[pos_] == '.') {
      ++pos_;
      double scale = 0.1;
      while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') {
        v += (s_[pos_++] - '0') * scale;
        scale *= 0.1;
        digits = true;
      }
    }
    if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
      ++pos_;
      const bool negative = pos_ < s_.size() && s_[pos_] == '-';
      if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
      int exp = 0;
      while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') exp = std::min(exp * 10 + (s_[pos_++] - '0'), 400);
      for (int i = 0; i < exp; ++i) v = negative ? v / 10 : v * 10;
    }
    *out = s_[start] == '-' ? -v : v;
    return digits;
  }

  bool Value(Json* out, int depth) {
    if (depth > kMaxDepth) return false;
    Space();
    if (pos_ >= s_.size()) return false;
    const char c = s_[pos_];
    if (c == '{') {
      ++pos_;
      out->type = Json::Type::Object;
      Space();
      if (pos_ < s_.size() && s_[pos_] == '}') {
        ++pos_;
        return true;
      }
      for (;;) {
        Space();
        std::wstring key;
        if (!String(&key)) return false;
        Space();
        if (pos_ >= s_.size() || s_[pos_++] != ':') return false;
        Json v;
        if (!Value(&v, depth + 1)) return false;
        out->members.emplace_back(std::move(key), std::move(v));
        Space();
        if (pos_ >= s_.size()) return false;
        if (s_[pos_] == ',') {
          ++pos_;
          continue;
        }
        if (s_[pos_++] != '}') return false;
        return true;
      }
    }
    if (c == '[') {
      ++pos_;
      out->type = Json::Type::Array;
      Space();
      if (pos_ < s_.size() && s_[pos_] == ']') {
        ++pos_;
        return true;
      }
      for (;;) {
        Json v;
        if (!Value(&v, depth + 1)) return false;
        out->items.push_back(std::move(v));
        Space();
        if (pos_ >= s_.size()) return false;
        if (s_[pos_] == ',') {
          ++pos_;
          continue;
        }
        if (s_[pos_++] != ']') return false;
        return true;
      }
    }
    if (c == '"') {
      out->type = Json::Type::String;
      return String(&out->text);
    }
    if (Literal("true")) {
      out->type = Json::Type::Bool;
      out->boolean = true;
      return true;
    }
    if (Literal("false")) {
      out->type = Json::Type::Bool;
      return true;
    }
    if (Literal("null")) return true;
    out->type = Json::Type::Number;
    return Number(&out->number);
  }

  std::string_view s_;
  size_t pos_ = 0;
};

std::wstring Text(const Json& obj, std::wstring_view key) {
  const Json* v = obj.Get(key);
  return v && v->type == Json::Type::String ? v->text : std::wstring();
}

bool Flag(const Json& obj, std::wstring_view key) {
  const Json* v = obj.Get(key);
  return v && v->type == Json::Type::Bool && v->boolean;
}

}

std::optional<ReleaseInfo> ParseRelease(std::string_view json) {
  Json root;
  if (!Parser(json).Parse(&root) || root.type != Json::Type::Object) return std::nullopt;
  ReleaseInfo out;
  out.version = VersionFromTag(Text(root, L"tag_name"));
  if (out.version.empty()) return std::nullopt;
  out.draft = Flag(root, L"draft");
  out.prerelease = Flag(root, L"prerelease");
  if (const Json* assets = root.Get(L"assets"); assets && assets->type == Json::Type::Array) {
    for (const Json& a : assets->items) {
      if (a.type != Json::Type::Object) continue;
      ReleaseAsset asset;
      asset.name = Text(a, L"name");
      asset.url = Text(a, L"browser_download_url");
      const Json* size = a.Get(L"size");
      if (size && size->type == Json::Type::Number && size->number > 0) asset.size = uint64_t(size->number);
      if (!asset.name.empty() && !asset.url.empty()) out.assets.push_back(std::move(asset));
    }
  }
  return out;
}

std::wstring VersionFromTag(std::wstring_view tag) {
  if (!tag.empty() && (tag.front() == L'v' || tag.front() == L'V')) tag.remove_prefix(1);
  if (tag.empty() || tag.front() < L'0' || tag.front() > L'9') return {};
  for (wchar_t c : tag) {
    if (!((c >= L'0' && c <= L'9') || c == L'.')) return {};
  }
  return std::wstring(tag);
}

const ReleaseAsset* FindSetupAsset(const ReleaseInfo& release) {
  const std::wstring want = L"Opal-Setup-" + release.version + L".exe";
  for (const auto& a : release.assets) {
    if (a.name == want && a.size > 0 && IsTrustedDownload(a.url)) return &a;
  }
  return nullptr;
}

bool IsTrustedDownload(std::wstring_view url) {
  constexpr std::wstring_view kPrefix = L"https://github.com/yoraudev/Opal/releases/download/";
  return url.size() > kPrefix.size() && url.substr(0, kPrefix.size()) == kPrefix &&
         url.find(L"..") == std::wstring_view::npos;
}

}
