#pragma once
#include <windows.h>

#include <cstdint>
#include <string>

namespace opal {

class Updater {
 public:
  enum class State { Idle, Checking, UpToDate, Available, Downloading, Installing, CheckFailed, DownloadFailed };
  enum class Event { None, Available, Failed };

  static Updater& Get();

  void Init(HWND owner, UINT msg);
  bool Supported() const { return supported_; }
  State GetState() const { return state_; }
  const std::wstring& Version() const { return version_; }
  bool Busy() const { return state_ == State::Checking || state_ == State::Downloading || state_ == State::Installing; }

  void CheckIfDue();
  void Check();
  void Install();
  Event OnMessage(WPARAM wp, LPARAM lp);

 private:
  Updater() = default;

  HWND owner_ = nullptr;
  UINT msg_ = 0;
  bool supported_ = false;
  State state_ = State::Idle;
  std::wstring version_;
  std::wstring url_;
  uint64_t size_ = 0;
};

}
