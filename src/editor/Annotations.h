#pragma once
#include <d2d1_3.h>
#include <wrl/client.h>

#include <string>
#include <vector>

#include "core/Image.h"
#include "ui/Ui.h"

namespace opal {

enum class Tool { Select, Pen, Highlighter, Arrow, Rect, Ellipse, Text, Blur, Pixelate, Crop };

struct Annotation {
  Tool kind = Tool::Pen;
  D2D1_COLOR_F color{1, 0, 0, 1};
  float width = 4.f;
  std::vector<D2D1_POINT_2F> points;
  D2D1_POINT_2F a{}, b{};
  std::wstring text;
  float fontSize = 24.f;
  ComPtr<ID2D1Bitmap1> patch;
  RectI patchRect;

  D2D1_RECT_F Bounds(Painter* p = nullptr) const;
  bool HitTest(D2D1_POINT_2F pt, float tolerance, Painter* p) const;
  void Offset(float dx, float dy);
  RectI EffectRect() const;
};

struct DocState {
  std::vector<Annotation> items;
  RectI crop;
};

void DrawAnnotation(ID2D1DeviceContext5* dc, Painter& p, const Annotation& a);
void UpdateEffectPatch(ID2D1DeviceContext5* dc, Annotation& a, const Image& base, float captureScale);
bool RenderDocument(const Image& base, const DocState& doc, Image* out);

}
