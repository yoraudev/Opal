#pragma once
#include <algorithm>
#include <cmath>

namespace opal {

struct PointI {
  int x = 0, y = 0;
  bool operator==(const PointI&) const = default;
};

struct RectI {
  int left = 0, top = 0, right = 0, bottom = 0;

  static RectI FromXYWH(int x, int y, int w, int h) { return {x, y, x + w, y + h}; }
  static RectI FromPoints(PointI a, PointI b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
  }
  int Width() const { return right - left; }
  int Height() const { return bottom - top; }
  bool Empty() const { return right <= left || bottom <= top; }
  bool Contains(int x, int y) const { return x >= left && x < right && y >= top && y < bottom; }
  bool Contains(PointI p) const { return Contains(p.x, p.y); }
  bool Contains(const RectI& r) const {
    return r.left >= left && r.top >= top && r.right <= right && r.bottom <= bottom;
  }
  long long Area() const { return Empty() ? 0 : 1LL * Width() * Height(); }
  RectI Intersect(const RectI& o) const {
    RectI r{std::max(left, o.left), std::max(top, o.top), std::min(right, o.right),
            std::min(bottom, o.bottom)};
    if (r.Empty()) return {};
    return r;
  }
  RectI Union(const RectI& o) const {
    if (Empty()) return o;
    if (o.Empty()) return *this;
    return {std::min(left, o.left), std::min(top, o.top), std::max(right, o.right),
            std::max(bottom, o.bottom)};
  }
  RectI Offset(int dx, int dy) const { return {left + dx, top + dy, right + dx, bottom + dy}; }
  RectI Inflate(int d) const { return {left - d, top - d, right + d, bottom + d}; }
  bool operator==(const RectI&) const = default;
};

struct CornerRadii {
  float tl = 0, tr = 0, br = 0, bl = 0;
  static CornerRadii Uniform(float r) { return {r, r, r, r}; }
  float Max() const { return std::max(std::max(tl, tr), std::max(br, bl)); }
  bool IsZero() const { return Max() <= 0.01f; }
};

}
