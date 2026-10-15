#include "editor/Annotations.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/Log.h"
#include "render/Gpu.h"
#include "render/ImageOps.h"

namespace opal {
namespace {

D2D1_RECT_F Norm(D2D1_POINT_2F a, D2D1_POINT_2F b) {
  return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
}

float SegDist(D2D1_POINT_2F p, D2D1_POINT_2F a, D2D1_POINT_2F b) {
  const float dx = b.x - a.x, dy = b.y - a.y;
  const float len2 = dx * dx + dy * dy;
  float t = len2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.f;
  t = std::clamp(t, 0.f, 1.f);
  return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

ComPtr<ID2D1StrokeStyle> RoundStyle(ID2D1DeviceContext* dc) {
  ComPtr<ID2D1Factory> f;
  dc->GetFactory(&f);
  ComPtr<ID2D1StrokeStyle> s;
  D2D1_STROKE_STYLE_PROPERTIES props{D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                     D2D1_LINE_JOIN_ROUND, 10.f, D2D1_DASH_STYLE_SOLID, 0.f};
  f->CreateStrokeStyle(props, nullptr, 0, &s);
  return s;
}

ComPtr<ID2D1PathGeometry> SmoothPath(ID2D1DeviceContext* dc, const std::vector<D2D1_POINT_2F>& pts) {
  ComPtr<ID2D1Factory> f;
  dc->GetFactory(&f);
  ComPtr<ID2D1PathGeometry> g;
  if (pts.size() < 2 || FAILED(f->CreatePathGeometry(&g))) return nullptr;
  ComPtr<ID2D1GeometrySink> sink;
  g->Open(&sink);
  sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_HOLLOW);
  for (size_t i = 1; i + 1 < pts.size(); ++i) {
    const D2D1_POINT_2F mid{(pts[i].x + pts[i + 1].x) * 0.5f, (pts[i].y + pts[i + 1].y) * 0.5f};
    sink->AddQuadraticBezier({pts[i], mid});
  }
  sink->AddLine(pts.back());
  sink->EndFigure(D2D1_FIGURE_END_OPEN);
  sink->Close();
  return g;
}

}

RectI Annotation::EffectRect() const {
  const D2D1_RECT_F r = Norm(a, b);
  return {int(std::floor(r.left)), int(std::floor(r.top)), int(std::ceil(r.right)), int(std::ceil(r.bottom))};
}

D2D1_RECT_F Annotation::Bounds(Painter* p) const {
  const float pad = width * (kind == Tool::Highlighter ? 2.f : 0.5f) + 2.f;
  switch (kind) {
    case Tool::Pen:
    case Tool::Highlighter: {
      if (points.empty()) return {};
      D2D1_RECT_F r{points[0].x, points[0].y, points[0].x, points[0].y};
      for (const auto& q : points) {
        r.left = std::min(r.left, q.x);
        r.top = std::min(r.top, q.y);
        r.right = std::max(r.right, q.x);
        r.bottom = std::max(r.bottom, q.y);
      }
      return Inset(r, -pad);
    }
    case Tool::Text: {
      D2D1_SIZE_F sz{float(text.size()) * fontSize * 0.55f, fontSize * 1.3f};
      if (p) {
        sz = p->Measure(text.empty() ? std::wstring_view(L" ") : std::wstring_view(text), fontSize,
                        FontWeight::Semibold);
      }
      return {a.x, a.y, a.x + std::max(sz.width, fontSize * 0.5f), a.y + std::max(sz.height, fontSize * 1.2f)};
    }
    default:
      return Inset(Norm(a, b), -pad);
  }
}

bool Annotation::HitTest(D2D1_POINT_2F pt, float tol, Painter* p) const {
  switch (kind) {
    case Tool::Pen:
    case Tool::Highlighter: {
      const float reach = tol + width * (kind == Tool::Highlighter ? 1.75f : 0.5f);
      if (points.size() == 1) return std::hypot(pt.x - points[0].x, pt.y - points[0].y) <= reach;
      for (size_t i = 1; i < points.size(); ++i) {
        if (SegDist(pt, points[i - 1], points[i]) <= reach) return true;
      }
      return false;
    }
    case Tool::Arrow:
      return SegDist(pt, a, b) <= tol + width;
    default:
      return InRect(Inset(Bounds(p), -tol), pt);
  }
}

void Annotation::Offset(float dx, float dy) {
  for (auto& q : points) {
    q.x += dx;
    q.y += dy;
  }
  a.x += dx;
  a.y += dy;
  b.x += dx;
  b.y += dy;
}

void DrawAnnotation(ID2D1DeviceContext5* dc, Painter& p, const Annotation& an) {
  static thread_local ComPtr<ID2D1StrokeStyle> round;
  static thread_local ID2D1DeviceContext5* roundOwner = nullptr;
  if (!round || roundOwner != dc) {
    round = RoundStyle(dc);
    roundOwner = dc;
  }
  ID2D1SolidColorBrush* brush = p.Brush(an.color);
  switch (an.kind) {
    case Tool::Pen:
      if (an.points.size() == 1) {
        dc->FillEllipse({an.points[0], an.width / 2, an.width / 2}, brush);
      } else if (auto g = SmoothPath(dc, an.points)) {
        dc->DrawGeometry(g.Get(), brush, an.width, round.Get());
      }
      break;
    case Tool::Highlighter: {
      ID2D1SolidColorBrush* hb = p.Brush(WithAlpha(an.color, 0.4f));
      if (an.points.size() == 1) {
        dc->FillEllipse({an.points[0], an.width * 1.75f, an.width * 1.75f}, hb);
      } else if (auto g = SmoothPath(dc, an.points)) {
        dc->DrawGeometry(g.Get(), hb, an.width * 3.5f, round.Get());
      }
      break;
    }
    case Tool::Arrow: {
      const float dx = an.b.x - an.a.x, dy = an.b.y - an.a.y;
      const float len = std::hypot(dx, dy);
      if (len < 1.f) break;
      const float ux = dx / len, uy = dy / len;
      const float head = std::min(len * 0.6f, std::max(14.f, an.width * 4.5f));
      const float hw = head * 0.58f;
      const D2D1_POINT_2F base{an.b.x - ux * head, an.b.y - uy * head};
      const D2D1_POINT_2F shaftEnd{an.b.x - ux * head * 0.7f, an.b.y - uy * head * 0.7f};
      dc->DrawLine(an.a, shaftEnd, brush, an.width, round.Get());
      ComPtr<ID2D1Factory> f;
      dc->GetFactory(&f);
      ComPtr<ID2D1PathGeometry> tri;
      if (SUCCEEDED(f->CreatePathGeometry(&tri))) {
        ComPtr<ID2D1GeometrySink> sink;
        tri->Open(&sink);
        sink->BeginFigure(an.b, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine({base.x - uy * hw, base.y + ux * hw});
        sink->AddLine({base.x + uy * hw, base.y - ux * hw});
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        dc->FillGeometry(tri.Get(), brush);
        dc->DrawGeometry(tri.Get(), brush, std::max(1.f, an.width * 0.35f), round.Get());
      }
      break;
    }
    case Tool::Rect: {
      const D2D1_RECT_F r = Norm(an.a, an.b);
      dc->DrawRoundedRectangle({r, an.width * 1.2f, an.width * 1.2f}, brush, an.width, round.Get());
      break;
    }
    case Tool::Ellipse: {
      const D2D1_RECT_F r = Norm(an.a, an.b);
      dc->DrawEllipse({Center(r), RW(r) / 2, RH(r) / 2}, brush, an.width, round.Get());
      break;
    }
    case Tool::Text:
      if (!an.text.empty()) {
        auto layout = p.Layout(an.text, an.fontSize, FontWeight::Semibold, 100000.f, 100000.f, HAlign::Left,
                               VAlign::Top, false);
        if (layout) dc->DrawTextLayout(an.a, layout.Get(), p.Brush(an.color), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
      }
      break;
    case Tool::Blur:
    case Tool::Pixelate:
      if (an.patch) {
        const RectI& r = an.patchRect;
        dc->DrawBitmap(an.patch.Get(), D2D1::RectF(float(r.left), float(r.top), float(r.right), float(r.bottom)), 1.f,
                       an.kind == Tool::Pixelate ? D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                                                 : D2D1_INTERPOLATION_MODE_LINEAR);
      }
      break;
    default:
      break;
  }
}

void UpdateEffectPatch(ID2D1DeviceContext5* dc, Annotation& a, const Image& base, float captureScale) {
  if (a.kind != Tool::Blur && a.kind != Tool::Pixelate) return;
  const RectI r = a.EffectRect().Intersect(base.Bounds());
  if (r.Empty()) {
    a.patch.Reset();
    a.patchRect = {};
    return;
  }
  if (a.patch && r == a.patchRect) return;
  Image img = base.Crop(r.Inflate(0));
  if (a.kind == Tool::Blur) {
    BlurRegion(img, img.Bounds(), std::max(12.f, 22.f * captureScale));
  } else {
    PixelateRegion(img, img.Bounds(), std::max(6, int(std::lround(10.f * captureScale))));
  }
  D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  a.patch.Reset();
  dc->CreateBitmap(D2D1::SizeU(UINT(img.width), UINT(img.height)), img.pixels.data(), UINT(img.width * 4), &props,
                   &a.patch);
  a.patchRect = r;
}

bool RenderDocument(const Image& base, const DocState& doc, Image* out) {
  Gpu& gpu = Gpu::Get();
  ComPtr<ID2D1DeviceContext5> dc = gpu.CreateContext();
  if (!dc) return false;
  const RectI crop = doc.crop.Empty() ? base.Bounds() : doc.crop.Intersect(base.Bounds());
  if (crop.Empty()) return false;
  const D2D1_SIZE_U size = D2D1::SizeU(UINT(crop.Width()), UINT(crop.Height()));
  const D2D1_PIXEL_FORMAT fmt = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
  ComPtr<ID2D1Bitmap1> target, source, readback;
  D2D1_BITMAP_PROPERTIES1 tp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, fmt);
  D2D1_BITMAP_PROPERTIES1 sp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, fmt);
  D2D1_BITMAP_PROPERTIES1 rp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, fmt);
  if (FAILED(dc->CreateBitmap(size, nullptr, 0, &tp, &target))) return false;
  if (FAILED(dc->CreateBitmap(D2D1::SizeU(UINT(base.width), UINT(base.height)), base.pixels.data(),
                              UINT(base.width * 4), &sp, &source))) {
    return false;
  }
  if (FAILED(dc->CreateBitmap(size, nullptr, 0, &rp, &readback))) return false;
  Painter p;
  p.Begin(dc.Get(), gpu.DWrite());
  dc->SetTarget(target.Get());
  dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  dc->BeginDraw();
  dc->Clear({0, 0, 0, 0});
  dc->SetTransform(D2D1::Matrix3x2F::Translation(float(-crop.left), float(-crop.top)));
  dc->DrawBitmap(source.Get(), D2D1::RectF(0, 0, float(base.width), float(base.height)), 1.f,
                 D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
  for (const auto& a : doc.items) DrawAnnotation(dc.Get(), p, a);
  if (FAILED(dc->EndDraw())) return false;
  dc->SetTarget(nullptr);
  if (FAILED(readback->CopyFromBitmap(nullptr, target.Get(), nullptr))) return false;
  D2D1_MAPPED_RECT mapped{};
  if (FAILED(readback->Map(D2D1_MAP_OPTIONS_READ, &mapped))) return false;
  *out = Image(crop.Width(), crop.Height());
  for (int y = 0; y < out->height; ++y) std::memcpy(out->Row(y), mapped.bits + size_t(y) * mapped.pitch, size_t(out->width) * 4);
  readback->Unmap();
  return true;
}

}
