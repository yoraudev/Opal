#include "app/Updater.h"

#include <winhttp.h>

#include <algorithm>
#include <memory>
#include <thread>
#include <vector>

#include "Version.h"
#include "app/Settings.h"
#include "core/Log.h"
#include "setup/InstallRules.h"
#include "setup/Installer.h"
#include "setup/ReleaseInfo.h"

namespace opal {
namespace {

constexpr wchar_t kLatestRelease[] = L"https://api.github.com/repos/yoraudev/Opal/releases/latest";
constexpr long long kCheckInterval = 24 * 60 * 60;
constexpr size_t kMaxApiBytes = 2 * 1024 * 1024;
constexpr uint64_t kMaxSetupBytes = 64ull * 1024 * 1024;
constexpr WPARAM kCheckDone = 1;
constexpr WPARAM kDownloadDone = 2;

struct CheckResult {
  bool ok = false;
  std::wstring version;
  std::wstring url;
  uint64_t size = 0;
};

struct DownloadResult {
  bool ok = false;
  std::wstring path;
};

long long NowSeconds() {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  const ULARGE_INTEGER t{{ft.dwLowDateTime, ft.dwHighDateTime}};
  return static_cast<long long>((t.QuadPart - 116444736000000000ull) / 10000000ull);
}

std::wstring UpdateDir() {
  wchar_t temp[MAX_PATH];
  GetTempPathW(MAX_PATH, temp);
  return std::wstring(temp) + L"Opal-Update";
}

void RemoveUpdateDir() {
  const std::wstring dir = UpdateDir();
  WIN32_FIND_DATAW fd;
  HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &fd);
  if (find == INVALID_HANDLE_VALUE) return;
  do {
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
  } while (FindNextFileW(find, &fd));
  FindClose(find);
  RemoveDirectoryW(dir.c_str());
}

class Handle {
 public:
  explicit Handle(HINTERNET h = nullptr) : h_(h) {}
  ~Handle() {
    if (h_) WinHttpCloseHandle(h_);
  }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  HINTERNET get() const { return h_; }
  explicit operator bool() const { return h_ != nullptr; }

 private:
  HINTERNET h_;
};

bool HttpGet(const std::wstring& url, bool api, std::string* body, HANDLE file, uint64_t limit, uint64_t* received) {
  *received = 0;
  URL_COMPONENTS parts{sizeof(parts)};
  wchar_t host[256] = {}, path[2048] = {};
  parts.lpszHostName = host;
  parts.dwHostNameLength = ARRAYSIZE(host);
  parts.lpszUrlPath = path;
  parts.dwUrlPathLength = ARRAYSIZE(path);
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
    Log(L"Update: refusing %s", url.c_str());
    return false;
  }
  const std::wstring agent = std::wstring(L"Opal/") + OPAL_VERSION_WTEXT;
  Handle session(WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                             WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session) return false;
  WinHttpSetTimeouts(session.get(), 10000, 10000, 15000, 30000);
  Handle connection(WinHttpConnect(session.get(), host, parts.nPort, 0));
  if (!connection) return false;
  Handle request(WinHttpOpenRequest(connection.get(), L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                    WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
  if (!request) return false;
  const wchar_t* headers = api ? L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28" : nullptr;
  if (!WinHttpSendRequest(request.get(), headers, headers ? DWORD(-1L) : 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.get(), nullptr)) {
    Log(L"Update: request to %s failed: %lu", host, GetLastError());
    return false;
  }
  DWORD status = 0, size = sizeof(status);
  WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                      &status, &size, WINHTTP_NO_HEADER_INDEX);
  if (status != 200) {
    Log(L"Update: %s answered %lu", host, status);
    return false;
  }
  std::vector<char> buf(64 * 1024);
  for (;;) {
    DWORD got = 0;
    if (!WinHttpReadData(request.get(), buf.data(), DWORD(buf.size()), &got)) return false;
    if (got == 0) return true;
    *received += got;
    if (*received > limit) return false;
    if (body) body->append(buf.data(), got);
    DWORD written = 0;
    if (file && (!WriteFile(file, buf.data(), got, &written, nullptr) || written != got)) return false;
  }
}

void CheckWorker(HWND owner, UINT msg) {
  auto* r = new CheckResult;
  std::string body;
  uint64_t received = 0;
  if (HttpGet(kLatestRelease, true, &body, nullptr, kMaxApiBytes, &received)) {
    if (auto release = setup::ParseRelease(body)) {
      r->ok = true;
      r->version = release->version;
      if (const setup::ReleaseAsset* asset = setup::FindSetupAsset(*release)) {
        r->url = asset->url;
        r->size = asset->size;
      }
      Log(L"Update: latest release is %s%s", r->version.c_str(), r->url.empty() ? L" (no installer)" : L"");
    } else {
      Log(L"Update: unreadable release data (%llu bytes)", static_cast<unsigned long long>(received));
    }
  }
  if (!PostMessageW(owner, msg, kCheckDone, reinterpret_cast<LPARAM>(r))) delete r;
}

void DownloadWorker(HWND owner, UINT msg, std::wstring version, std::wstring url, uint64_t expected) {
  auto* r = new DownloadResult;
  const std::wstring dir = UpdateDir();
  CreateDirectoryW(dir.c_str(), nullptr);
  r->path = dir + L"\\Opal-Setup-" + version + L".exe";
  const std::wstring part = r->path + L".part";
  HANDLE f = CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  uint64_t received = 0;
  bool ok = false;
  if (f != INVALID_HANDLE_VALUE) {
    ok = HttpGet(url, false, nullptr, f, std::min(expected, kMaxSetupBytes), &received);
    CloseHandle(f);
  }
  if (ok && received != expected) {
    Log(L"Update: downloaded %llu bytes, expected %llu", static_cast<unsigned long long>(received),
        static_cast<unsigned long long>(expected));
    ok = false;
  }
  r->ok = ok && MoveFileExW(part.c_str(), r->path.c_str(), MOVEFILE_REPLACE_EXISTING);
  if (!r->ok) DeleteFileW(part.c_str());
  if (!PostMessageW(owner, msg, kDownloadDone, reinterpret_cast<LPARAM>(r))) delete r;
}

}

