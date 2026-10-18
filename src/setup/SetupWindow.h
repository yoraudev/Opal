#pragma once
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "render/D2DWindow.h"
#include "setup/Installer.h"

namespace opal {

class SetupWindow : public D2DWindow {
 public:
  enum class Kind { Fresh, Update, Reinstall, Downgrade, Uninstall };
  enum class Page { Options, Working, Done, Failed };

  SetupWindow(Kind kind, setup::Installed installed);
  ~SetupWindow() override;

  bool Init();
  int ExitCode() const { return exitCode_; }
  void Preview(Page page, setup::Step step, const std::wstring& error);
  static D2D1_SIZE_F Size();

 protected:
  void OnRender(ID2D1DeviceContext5* dc) override;
  bool WantsFrame() override { return page_ == Page::Working; }
  LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp, bool& handled) override;
  LRESULT HitTestClient(POINT clientPx) override;
  void OnDeviceLost() override { logo_.Reset(); }
  D2D1_COLOR_F ClearColor() override;

 private:
  struct Row {
    std::wstring title;
    std::wstring desc;
    float controlWidth = 40.f;
    float controlHeight = 22.f;
    std::function<void(const D2D1_RECT_F&)> control;
  };
  struct Result {
    bool ok = false;
    bool wasRunning = false;
    std::wstring error;
  };
  std::wstring CaptureKey() const;

  void EnsureLogo(ID2D1DeviceContext5* dc);
  void DrawHeader(ID2D1DeviceContext5* dc);
  float Card(float y, const std::vector<Row>& rows);
  void DrawFooter();
  void PageOptions();
  void PageWorking();
  void PageDone();
  void PageFailed();
  std::wstring Title() const;
  std::wstring Subtitle() const;
  void PickFolder();
  void StartWork();
  void ApplyResult();
  void Primary();
  void Cancel();

  Kind kind_;
  setup::Installed installed_;
  setup::InstallOptions options_;
  Page page_ = Page::Options;
  setup::Step step_ = setup::Step::Stopping;
  std::wstring error_;
  bool launch_ = true;
  bool removeData_ = false;
  int exitCode_ = 2;
  double workStart_ = 0;
  std::unique_ptr<Result> result_;
  std::thread worker_;
  ComPtr<ID2D1Bitmap1> logo_;
  std::vector<D2D1_RECT_F> hitRects_;
};

int RunSetup(const std::wstring& args);
int RunUninstall(const std::wstring& args);

}
