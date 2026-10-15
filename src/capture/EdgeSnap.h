#pragma once
#include "core/Image.h"

namespace opal {

struct EdgeSnapOptions {
  int band = 24;
  int colorThreshold = 12;
  float minScore = 0.45f;
};

struct EdgeSnapResult {
  RectI rect;
  bool snapped[4] = {false, false, false, false};
  bool Any() const { return snapped[0] || snapped[1] || snapped[2] || snapped[3]; }
};

EdgeSnapResult SnapToEdges(const Image& img, const RectI& rough, const EdgeSnapOptions& opt = {});

inline int ColorDistance(uint32_t a, uint32_t b) {
  auto d = [](int x, int y) { return x > y ? x - y : y - x; };
  return d(ChR(a), ChR(b)) + d(ChG(a), ChG(b)) + d(ChB(a), ChB(b));
}

}
