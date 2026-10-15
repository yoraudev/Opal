#include "render/ImageOps.h"

#include <algorithm>
#include <cmath>

namespace opal {
namespace {

inline float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

inline uint32_t ScalePremul(uint32_t p, float k) {
  if (k >= 1.f) return p;
  if (k <= 0.f) return 0;
  auto s = [k](uint32_t c) { return uint8_t(std::lround(float(c) * k)); };
  return PackBGRA(s(ChB(p)), s(ChG(p)), s(ChR(p)), s(ChA(p)));
}

inline uint32_t Over(uint32_t src, uint32_t dst) {
  uint32_t sa = ChA(src);
  if (sa == 255) return src;
  if (sa == 0) return dst;
  uint32_t inv = 255 - sa;
  auto mix = [inv](uint32_t s, uint32_t d) { return uint8_t(std::min<uint32_t>(255, s + (d * inv + 127) / 255)); };
  return PackBGRA(mix(ChB(src), ChB(dst)), mix(ChG(src), ChG(dst)), mix(ChR(src), ChR(dst)),
                  mix(sa, ChA(dst)));
}

int BoxRadiusForSigma(float sigma) {
  float w = std::sqrt(4.f * sigma * sigma + 1.f);
  return std::max(1, int(std::lround((w - 1.f) * 0.5f)));
}

void BoxBlurLine(const uint8_t* in, uint8_t* out, int n, int stride, int r, bool clampEdges) {
  const int win = 2 * r + 1;
  auto sample = [&](int i) -> int {
    if (i < 0) return clampEdges ? in[0] : 0;
    if (i >= n) return clampEdges ? in[size_t(n - 1) * stride] : 0;
    return in[size_t(i) * stride];
  };
  int sum = 0;
  for (int i = -r; i <= r; ++i) sum += sample(i);
  for (int i = 0; i < n; ++i) {
    out[size_t(i) * stride] = uint8_t((sum + win / 2) / win);
    sum += sample(i + r + 1) - sample(i - r);
  }
}

void BoxBlurImpl(std::vector<uint8_t>& data, int w, int h, int radius, int passes, bool clampEdges) {
  if (radius <= 0 || w <= 0 || h <= 0) return;
  std::vector<uint8_t> tmp(data.size());
  for (int p = 0; p < passes; ++p) {
    for (int y = 0; y < h; ++y) {
      BoxBlurLine(data.data() + size_t(y) * w, tmp.data() + size_t(y) * w, w, 1, radius, clampEdges);
    }
    for (int x = 0; x < w; ++x) {
      BoxBlurLine(tmp.data() + x, data.data() + x, h, w, radius, clampEdges);
    }
  }
}

}

float RoundedRectCoverage(int px, int py, int w, int h, const CornerRadii& r) {
  const float x = px + 0.5f, y = py + 0.5f;
  float cx, cy, rad;
  if (x < r.tl && y < r.tl) {
    cx = r.tl; cy = r.tl; rad = r.tl;
  } else if (x > w - r.tr && y < r.tr) {
    cx = w - r.tr; cy = r.tr; rad = r.tr;
  } else if (x > w - r.br && y > h - r.br) {
    cx = w - r.br; cy = h - r.br; rad = r.br;
  } else if (x < r.bl && y > h - r.bl) {
    cx = r.bl; cy = h - r.bl; rad = r.bl;
  } else {
    return 1.f;
  }
  const float d = std::hypot(x - cx, y - cy);
  return Clamp01(rad - d + 0.5f);
}

void ApplyRoundedMask(Image& img, const CornerRadii& radii) {
  if (radii.IsZero() || img.Empty()) return;
  const int w = img.width, h = img.height;
  CornerRadii r = radii;
  const float maxR = std::min(w, h) * 0.5f;
  r.tl = std::min(r.tl, maxR); r.tr = std::min(r.tr, maxR);
  r.br = std::min(r.br, maxR); r.bl = std::min(r.bl, maxR);
  auto doCorner = [&](int x0, int y0, int size) {
    for (int y = std::max(0, y0); y < std::min(h, y0 + size); ++y) {
      for (int x = std::max(0, x0); x < std::min(w, x0 + size); ++x) {
        img.At(x, y) = ScalePremul(img.At(x, y), RoundedRectCoverage(x, y, w, h, r));
      }
    }
  };
  doCorner(0, 0, int(std::ceil(r.tl)) + 1);
  doCorner(w - int(std::ceil(r.tr)) - 1, 0, int(std::ceil(r.tr)) + 1);
  doCorner(w - int(std::ceil(r.br)) - 1, h - int(std::ceil(r.br)) - 1, int(std::ceil(r.br)) + 1);
  doCorner(0, h - int(std::ceil(r.bl)) - 1, int(std::ceil(r.bl)) + 1);
}

ShadowStyle ShadowStyle::MacWindow(float scale) {
  ShadowStyle s;
  s.layers.push_back({30.f * scale, 0.f, 18.f * scale, 0.42f});
  s.layers.push_back({3.f * scale, 0.f, 1.f * scale, 0.22f});
  return s;
}

Image AddDropShadow(const Image& img, const ShadowStyle& style, PointI* outOrigin) {
  if (img.Empty()) return img;
  int pad = style.padding;
  if (pad < 0) {
    float p = 0;
    for (const auto& l : style.layers) {
      p = std::max(p, l.blur * 1.6f + std::max(std::fabs(l.offsetX), std::fabs(l.offsetY)));
    }
    pad = int(std::ceil(p));
  }
  const int W = img.width + 2 * pad, H = img.height + 2 * pad;
  std::vector<uint8_t> total(size_t(W) * H, 0);
  std::vector<uint8_t> layer(size_t(W) * H);
  for (const auto& l : style.layers) {
    std::fill(layer.begin(), layer.end(), uint8_t(0));
    const int ox = pad + int(std::lround(l.offsetX)), oy = pad + int(std::lround(l.offsetY));
    for (int y = 0; y < img.height; ++y) {
      const int ty = y + oy;
      if (ty < 0 || ty >= H) continue;
      const uint32_t* src = img.Row(y);
      for (int x = 0; x < img.width; ++x) {
        const int tx = x + ox;
        if (tx >= 0 && tx < W) layer[size_t(ty) * W + tx] = ChA(src[x]);
      }
    }
    BoxBlurImpl(layer, W, H, BoxRadiusForSigma(l.blur * 0.5f), 3, false);
    const float op = Clamp01(l.opacity);
    for (size_t i = 0; i < total.size(); ++i) {
      const uint32_t a = uint32_t(std::lround(layer[i] * op));
      total[i] = uint8_t(total[i] + (a * (255u - total[i]) + 127) / 255);
    }
  }
  Image out(W, H);
  for (size_t i = 0; i < total.size(); ++i) out.pixels[i] = PackBGRA(0, 0, 0, total[i]);
  for (int y = 0; y < img.height; ++y) {
    uint32_t* dst = out.Row(y + pad) + pad;
    const uint32_t* src = img.Row(y);
    for (int x = 0; x < img.width; ++x) dst[x] = Over(src[x], dst[x]);
  }
  if (outOrigin) *outOrigin = {pad, pad};
  return out;
}

void ApplyHairline(Image& img, const CornerRadii& radii, uint32_t premulColor) {
  if (img.width < 3 || img.height < 3) return;
  const int w = img.width, h = img.height;
  CornerRadii inner{std::max(0.f, radii.tl - 1), std::max(0.f, radii.tr - 1),
                    std::max(0.f, radii.br - 1), std::max(0.f, radii.bl - 1)};
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const bool nearEdge = x < radii.Max() + 2 || y < radii.Max() + 2 || x >= w - radii.Max() - 2 ||
                            y >= h - radii.Max() - 2;
      if (!nearEdge) continue;
      const float outer = RoundedRectCoverage(x, y, w, h, radii);
      const float in = (x == 0 || y == 0 || x == w - 1 || y == h - 1)
                           ? 0.f
                           : RoundedRectCoverage(x - 1, y - 1, w - 2, h - 2, inner);
      const float ring = Clamp01(outer - in);
      if (ring <= 0.f) continue;
      img.At(x, y) = Over(ScalePremul(premulColor, ring), img.At(x, y));
    }
  }
}

