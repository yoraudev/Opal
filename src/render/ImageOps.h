#pragma once
#include <vector>

#include "core/Image.h"

namespace opal {

float RoundedRectCoverage(int px, int py, int w, int h, const CornerRadii& r);

void ApplyRoundedMask(Image& img, const CornerRadii& radii);

struct ShadowLayer {
  float blur = 24.f;
  float offsetX = 0.f;
  float offsetY = 12.f;
  float opacity = 0.4f;
};

struct ShadowStyle {
  std::vector<ShadowLayer> layers;
  int padding = -1;
  static ShadowStyle MacWindow(float scale);
};

Image AddDropShadow(const Image& img, const ShadowStyle& style, PointI* outOrigin = nullptr);

void ApplyHairline(Image& img, const CornerRadii& radii, uint32_t premulColor);

void BlurRegion(Image& img, const RectI& rect, float radius);
void PixelateRegion(Image& img, const RectI& rect, int block);

Image FlattenOver(const Image& img, uint32_t background);

Image Resize(const Image& img, int w, int h);

void BoxBlur8(std::vector<uint8_t>& data, int w, int h, int radius, int passes);

}
