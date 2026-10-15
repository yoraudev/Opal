#include "capture/EdgeSnap.h"

#include <algorithm>
#include <vector>

namespace opal {
namespace {

float EdgeScore(const Image& img, bool vertical, int b, int s0, int s1, int thr) {
  const int n = s1 - s0;
  if (n <= 0) return 0.f;
  int hits = 0;
  if (vertical) {
    for (int y = s0; y < s1; ++y) {
      const uint32_t* row = img.Row(y);
      if (ColorDistance(row[b - 1], row[b]) > thr) ++hits;
    }
  } else {
    const uint32_t* r0 = img.Row(b - 1);
    const uint32_t* r1 = img.Row(b);
    for (int x = s0; x < s1; ++x) {
      if (ColorDistance(r0[x], r1[x]) > thr) ++hits;
    }
  }
  return float(hits) / float(n);
}

bool SnapOne(const Image& img, bool vertical, int rough, int lo, int hi, int s0, int s1,
             int outwardSign, const EdgeSnapOptions& opt, int* out) {
  const int limit = vertical ? img.width : img.height;
  lo = std::max(lo, 1);
  hi = std::min(hi, limit - 1);
  if (lo > hi || s1 - s0 < 3) return false;
  std::vector<float> score(size_t(hi - lo + 1));
  float best = 0.f;
  for (int b = lo; b <= hi; ++b) {
    score[size_t(b - lo)] = EdgeScore(img, vertical, b, s0, s1, opt.colorThreshold);
    best = std::max(best, score[size_t(b - lo)]);
  }
  if (best < opt.minScore) return false;
  int pick = -1;
  for (int b = lo; b <= hi; ++b) {
    if (score[size_t(b - lo)] < best * 0.85f) continue;
    if (pick < 0 || std::abs(b - rough) < std::abs(pick - rough)) pick = b;
  }
  for (int step = 0; step < 3; ++step) {
    const int next = pick + outwardSign;
    if (next < lo || next > hi || score[size_t(next - lo)] < best * 0.5f) break;
    pick = next;
  }
  *out = pick;
  return true;
}

}

EdgeSnapResult SnapToEdges(const Image& img, const RectI& rough, const EdgeSnapOptions& opt) {
  EdgeSnapResult res;
  RectI r = rough.Intersect(img.Bounds());
  res.rect = r;
  if (r.Width() < 6 || r.Height() < 6) return res;

  for (int round = 0; round < 2; ++round) {
    const int mh = std::max(2, r.Height() / 4);
    const int ys0 = r.top + mh, ys1 = r.bottom - mh;
    int v;
    if (SnapOne(img, true, rough.left, rough.left - opt.band, std::min(rough.left + opt.band, r.right - 4),
                ys0, ys1, -1, opt, &v)) {
      r.left = v;
      res.snapped[0] = true;
    }
    if (SnapOne(img, true, rough.right, std::max(rough.right - opt.band, r.left + 4), rough.right + opt.band,
                ys0, ys1, +1, opt, &v)) {
      r.right = v;
      res.snapped[2] = true;
    }
    const int mw = std::max(2, r.Width() / 4);
    const int xs0 = r.left + mw, xs1 = r.right - mw;
    if (SnapOne(img, false, rough.top, rough.top - opt.band, std::min(rough.top + opt.band, r.bottom - 4),
                xs0, xs1, -1, opt, &v)) {
      r.top = v;
      res.snapped[1] = true;
    }
    if (SnapOne(img, false, rough.bottom, std::max(rough.bottom - opt.band, r.top + 4), rough.bottom + opt.band,
                xs0, xs1, +1, opt, &v)) {
      r.bottom = v;
      res.snapped[3] = true;
    }
  }
  res.rect = r;
  return res;
}

}
