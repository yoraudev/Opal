#pragma once
#include <string>

#include "core/Image.h"
#include "render/D2DWindow.h"

namespace opal {

class ThumbnailWindow : public D2DWindow {
 public:
  ThumbnailWindow(Image image, const RectI& workArea, UINT dpi, float captureScale);
  bool Init();
  void SetPath(const std::wstring& path);
  const std::wstring& Path() const { return path_; }
  void Dismiss(bool animated = true);

 protected:
  void OnRender(ID2D1DeviceContext5* dc) override;
  bool WantsFrame() override;
  LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp, bool& handled) override;
  void OnDeviceLost() override { bitmap_.Reset(); }

 private:
  void ShowMenu();
  void StartFileDrag();
  std::wstring EnsureFileForDrag();
  void OpenEditor();
  void SaveAs();
  void DeleteFile();

  Image image_;
  Image preview_;
  std::wstring path_;
  std::wstring tempPath_;
  RectI work_;
  UINT dpi_;
  float captureScale_;
  float thumbW_ = 0, thumbH_ = 0;
  ComPtr<ID2D1Bitmap1> bitmap_;
  double shownAt_ = 0;
  double hoverStart_ = 0;
  bool hovered_ = false;
  bool dismissing_ = false;
  double dismissStart_ = 0;
  bool pressed_ = false;
  POINT pressPt_{};
  float swipe_ = 0;
  bool swiping_ = false;
};

}
