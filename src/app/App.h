#pragma once
#include <windows.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/Settings.h"
#include "app/Tray.h"
#include "app/Updater.h"
#include "core/Image.h"
#include "overlay/CaptureSession.h"

namespace opal {

class D2DWindow;
class ThumbnailWindow;
class SettingsWindow;

constexpr wchar_t kAppWindowClass[] = L"OpalAppWindow";

class App {
 public:
  static App& Get();

  int Run(const std::wstring& args);
  HWND hwnd() const { return hwnd_; }

  void StartCapture(Action a);
  void OpenSettings();
  void OpenEditor(const Image& image, const std::wstring& path, float captureScale);
  void ApplySettings();
  void Deliver(const Image& img, const std::wstring& path, bool clipboard, ThumbnailWindow* notify);
  void HandleCommandLine(const std::wstring& args, bool fromOtherInstance);
  void Quit();

 private:
  App() = default;
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
  void SyncLaunchAtStartup();
  void OnUpdateEvent(Updater::Event e);
  void OnSessionDone();
  void HandleResult(CaptureResult&& r);
  void Adopt(std::unique_ptr<D2DWindow> w);
  void OnWindowDestroyed(D2DWindow* w);
  void RefreshAllWindows();

  HWND hwnd_ = nullptr;
  Tray tray_;
  UINT taskbarCreated_ = 0;
  std::unique_ptr<CaptureSession> session_;
  std::optional<CaptureResult> pending_;
  bool sessionDone_ = false;
  std::vector<std::unique_ptr<D2DWindow>> windows_;
  ThumbnailWindow* thumbnail_ = nullptr;
  SettingsWindow* settings_ = nullptr;
  bool trimmed_ = false;
};

}
