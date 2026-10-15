#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "app/Hotkey.h"
#include "app/Ini.h"
#include "capture/CornerRadius.h"
#include "capture/EdgeSnap.h"
#include "render/ImageOps.h"

using namespace opal;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    ++g_checks;                                                              \
    if (!(cond)) {                                                           \
      ++g_failures;                                                          \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
    }                                                                        \
  } while (0)

#define CHECK_NEAR(a, b, tol)                                                          \
  do {                                                                                 \
    ++g_checks;                                                                        \
    double _a = (a), _b = (b);                                                         \
    if (std::fabs(_a - _b) > (tol)) {                                                  \
      ++g_failures;                                                                    \
      std::printf("  FAIL %s:%d: %s = %.3f, expected %.3f +- %.2f\n", __FILE__, __LINE__, \
                  #a, _a, _b, double(tol));                                            \
    }                                                                                  \
  } while (0)

struct TestCase {
  const char* name;
  std::function<void()> fn;
};
std::vector<TestCase>& Registry() {
  static std::vector<TestCase> r;
  return r;
}
struct Register {
  Register(const char* n, std::function<void()> f) { Registry().push_back({n, std::move(f)}); }
};
#define TEST(name)                                  \
  void name();                                      \
  Register reg_##name(#name, name);                 \
  void name()

uint32_t Blend(uint32_t over, uint32_t under, float a) {
  auto m = [a](int o, int u) { return uint8_t(std::lround(o * a + u * (1 - a))); };
  return PackBGRA(m(ChB(over), ChB(under)), m(ChG(over), ChG(under)), m(ChR(over), ChR(under)), 255);
}

Image MakePanel(int W, int H, RectI panel, float radius, uint32_t bg, uint32_t fill,
                uint32_t border = 0, bool content = true) {
  Image img(W, H, bg);
  const int w = panel.Width(), h = panel.Height();
  const CornerRadii outer = CornerRadii::Uniform(radius);
  const CornerRadii inner = CornerRadii::Uniform(std::max(0.f, radius - 1));
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      uint32_t& p = img.At(panel.left + x, panel.top + y);
      const float co = RoundedRectCoverage(x, y, w, h, outer);
      if (border) {
        p = Blend(border, p, co);
        const bool interior = x > 0 && y > 0 && x < w - 1 && y < h - 1;
        const float ci = interior ? RoundedRectCoverage(x - 1, y - 1, w - 2, h - 2, inner) : 0.f;
        p = Blend(fill, p, ci);
      } else {
        p = Blend(fill, p, co);
      }
    }
  }
  if (content) {
    std::mt19937 rng(42);
    RectI c = panel.Inflate(-std::max(12, int(radius) + 10));
    for (int i = 0; i < c.Area() / 6; ++i) {
      int x = c.left + int(rng() % uint32_t(std::max(1, c.Width())));
      int y = c.top + int(rng() % uint32_t(std::max(1, c.Height())));
      img.At(x, y) = Rgb(20, 20, 20);
    }
  }
  return img;
}

TEST(HotkeyRoundTrip) {
  auto hk = ParseHotkey(L"Ctrl+Shift+4");
  CHECK(hk.has_value());
  CHECK(hk->mods == (kModCtrl | kModShift));
  CHECK(hk->vk == '4');
  CHECK(FormatHotkey(*hk) == L"Ctrl+Shift+4");
  auto ps = ParseHotkey(L"PrintScreen");
  CHECK(ps && ps->vk == 0x2C && ps->mods == 0);
  auto f = ParseHotkey(L"alt+f12");
  CHECK(f && f->vk == 0x7B && f->mods == kModAlt);
  CHECK(FormatHotkey(*f) == L"Alt+F12");
  auto none = ParseHotkey(L"");
  CHECK(none && !none->Valid());
  CHECK(!ParseHotkey(L"Ctrl+").has_value());
  CHECK(!ParseHotkey(L"Ctrl+Bogus").has_value());
  CHECK(!ParseHotkey(L"A+B").has_value());
}

TEST(IniRoundTrip) {
  Ini ini;
  ini.Parse(L"; comment\r\n[General]\r\nlaunch = 1\r\nname=Opal Shot\r\n[Hotkeys]\nbar=PrintScreen\n");
  CHECK(ini.GetBool(L"General", L"launch", false));
  CHECK(ini.Get(L"General", L"name") == L"Opal Shot");
  CHECK(ini.Get(L"Hotkeys", L"bar") == L"PrintScreen");
  CHECK(ini.GetInt(L"General", L"missing", 7) == 7);
  ini.SetInt(L"Capture", L"timer", 5);
  Ini again;
  again.Parse(ini.Serialize());
  CHECK(again.GetInt(L"Capture", L"timer", 0) == 5);
  CHECK(again.Get(L"General", L"name") == L"Opal Shot");
}

TEST(RoundedMaskCorners) {
  Image img(64, 48, Rgb(200, 100, 50));
  ApplyRoundedMask(img, CornerRadii::Uniform(8));
  CHECK(ChA(img.At(0, 0)) == 0);
  CHECK(ChA(img.At(63, 47)) == 0);
  CHECK(ChA(img.At(32, 24)) == 255);
  CHECK(ChA(img.At(32, 0)) == 255);
  CHECK(ChA(img.At(2, 2)) > 0 && ChA(img.At(2, 2)) < 255);
  uint32_t p = img.At(2, 2);
  CHECK(ChR(p) <= ChA(p));
}

