#pragma once
#include <cstdint>
#include <vector>

#include "core/Geometry.h"

namespace opal {

struct Image {
  int width = 0;
  int height = 0;
  std::vector<uint32_t> pixels;

  Image() = default;
  Image(int w, int h, uint32_t fill = 0) : width(w), height(h), pixels(size_t(w) * h, fill) {}

  bool Empty() const { return width <= 0 || height <= 0; }
  RectI Bounds() const { return {0, 0, width, height}; }
  uint32_t* Row(int y) { return pixels.data() + size_t(y) * width; }
  const uint32_t* Row(int y) const { return pixels.data() + size_t(y) * width; }
  uint32_t& At(int x, int y) { return pixels[size_t(y) * width + x]; }
  uint32_t At(int x, int y) const { return pixels[size_t(y) * width + x]; }

  Image Crop(const RectI& r) const;
  bool HasTransparency() const;
};

inline uint8_t ChB(uint32_t p) { return uint8_t(p); }
inline uint8_t ChG(uint32_t p) { return uint8_t(p >> 8); }
inline uint8_t ChR(uint32_t p) { return uint8_t(p >> 16); }
inline uint8_t ChA(uint32_t p) { return uint8_t(p >> 24); }
inline uint32_t PackBGRA(uint8_t b, uint8_t g, uint8_t r, uint8_t a) {
  return uint32_t(b) | (uint32_t(g) << 8) | (uint32_t(r) << 16) | (uint32_t(a) << 24);
}
inline uint32_t Rgb(uint8_t r, uint8_t g, uint8_t b) { return PackBGRA(b, g, r, 255); }

std::vector<uint32_t> ToStraightAlpha(const Image& img);

}
