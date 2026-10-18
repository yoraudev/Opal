#include <windows.h>
#include <ole2.h>

#include <cstdio>
#include <string>
#include <vector>

#include "core/Strings.h"
#include "output/Output.h"
#include "render/Gpu.h"
#include "render/ImageOps.h"
#include "setup/SetupWindow.h"
#include "ui/Theme.h"

using namespace opal;

namespace {

struct Shot {
  const char* name;
  SetupWindow::Kind kind;
  SetupWindow::Page page;
  setup::Step step;
};

uint32_t ToPixel(D2D1_COLOR_F c) {
  return Rgb(uint8_t(c.r * 255.f + 0.5f), uint8_t(c.g * 255.f + 0.5f), uint8_t(c.b * 255.f + 0.5f));
}

}

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) {
    std::printf("usage: opal_preview <output folder>\n");
    return 2;
  }
  const std::wstring outDir = argv[1];
  CreateDirectoryW(outDir.c_str(), nullptr);
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  if (FAILED(OleInitialize(nullptr))) return 1;

  setup::Installed installed;
  installed.present = true;
  installed.version = L"0.1.0";
  installed.dir = L"C:\\Users\\you\\AppData\\Local\\Programs\\Opal";

  const Shot shots[] = {
      {"install", SetupWindow::Kind::Fresh, SetupWindow::Page::Options, setup::Step::Stopping},
      {"update", SetupWindow::Kind::Update, SetupWindow::Page::Options, setup::Step::Stopping},
      {"reinstall", SetupWindow::Kind::Reinstall, SetupWindow::Page::Options, setup::Step::Stopping},
      {"downgrade", SetupWindow::Kind::Downgrade, SetupWindow::Page::Options, setup::Step::Stopping},
      {"working", SetupWindow::Kind::Fresh, SetupWindow::Page::Working, setup::Step::Copying},
      {"done", SetupWindow::Kind::Fresh, SetupWindow::Page::Done, setup::Step::Finished},
      {"failed", SetupWindow::Kind::Fresh, SetupWindow::Page::Failed, setup::Step::Finished},
      {"uninstall", SetupWindow::Kind::Uninstall, SetupWindow::Page::Options, setup::Step::Stopping},
      {"uninstalled", SetupWindow::Kind::Uninstall, SetupWindow::Page::Done, setup::Step::Finished},
  };
  const D2D1_SIZE_F dip = SetupWindow::Size();
  const UINT dpi = 144;
  const SIZE px{LONG(dip.width * dpi / 96.f), LONG(dip.height * dpi / 96.f)};
  int failures = 0;
  for (const wchar_t* lang : {L"en", L"tr"}) {
    SetLanguage(lang);
    for (const bool dark : {false, true}) {
      Theme::Refresh(dark ? ThemePref::Dark : ThemePref::Light);
      for (const Shot& shot : shots) {
        setup::Installed inst = installed;
        if (shot.kind == SetupWindow::Kind::Fresh) inst = {};
        SetupWindow w(shot.kind, inst);
        w.Preview(shot.page, shot.step, T(S::SetupErrorRunning));
        Image img;
        for (int frame = 0; frame < 3 && w.Snapshot(px, dpi, &img); ++frame) {
        }
        if (img.Empty()) {
          ++failures;
          continue;
        }
        Image flat = FlattenOver(img, ToPixel(Theme::Current().windowBg));
        std::vector<uint8_t> png;
        wchar_t name[128];
        swprintf_s(name, L"\\setup-%S-%s-%s.png", shot.name, lang, dark ? L"dark" : L"light");
        if (!output::EncodePng(flat, &png) || !output::WriteFileBytes(outDir + name, png)) ++failures;
      }
    }
  }
  Gpu::Get().Shutdown();
  OleUninitialize();
  std::printf("%d failures\n", failures);
  return failures == 0 ? 0 : 1;
}
