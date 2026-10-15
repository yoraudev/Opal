#include "ui/Icons.h"

#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace opal {
namespace {

using Microsoft::WRL::ComPtr;

struct Pen {
  ID2D1DeviceContext* dc;
  ID2D1Brush* brush;
  ID2D1StrokeStyle* style;
  D2D1_RECT_F box;
  float s;
  float sw;

  D2D1_POINT_2F P(float x, float y) const { return {box.left + x * s, box.top + y * s}; }

  void Line(float x0, float y0, float x1, float y1) const {
    dc->DrawLine(P(x0, y0), P(x1, y1), brush, sw, style);
  }
  void Poly(std::initializer_list<D2D1_POINT_2F> pts, bool closed, bool fill = false) const {
    ComPtr<ID2D1Factory> f;
    dc->GetFactory(&f);
    ComPtr<ID2D1PathGeometry> g;
    if (FAILED(f->CreatePathGeometry(&g))) return;
    ComPtr<ID2D1GeometrySink> sink;
    g->Open(&sink);
    bool first = true;
    for (const auto& p : pts) {
      if (first) {
        sink->BeginFigure(P(p.x, p.y), fill ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
        first = false;
      } else {
        sink->AddLine(P(p.x, p.y));
      }
    }
    sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    sink->Close();
    if (fill) dc->FillGeometry(g.Get(), brush);
    else dc->DrawGeometry(g.Get(), brush, sw, style);
  }
  void RRect(float l, float t, float r, float b, float rad, bool fill = false) const {
    D2D1_ROUNDED_RECT rr{{box.left + l * s, box.top + t * s, box.left + r * s, box.top + b * s},
                         rad * s, rad * s};
    if (fill) dc->FillRoundedRectangle(rr, brush);
    else dc->DrawRoundedRectangle(rr, brush, sw, style);
  }
  void Circle(float cx, float cy, float r, bool fill = false) const {
    D2D1_ELLIPSE e{P(cx, cy), r * s, r * s};
    if (fill) dc->FillEllipse(e, brush);
    else dc->DrawEllipse(e, brush, sw, style);
  }
  void Ellipse(float cx, float cy, float rx, float ry) const {
    dc->DrawEllipse({P(cx, cy), rx * s, ry * s}, brush, sw, style);
  }
  void ArcPath(D2D1_POINT_2F a, D2D1_POINT_2F arcStart, D2D1_POINT_2F arcEnd, float radius,
               D2D1_SWEEP_DIRECTION dir, D2D1_POINT_2F end) const {
    ComPtr<ID2D1Factory> f;
    dc->GetFactory(&f);
    ComPtr<ID2D1PathGeometry> g;
    if (FAILED(f->CreatePathGeometry(&g))) return;
    ComPtr<ID2D1GeometrySink> sink;
    g->Open(&sink);
    sink->BeginFigure(P(a.x, a.y), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine(P(arcStart.x, arcStart.y));
    sink->AddArc({P(arcEnd.x, arcEnd.y), {radius * s, radius * s}, 0.f, dir, D2D1_ARC_SIZE_SMALL});
    sink->AddLine(P(end.x, end.y));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    dc->DrawGeometry(g.Get(), brush, sw, style);
  }
};

ID2D1StrokeStyle* RoundStroke(ID2D1DeviceContext* dc) {
  static ComPtr<ID2D1StrokeStyle> style;
  static ID2D1Factory* owner = nullptr;
  ComPtr<ID2D1Factory> f;
  dc->GetFactory(&f);
  if (!style || owner != f.Get()) {
    style.Reset();
    D2D1_STROKE_STYLE_PROPERTIES props{D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                       D2D1_LINE_JOIN_ROUND, 10.f, D2D1_DASH_STYLE_SOLID, 0.f};
    f->CreateStrokeStyle(props, nullptr, 0, &style);
    owner = f.Get();
  }
  return style.Get();
}

}

void DrawIcon(ID2D1DeviceContext* dc, ID2D1Brush* brush, Icon icon, const D2D1_RECT_F& box,
              float strokeWidth) {
  const float size = std::min(box.right - box.left, box.bottom - box.top);
  D2D1_RECT_F sq{box.left + (box.right - box.left - size) / 2, box.top + (box.bottom - box.top - size) / 2,
                 0, 0};
  sq.right = sq.left + size;
  sq.bottom = sq.top + size;
  Pen p{dc, brush, RoundStroke(dc), sq, size / 20.f, strokeWidth};
  auto pt = [](float x, float y) { return D2D1_POINT_2F{x, y}; };

  switch (icon) {
    case Icon::Screen:
      p.RRect(2, 3, 18, 14, 2);
      p.Line(10, 14, 10, 17);
      p.Line(6.5f, 17, 13.5f, 17);
      break;
    case Icon::Window:
      p.RRect(2, 3.5f, 18, 16.5f, 2.5f);
      p.Line(2, 7.5f, 18, 7.5f);
      p.Circle(4.6f, 5.5f, 0.75f, true);
      p.Circle(6.8f, 5.5f, 0.75f, true);
      p.Circle(9.0f, 5.5f, 0.75f, true);
      break;
    case Icon::Region:
      p.Poly({pt(2.5f, 7), pt(2.5f, 3.5f), pt(6, 3.5f)}, false);
      p.Poly({pt(14, 3.5f), pt(17.5f, 3.5f), pt(17.5f, 7)}, false);
      p.Poly({pt(17.5f, 13), pt(17.5f, 16.5f), pt(14, 16.5f)}, false);
      p.Poly({pt(6, 16.5f), pt(2.5f, 16.5f), pt(2.5f, 13)}, false);
      p.Line(9, 10, 11, 10);
      p.Line(10, 9, 10, 11);
      break;
    case Icon::Element:
      p.RRect(2, 3, 15, 12.5f, 2.5f);
      p.Poly({pt(10.5f, 9.5f), pt(10.5f, 18.5f), pt(12.7f, 16.3f), pt(14.2f, 19.2f), pt(15.6f, 18.5f),
              pt(14.1f, 15.6f), pt(17.2f, 15.6f)},
             true, true);
      break;
    case Icon::ChevronDown:
      p.Poly({pt(6, 8), pt(10, 12), pt(14, 8)}, false);
      break;
    case Icon::ChevronRight:
      p.Poly({pt(8, 6), pt(12, 10), pt(8, 14)}, false);
      break;
    case Icon::Close:
      p.Line(6, 6, 14, 14);
      p.Line(14, 6, 6, 14);
      break;
    case Icon::Timer:
      p.Circle(10, 11, 6.5f);
      p.Line(10, 11, 10, 7.5f);
      p.Line(8, 2.5f, 12, 2.5f);
      p.Line(15.2f, 5.3f, 16.4f, 4.1f);
      break;
    case Icon::Pointer:
    case Icon::Select:
      p.Poly({pt(6, 3), pt(6, 16), pt(9.2f, 12.9f), pt(11.4f, 17.6f), pt(13.3f, 16.7f), pt(11.1f, 12.1f),
              pt(15.5f, 12.1f)},
             true);
      break;
    case Icon::Folder:
      p.Poly({pt(2.5f, 15.5f), pt(2.5f, 4.5f), pt(7.5f, 4.5f), pt(9.2f, 6.5f), pt(17.5f, 6.5f),
              pt(17.5f, 15.5f)},
             true);
      p.Line(2.5f, 9, 17.5f, 9);
      break;
    case Icon::Clipboard:
      p.RRect(4.5f, 4, 15.5f, 17.5f, 2);
      p.RRect(7.5f, 2.5f, 12.5f, 5.5f, 1, false);
      break;
    case Icon::Pen:
      p.Poly({pt(4, 16), pt(4.7f, 12.8f), pt(13.4f, 4.1f), pt(15.9f, 6.6f), pt(7.2f, 15.3f)}, true);
      p.Line(11.6f, 5.9f, 14.1f, 8.4f);
      break;
    case Icon::Highlighter:
      p.Poly({pt(6.2f, 14.6f), pt(5.4f, 11.2f), pt(12.6f, 4), pt(16, 7.4f), pt(8.8f, 14.6f)}, true);
      p.Poly({pt(5.4f, 11.2f), pt(3, 16.8f), pt(6.2f, 14.6f)}, false);
      p.Line(9.5f, 17, 17, 17);
      break;
    case Icon::Arrow:
      p.Line(4.5f, 15.5f, 15, 5);
      p.Poly({pt(8.5f, 5), pt(15, 5), pt(15, 11.5f)}, false);
      break;
    case Icon::Rect:
      p.RRect(3, 5, 17, 15, 1.5f);
      break;
    case Icon::Ellipse:
      p.Ellipse(10, 10, 7, 5.5f);
      break;
    case Icon::Text:
      p.Poly({pt(4.5f, 16), pt(10, 4), pt(15.5f, 16)}, false);
      p.Line(6.6f, 12, 13.4f, 12);
      break;
    case Icon::Blur:
      for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
          const float r = (x == 1 && y == 1) ? 2.1f : ((x == 1 || y == 1) ? 1.5f : 1.0f);
          p.Circle(4.5f + x * 5.5f, 4.5f + y * 5.5f, r, true);
        }
      }
      break;
    case Icon::Pixelate:
      p.RRect(3, 3, 17, 17, 2);
      for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
          if ((x + y) % 2 == 0) {
            const float l = 3 + x * 14.f / 3, t = 3 + y * 14.f / 3;
            p.RRect(l + 0.6f, t + 0.6f, l + 14.f / 3 - 0.6f, t + 14.f / 3 - 0.6f, 0.6f, true);
          }
        }
      }
      break;
    case Icon::Crop:
      p.Poly({pt(6, 2.5f), pt(6, 14), pt(17.5f, 14)}, false);
      p.Poly({pt(2.5f, 6), pt(14, 6), pt(14, 17.5f)}, false);
      break;
    case Icon::Undo:
      p.ArcPath(pt(4, 8), pt(12, 8), pt(12, 16), 4, D2D1_SWEEP_DIRECTION_CLOCKWISE, pt(8, 16));
      p.Poly({pt(7.5f, 4.5f), pt(4, 8), pt(7.5f, 11.5f)}, false);
      break;
    case Icon::Redo:
      p.ArcPath(pt(16, 8), pt(8, 8), pt(8, 16), 4, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, pt(12, 16));
      p.Poly({pt(12.5f, 4.5f), pt(16, 8), pt(12.5f, 11.5f)}, false);
      break;
    case Icon::Copy:
      p.RRect(7.5f, 7.5f, 16.5f, 16.5f, 2);
      p.Poly({pt(4.5f, 12.5f), pt(4.5f, 4.5f), pt(12.5f, 4.5f)}, false);
      break;
    case Icon::Download:
      p.Line(10, 3, 10, 12.5f);
      p.Poly({pt(6.5f, 9), pt(10, 12.5f), pt(13.5f, 9)}, false);
      p.Poly({pt(3.5f, 12), pt(3.5f, 16.5f), pt(16.5f, 16.5f), pt(16.5f, 12)}, false);
      break;
    case Icon::Trash:
      p.Line(3.5f, 5.5f, 16.5f, 5.5f);
      p.Poly({pt(8, 5.5f), pt(8, 3.5f), pt(12, 3.5f), pt(12, 5.5f)}, false);
      p.Poly({pt(5, 5.5f), pt(6, 17), pt(14, 17), pt(15, 5.5f)}, false);
      p.Line(8.5f, 8.5f, 8.7f, 14);
      p.Line(11.5f, 8.5f, 11.3f, 14);
      break;
    case Icon::Check:
      p.Poly({pt(5, 10.5f), pt(8.5f, 14), pt(15, 6.5f)}, false);
      break;
    case Icon::Gear: {
      p.Circle(10, 10, 2.6f);
      p.Circle(10, 10, 6);
      for (int i = 0; i < 8; ++i) {
        const float a = float(i) * 3.14159265f / 4.f;
        p.Line(10 + std::cos(a) * 6, 10 + std::sin(a) * 6, 10 + std::cos(a) * 8.2f, 10 + std::sin(a) * 8.2f);
      }
      break;
    }
    case Icon::Keyboard:
      p.RRect(2, 5, 18, 15, 2);
      for (int i = 0; i < 5; ++i) p.Circle(5 + i * 2.5f, 8.3f, 0.7f, true);
      p.Line(6.5f, 11.8f, 13.5f, 11.8f);
      break;
    case Icon::Camera:
      p.Poly({pt(2.5f, 16), pt(2.5f, 7), pt(6.5f, 7), pt(8, 4.5f), pt(12, 4.5f), pt(13.5f, 7), pt(17.5f, 7),
              pt(17.5f, 16)},
             true);
      p.Circle(10, 11.3f, 3);
      break;
    case Icon::Info:
      p.Circle(10, 10, 7.5f);
      p.Circle(10, 6.6f, 0.9f, true);
      p.Line(10, 9.3f, 10, 14);
      break;
    case Icon::Sparkle:
      p.Poly({pt(10, 2.5f), pt(11.6f, 8.4f), pt(17.5f, 10), pt(11.6f, 11.6f), pt(10, 17.5f), pt(8.4f, 11.6f),
              pt(2.5f, 10), pt(8.4f, 8.4f)},
             true);
      break;
    case Icon::Plus:
      p.Line(10, 4.5f, 10, 15.5f);
      p.Line(4.5f, 10, 15.5f, 10);
      break;
    case Icon::Minus:
      p.Line(4.5f, 10, 15.5f, 10);
      break;
    case Icon::Shadow:
      p.RRect(3, 3, 14, 14, 2.5f);
      p.Poly({pt(16.5f, 6.5f), pt(16.5f, 15), pt(15, 16.5f), pt(6.5f, 16.5f)}, false);
      break;
    case Icon::Photo:
      p.RRect(2.5f, 4, 17.5f, 16, 2);
      p.Poly({pt(2.5f, 14), pt(7, 9.5f), pt(10.5f, 13), pt(12.5f, 11), pt(17.5f, 15.5f)}, false);
      p.Circle(13, 7.5f, 1.3f);
      break;
  }
}

}
