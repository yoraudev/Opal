#pragma once
#include <string>
#include <vector>

#include "core/Image.h"
#include "editor/Annotations.h"
#include "render/D2DWindow.h"

namespace opal {

class EditorWindow : public D2DWindow {
 public:
  EditorWindow(Image image, std::wstring path, float captureScale);
  bool Init(const RectI& work);

 protected:
  void OnRender(ID2D1DeviceContext5* dc) override;
  LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp, bool& handled) override;
  LRESULT HitTestClient(POINT clientPx) override;
  HCURSOR Cursor() override;
  void OnDeviceLost() override;
  D2D1_COLOR_F ClearColor() override;

 private:
  void DrawToolbar(ID2D1DeviceContext5* dc);
  void DrawCanvas(ID2D1DeviceContext5* dc);
  void HandleCanvasInput(ID2D1DeviceContext5* dc);
  void HandleKeys();
  void SetTool(Tool t);
  void PushUndo();
  void Undo();
  void Redo();
  void CommitText();
  void CommitCrop();
  bool Flatten(Image* out);
  void Finish(bool save);
  void CopyToClipboard();
  void SaveAs();
  void RequestClose();
  D2D1_POINT_2F ToImage(D2D1_POINT_2F dip) const;
  float StrokeWidth() const;
  float FontSize() const;
  D2D1_COLOR_F CurrentColor() const;

  Image image_;
  std::wstring path_;
  float captureScale_;
  DocState doc_;
  std::vector<DocState> undo_, redo_;
  Tool tool_ = Tool::Arrow;
  int color_ = 0;
  int width_ = 1;
  bool dirty_ = false;

  ComPtr<ID2D1Bitmap1> base_;
  float zoom_ = 1.f;
  D2D1_POINT_2F origin_{};
  D2D1_RECT_F canvasDip_{};

  int drawing_ = -1;
  int selected_ = -1;
  int editingText_ = -1;
  bool moving_ = false;
  bool moveUndoPushed_ = false;
  D2D1_POINT_2F lastPt_{};
  RectI cropDraft_;
  int cropHandle_ = -1;
  RectI cropStart_;
  D2D1_POINT_2F cropAnchor_{};
  bool colorOpen_ = false;
  D2D1_RECT_F colorAnchor_{};
  std::vector<D2D1_RECT_F> hitRects_;
};

}
