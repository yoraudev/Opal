#pragma once
#include "core/Image.h"

namespace opal {

struct CornerEstimate {
  CornerRadii radii;
  bool trusted[4] = {false, false, false, false};
};

CornerEstimate EstimateCornerRadii(const Image& img, const RectI& rect, int maxRadius = 48);

}