void BoxBlur8(std::vector<uint8_t>& data, int w, int h, int radius, int passes) {
  BoxBlurImpl(data, w, h, radius, passes, false);
}

void BlurRegion(Image& img, const RectI& rect, float radius) {
  RectI r = rect.Intersect(img.Bounds());
  if (r.Empty() || radius <= 0.f) return;
  const int w = r.Width(), h = r.Height();
  std::vector<uint8_t> ch[4];
  for (auto& c : ch) c.resize(size_t(w) * h);
  for (int y = 0; y < h; ++y) {
    const uint32_t* row = img.Row(r.top + y) + r.left;
    for (int x = 0; x < w; ++x) {
      const uint32_t p = row[x];
      const size_t i = size_t(y) * w + x;
      ch[0][i] = ChB(p); ch[1][i] = ChG(p); ch[2][i] = ChR(p); ch[3][i] = ChA(p);
    }
  }
  const int br = BoxRadiusForSigma(radius * 0.5f);
  for (auto& c : ch) BoxBlurImpl(c, w, h, br, 3, true);
  for (int y = 0; y < h; ++y) {
    uint32_t* row = img.Row(r.top + y) + r.left;
    for (int x = 0; x < w; ++x) {
      const size_t i = size_t(y) * w + x;
      const uint8_t a = ch[3][i];
      row[x] = PackBGRA(std::min(ch[0][i], a), std::min(ch[1][i], a), std::min(ch[2][i], a), a);
    }
  }
}

