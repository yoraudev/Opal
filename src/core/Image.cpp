#include "core/Image.h"

#include <algorithm>
#include <cstring>

namespace opal {

Image Image::Crop(const RectI& r) const {
  RectI c = r.Intersect(Bounds());
  if (c.Empty()) return {};
  Image out(c.Width(), c.Height());
  for (int y = 0; y < out.height; ++y) {
    std::memcpy(out.Row(y), Row(c.top + y) + c.left, size_t(out.width) * 4);
  }
  return out;
}

bool Image::HasTransparency() const {
  for (uint32_t p : pixels) {
    if (ChA(p) != 255) return true;
  }
  return false;
}

std::vector<uint32_t> ToStraightAlpha(const Image& img) {
  std::vector<uint32_t> out(img.pixels.size());
  for (size_t i = 0; i < img.pixels.size(); ++i) {
    uint32_t p = img.pixels[i];
    uint32_t a = ChA(p);
    if (a == 255) {
      out[i] = p;
    } else if (a == 0) {
      out[i] = 0;
    } else {
      auto un = [a](uint32_t c) { return uint8_t(std::min<uint32_t>(255, (c * 255 + a / 2) / a)); };
      out[i] = PackBGRA(un(ChB(p)), un(ChG(p)), un(ChR(p)), uint8_t(a));
    }
  }
  return out;
}

}