TEST(DropShadowLayout) {
  Image img(40, 30, Rgb(255, 255, 255));
  ShadowStyle s;
  s.layers.push_back({10.f, 0.f, 5.f, 0.5f});
  s.padding = 25;
  PointI origin;
  Image out = AddDropShadow(img, s, &origin);
  CHECK(out.width == 90 && out.height == 80);
  CHECK(origin.x == 25 && origin.y == 25);
  CHECK(out.At(45, 40) == Rgb(255, 255, 255));
  CHECK(ChA(out.At(0, 0)) == 0);
  CHECK(ChA(out.At(45, 25 + 30 + 4)) > 0);
  CHECK(ChA(out.At(45, 25 + 30 + 4)) < 255);
}

void CheckRadius(const char* label, uint32_t bg, uint32_t fill, uint32_t border, float radius) {
  RectI panel{30, 20, 190, 140};
  Image img = MakePanel(220, 170, panel, radius, bg, fill, border);
  CornerEstimate est = EstimateCornerRadii(img, panel);
  const float tol = radius < 1 ? 0.6f : 1.0f;
  std::printf("  %s r=%.0f -> %.2f %.2f %.2f %.2f\n", label, radius, est.radii.tl, est.radii.tr,
              est.radii.br, est.radii.bl);
  CHECK_NEAR(est.radii.tl, radius, tol);
  CHECK_NEAR(est.radii.tr, radius, tol);
  CHECK_NEAR(est.radii.br, radius, tol);
  CHECK_NEAR(est.radii.bl, radius, tol);
}

TEST(CornerRadiusLightSubtle) {
  for (float r : {0.f, 4.f, 6.f, 8.f, 12.f, 16.f, 24.f}) {
    CheckRadius("light", Rgb(243, 243, 243), Rgb(251, 251, 251), 0, r);
  }
}

TEST(CornerRadiusDark) {
  for (float r : {0.f, 4.f, 8.f, 12.f, 20.f, 32.f}) {
    CheckRadius("dark", Rgb(28, 28, 30), Rgb(58, 58, 60), 0, r);
  }
}

TEST(CornerRadiusBordered) {
  for (float r : {4.f, 8.f, 12.f}) {
    CheckRadius("border", Rgb(255, 255, 255), Rgb(246, 246, 246), Rgb(210, 210, 210), r);
  }
}

TEST(CornerRadiusFlatIsUntrusted) {
  Image img(120, 100, Rgb(240, 240, 240));
  CornerEstimate est = EstimateCornerRadii(img, {20, 20, 100, 80});
  CHECK(est.radii.IsZero());
  CHECK(!est.trusted[0]);
}

TEST(EdgeSnapFindsPanel) {
  RectI panel{40, 30, 260, 190};
  for (uint32_t border : {0u, uint32_t(Rgb(205, 205, 205))}) {
    Image img = MakePanel(300, 230, panel, 8, Rgb(243, 243, 243), Rgb(251, 251, 251), border);
    RectI rough{47, 24, 269, 184};
    EdgeSnapResult res = SnapToEdges(img, rough);
    std::printf("  snapped -> %d %d %d %d\n", res.rect.left, res.rect.top, res.rect.right,
                res.rect.bottom);
    CHECK(res.rect == panel);
    CHECK(res.snapped[0] && res.snapped[1] && res.snapped[2] && res.snapped[3]);
  }
}

TEST(EdgeSnapLeavesFlatAreaAlone) {
  Image img(200, 200, Rgb(30, 30, 30));
  RectI rough{50, 50, 150, 150};
  EdgeSnapResult res = SnapToEdges(img, rough);
  CHECK(res.rect == rough);
  CHECK(!res.Any());
}

TEST(PixelateAndBlur) {
  Image img(16, 16, Rgb(0, 0, 0));
  for (int x = 0; x < 8; ++x) {
    for (int y = 0; y < 16; ++y) img.At(x, y) = Rgb(255, 255, 255);
  }
  Image p = img;
  PixelateRegion(p, {4, 0, 12, 8}, 8);
  CHECK(ChR(p.At(5, 3)) == 127 || ChR(p.At(5, 3)) == 128);
  Image b = img;
  BlurRegion(b, b.Bounds(), 4.f);
  CHECK(ChR(b.At(8, 8)) > 40 && ChR(b.At(8, 8)) < 215);
  CHECK(ChA(b.At(8, 8)) == 255);
}

TEST(StraightAlpha) {
  Image img(1, 1, PackBGRA(64, 32, 16, 128));
  auto s = ToStraightAlpha(img);
  CHECK(ChA(s[0]) == 128);
  CHECK(ChB(s[0]) == 128);
  CHECK(ChR(s[0]) == 32);
}

}

int main() {
  for (const auto& t : Registry()) {
    int before = g_failures;
    std::printf("[ RUN  ] %s\n", t.name);
    t.fn();
    std::printf("[ %s ] %s\n", g_failures == before ? " OK " : "FAIL", t.name);
  }
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