void PixelateRegion(Image& img, const RectI& rect, int block) {
  RectI r = rect.Intersect(img.Bounds());
  if (r.Empty() || block <= 1) return;
  for (int by = r.top; by < r.bottom; by += block) {
    for (int bx = r.left; bx < r.right; bx += block) {
      const int ex = std::min(r.right, bx + block), ey = std::min(r.bottom, by + block);
      uint64_t s[4] = {0, 0, 0, 0};
      for (int y = by; y < ey; ++y) {
        for (int x = bx; x < ex; ++x) {
          const uint32_t p = img.At(x, y);
          s[0] += ChB(p); s[1] += ChG(p); s[2] += ChR(p); s[3] += ChA(p);
        }
      }
      const uint64_t n = uint64_t(ex - bx) * (ey - by);
      const uint32_t avg = PackBGRA(uint8_t(s[0] / n), uint8_t(s[1] / n), uint8_t(s[2] / n),
                                    uint8_t(s[3] / n));
      for (int y = by; y < ey; ++y) {
        for (int x = bx; x < ex; ++x) img.At(x, y) = avg;
      }
    }
  }
}

Image FlattenOver(const Image& img, uint32_t background) {
  Image out = img;
  const uint32_t bg = background | 0xFF000000u;
  for (auto& p : out.pixels) p = Over(p, bg);
  return out;
}

Image Resize(const Image& img, int w, int h) {
  if (img.Empty() || w <= 0 || h <= 0) return {};
  Image out(w, h);
  for (int dy = 0; dy < h; ++dy) {
    const int sy0 = int(int64_t(dy) * img.height / h);
    const int sy1 = std::max(sy0 + 1, int(int64_t(dy + 1) * img.height / h));
    for (int dx = 0; dx < w; ++dx) {
      const int sx0 = int(int64_t(dx) * img.width / w);
      const int sx1 = std::max(sx0 + 1, int(int64_t(dx + 1) * img.width / w));
      uint64_t s[4] = {0, 0, 0, 0};
      for (int y = sy0; y < sy1; ++y) {
        const uint32_t* row = img.Row(y);
        for (int x = sx0; x < sx1; ++x) {
          const uint32_t p = row[x];
          s[0] += ChB(p); s[1] += ChG(p); s[2] += ChR(p); s[3] += ChA(p);
        }
      }
      const uint64_t n = uint64_t(sx1 - sx0) * (sy1 - sy0);
      out.At(dx, dy) = PackBGRA(uint8_t(s[0] / n), uint8_t(s[1] / n), uint8_t(s[2] / n),
                                uint8_t(s[3] / n));
    }
  }
  return out;
}

}