Updater& Updater::Get() {
  static Updater u;
  return u;
}

void Updater::Init(HWND owner, UINT msg) {
  owner_ = owner;
  msg_ = msg;
  supported_ = setup::IsInstalledCopy();
  RemoveUpdateDir();
}

void Updater::CheckIfDue() {
  const Settings& s = AppSettings();
  if (!supported_ || !s.checkForUpdates || Busy() || state_ == State::Available) return;
  const long long now = NowSeconds();
  if (s.lastUpdateCheck > 0 && s.lastUpdateCheck <= now && now - s.lastUpdateCheck < kCheckInterval) return;
  Check();
}

void Updater::Check() {
  if (!supported_ || Busy()) return;
  state_ = State::Checking;
  Settings& s = AppSettings();
  s.lastUpdateCheck = NowSeconds();
  s.Save();
  std::thread(CheckWorker, owner_, msg_).detach();
}

void Updater::Install() {
  if (state_ != State::Available && state_ != State::DownloadFailed) return;
  if (url_.empty()) return;
  state_ = State::Downloading;
  Log(L"Update: downloading %s", url_.c_str());
  std::thread(DownloadWorker, owner_, msg_, version_, url_, size_).detach();
}

Updater::Event Updater::OnMessage(WPARAM wp, LPARAM lp) {
  if (wp == kCheckDone) {
    std::unique_ptr<CheckResult> r(reinterpret_cast<CheckResult*>(lp));
    if (!r->ok) {
      state_ = State::CheckFailed;
      return Event::None;
    }
    if (setup::CompareVersions(OPAL_VERSION_WTEXT, r->version) >= 0 || r->url.empty()) {
      state_ = State::UpToDate;
      return Event::None;
    }
    state_ = State::Available;
    version_ = r->version;
    url_ = r->url;
    size_ = r->size;
    return Event::Available;
  }
  if (wp == kDownloadDone) {
    std::unique_ptr<DownloadResult> r(reinterpret_cast<DownloadResult*>(lp));
    if (!r->ok) {
      state_ = State::DownloadFailed;
      return Event::Failed;
    }
    std::wstring cmd = L"\"" + r->path + L"\" --silent";
    const std::wstring dir = UpdateDir();
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(r->path.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
      Log(L"Update: starting %s failed: %lu", r->path.c_str(), GetLastError());
      state_ = State::DownloadFailed;
      return Event::Failed;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    Log(L"Update: running %s", r->path.c_str());
    state_ = State::Installing;
  }
  return Event::None;
}

}
