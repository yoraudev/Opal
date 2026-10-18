#pragma once
#include <functional>
#include <string>
#include <vector>

#include "render/D2DWindow.h"

namespace opal {

class SettingsWindow : public D2DWindow {
 public:
  bool Init();
  void Activate();

 protected:
  void OnRender(ID2D1DeviceContext5* dc) override;
  LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp, bool& handled) override;
  LRESULT HitTestClient(POINT clientPx) override;
  void OnDeviceLost() override { logo_.Reset(); }
  void OnDestroyed() override;
  D2D1_COLOR_F ClearColor() override;

 private:
  enum class Page { General, Shortcuts, Capture, Output, About };
  struct Row {
    std::wstring title;
    std::wstring desc;
    float controlWidth = 40.f;
    float controlHeight = 22.f;
    std::function<void(const D2D1_RECT_F&)> control;
  };

  void DrawSidebar();
  void DrawContent(ID2D1DeviceContext5* dc);
  float Section(float y, const wchar_t* title);
  float Card(float y, std::vector<Row> rows);
  void PageGeneral(float& y);
  void PageShortcuts(float& y);
  void PageCapture(float& y);
  void PageOutput(float& y);
  void PageAbout(ID2D1DeviceContext5* dc, float& y);
  void UpdateRow(float y);
  void Changed();
  void StartRecording(int action);
  void StopRecording();
  void PickFolder();

  Page page_ = Page::General;
  float scroll_ = 0.f;
  float contentHeight_ = 0.f;
  D2D1_RECT_F contentRect_{};
  float contentLeft_ = 0, contentWidth_ = 0;
  int recording_ = -1;
  std::wstring conflict_;
  bool snipKeyOff_ = false;
  ComPtr<ID2D1Bitmap1> logo_;
  std::vector<D2D1_RECT_F> hitRects_;
};

}
