#include "capture/CornerRadius.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "capture/EdgeSnap.h"

namespace opal {
namespace {

constexpr float kDiagToRadius = 1.f / (1.f - 0.70710678f);

struct Rgb3 {
  float r, g, b;
};

Rgb3 ToRgb(uint32_t p) { return {float(ChR(p)), float(ChG(p)), float(ChB(p))}; }

uint32_t MedianColor(std::vector<uint32_t>& v) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end(), [](uint32_t a, uint32_t b) {
    return ChR(a) + 2 * ChG(a) + ChB(a) < ChR(b) + 2 * ChG(b) + ChB(b);
  });
  return v[v.size() / 2];
}

float Coverage(uint32_t p, const Rgb3& in, const Rgb3& out) {
  Rgb3 c = ToRgb(p);
  const float dx = in.r - out.r, dy = in.g - out.g, dz = in.b - out.b;
  const float len2 = dx * dx + dy * dy + dz * dz;
  if (len2 < 1.f) return 1.f;
  const float t = ((c.r - out.r) * dx + (c.g - out.g) * dy + (c.b - out.b) * dz) / len2;
  return std::clamp(t, 0.f, 1.f);
}

float DiagonalCrossing(int j, float c) {
  const float u = c >= 0.5f ? std::sqrt(2.f * (1.f - c)) : 2.f - std::sqrt(2.f * c);
  return float(j) + u * 0.5f;
}

int insideMinDistFor(uint32_t in, uint32_t out) { return std::min(12, ColorDistance(in, out) / 2); }

struct Corner {
  int x, y;
  int dx, dy;
};

}

CornerEstimate EstimateCornerRadii(const Image& img, const RectI& rect, int maxRadius) {
  CornerEstimate est;
  RectI r = rect.Intersect(img.Bounds());
  if (r.Width() < 8 || r.Height() < 8) return est;
  maxRadius = std::min(maxRadius, std::min(r.Width(), r.Height()) / 2);

  const std::array<Corner, 4> corners = {{
      {r.left, r.top, 1, 1},
      {r.right - 1, r.top, -1, 1},
      {r.right - 1, r.bottom - 1, -1, -1},
      {r.left, r.bottom - 1, 1, -1},
  }};
  float* outR[4] = {&est.radii.tl, &est.radii.tr, &est.radii.br, &est.radii.bl};

  for (int ci = 0; ci < 4; ++ci) {
    const Corner& c = corners[size_t(ci)];
    std::vector<uint32_t> inside, outside;
    const int along0 = std::min(maxRadius + 2, std::min(r.Width(), r.Height()) / 2);
    for (int k = along0; k < along0 + 10; ++k) {
      const int hx = c.x + c.dx * k;
      if (hx >= r.left && hx < r.right) {
        inside.push_back(img.At(hx, c.y));
        const int oy = c.y - c.dy;
        if (oy >= 0 && oy < img.height) outside.push_back(img.At(hx, oy));
      }
      const int vy = c.y + c.dy * k;
      if (vy >= r.top && vy < r.bottom) {
        inside.push_back(img.At(c.x, vy));
        const int ox = c.x - c.dx;
        if (ox >= 0 && ox < img.width) outside.push_back(img.At(ox, vy));
      }
    }
    if (inside.size() < 6 || outside.size() < 6) continue;
    const uint32_t inC = MedianColor(inside), outC = MedianColor(outside);
    if (ColorDistance(inC, outC) < 12) continue;
    const Rgb3 in = ToRgb(inC), out = ToRgb(outC);

    const int kMax = int(std::ceil(maxRadius / kDiagToRadius)) + 2;
    int first = -1;
    float covFirst = 0.f, covPrev = 0.f;
    for (int k = 0; k <= kMax; ++k) {
      const int x = c.x + c.dx * k, y = c.y + c.dy * k;
      if (!r.Contains(x, y)) break;
      const float cov = Coverage(img.At(x, y), in, out);
      if (cov >= 0.5f) {
        first = k;
        covFirst = cov;
        break;
      }
      if (k > 0 && cov < covPrev - 0.05f && covPrev > 0.2f &&
          ColorDistance(img.At(x, y), outC) >= insideMinDistFor(inC, outC)) {
        first = k - 1;
        covFirst = covPrev;
        covPrev = 0.f;
        break;
      }
      covPrev = cov;
    }
    if (first < 0) continue;

    float t;
    if (first > 0 && std::fabs(covPrev - 0.5f) < std::fabs(covFirst - 0.5f)) {
      t = DiagonalCrossing(first - 1, covPrev);
    } else {
      t = DiagonalCrossing(first, covFirst);
    }
    float radius = t * kDiagToRadius;
    if (radius < 2.0f) radius = 0.f;
    radius = std::min(radius, float(maxRadius));

    const int insideMinDist = insideMinDistFor(inC, outC);
    bool ok = true;
    if (radius >= 4.f) {
      for (float deg : {22.5f, 67.5f}) {
        const float a = deg * 3.14159265f / 180.f;
        const float nx = std::cos(a), ny = std::sin(a);
        for (float off : {-2.0f, 2.0f}) {
          const float lx = radius - (radius + off) * nx;
          const float ly = radius - (radius + off) * ny;
          const int px = c.x + c.dx * int(std::floor(lx));
          const int py = c.y + c.dy * int(std::floor(ly));
          if (!img.Bounds().Contains(px, py)) continue;
          const uint32_t p = img.At(px, py);
          if (off > 0 && Coverage(p, in, out) > 0.6f) ok = false;
          if (off < 0 && ColorDistance(p, outC) < insideMinDist) ok = false;
        }
      }
    }
    if (!ok) continue;
    *outR[ci] = radius;
    est.trusted[ci] = true;
  }

  std::vector<float> vals;
  for (int i = 0; i < 4; ++i) {
    if (est.trusted[i] && *outR[i] > 0) vals.push_back(*outR[i]);
  }
  if (vals.size() >= 2) {
    std::sort(vals.begin(), vals.end());
    const float med = vals[vals.size() / 2];
    for (int i = 0; i < 4; ++i) {
      if (est.trusted[i] && std::fabs(*outR[i] - med) <= std::max(1.5f, med * 0.15f)) *outR[i] = med;
    }
  }
  return est;
}

}
