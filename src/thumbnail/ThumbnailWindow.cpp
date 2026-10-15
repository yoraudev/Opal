#include "thumbnail/ThumbnailWindow.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <cmath>
#include <cstring>

#include "app/App.h"
#include "app/Settings.h"
#include "app/SystemIntegration.h"
#include "core/Log.h"
#include "core/Strings.h"
#include "output/Output.h"
#include "render/ImageOps.h"

namespace opal {
namespace {

constexpr float kMargin = 26.f;
constexpr float kEdge = 18.f;
constexpr float kMaxW = 236.f, kMaxH = 156.f;
constexpr float kRadius = 7.f;
constexpr UINT_PTR kTick = 3;

enum MenuCmd : UINT { kMCopy = 1, kMMarkup, kMShow, kMSaveAs, kMDelete, kMClose };

HBITMAP MakeDragBitmap(const Image& img) {
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = img.width;
  bi.bmiHeader.biHeight = -img.height;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (bmp) std::memcpy(bits, img.pixels.data(), img.pixels.size() * 4);
  return bmp;
}

}

ThumbnailWindow::ThumbnailWindow(Image image, const RectI& workArea, UINT dpi, float captureScale)
    : image_(std::move(image)), work_(workArea), dpi_(dpi), captureScale_(captureScale) {}

bool ThumbnailWindow::Init() {
  if (image_.Empty()) return false;
  const float aspect = float(image_.width) / float(image_.height);
  thumbW_ = kMaxW;
  thumbH_ = thumbW_ / aspect;
  if (thumbH_ > kMaxH) {
    thumbH_ = kMaxH;
    thumbW_ = thumbH_ * aspect;
  }
  thumbW_ = std::max(thumbW_, 40.f);
  thumbH_ = std::max(thumbH_, 30.f);
  const float s = float(dpi_) / 96.f;
  const int w = int(std::ceil((thumbW_ + 2 * kMargin + kEdge) * s));
  const int h = int(std::ceil((thumbH_ + 2 * kMargin + kEdge) * s));
  CreateParams p;
  p.exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
  p.style = WS_POPUP;
  p.title = L"Opal Thumbnail";
  p.rect = {work_.right - w, work_.bottom - h, work_.right, work_.bottom};
  if (!Create(p)) return false;
  ExcludeFromCapture(hwnd());
  const int pw = std::max(1, int(std::lround(thumbW_ * Scale())));
  const int ph = std::max(1, int(std::lround(thumbH_ * Scale())));
  preview_ = (pw < image_.width) ? Resize(image_, pw, ph) : image_;
  shownAt_ = Now();
  RenderNow();
  ShowWindow(hwnd(), SW_SHOWNOACTIVATE);
  SetTimer(hwnd(), kTick, 200, nullptr);
  return true;
}

void ThumbnailWindow::SetPath(const std::wstring& path) { path_ = path; }

void ThumbnailWindow::Dismiss(bool animated) {
  if (!animated) {
    Close();
    return;
  }
  if (dismissing_) return;
  dismissing_ = true;
  dismissStart_ = Now();
  Invalidate();
}

bool ThumbnailWindow::WantsFrame() { return dismissing_; }

LRESULT ThumbnailWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp, bool& handled) {
  handled = false;
  switch (msg) {
    case WM_MOUSEACTIVATE:
      handled = true;
      return MA_NOACTIVATE;
    case WM_TIMER:
      if (wp == kTick) {
        const bool inside = mouseInside_;
        if (inside != hovered_) {
          hovered_ = inside;
          Invalidate();
        }
        if (hovered_ || pressed_) shownAt_ = Now();
        if (!dismissing_ && Now() - shownAt_ > AppSettings().thumbnailSeconds) Dismiss();
        handled = true;
        return 0;
      }
      break;
    case WM_LBUTTONDOWN:
      if (dismissing_) break;
      pressed_ = true;
      swiping_ = false;
      swipe_ = 0;
      GetCursorPos(&pressPt_);
      break;
    case WM_MOUSEMOVE:
      if (pressed_ && !dismissing_) {
        POINT pt;
        GetCursorPos(&pt);
        const float dx = float(pt.x - pressPt_.x) / Scale(), dy = float(pt.y - pressPt_.y) / Scale();
        if (!swiping_ && std::hypot(dx, dy) > 6.f) {
          if (dx > 0 && std::fabs(dx) > std::fabs(dy) * 1.2f) {
            swiping_ = true;
          } else {
            pressed_ = false;
            if (GetCapture() == hwnd()) ReleaseCapture();
            StartFileDrag();
            return 0;
          }
        }
        if (swiping_) {
          swipe_ = std::max(0.f, dx);
          Invalidate();
        }
      }
      break;
    case WM_LBUTTONUP:
      if (pressed_) {
        pressed_ = false;
        if (swiping_) {
          swiping_ = false;
          if (swipe_ > 48.f) Dismiss();
          swipe_ = 0;
          Invalidate();
        } else {
          const D2D1_POINT_2F m = input_.mouse;
          const D2D1_RECT_F closeBtn = RX(kMargin - 9.f, kMargin - 9.f, 20.f, 20.f);
          if (hovered_ && InRect(closeBtn, m)) Dismiss();
          else OpenEditor();
        }
      }
      break;
    case WM_RBUTTONUP:
      ShowMenu();
      handled = true;
      return 0;
    default:
      break;
  }
  (void)lp;
  return 0;
}

void ThumbnailWindow::OnRender(ID2D1DeviceContext5* dc) {
  if (!bitmap_ && !preview_.Empty()) {
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    dc->CreateBitmap(D2D1::SizeU(UINT(preview_.width), UINT(preview_.height)), preview_.pixels.data(),
                     UINT(preview_.width * 4), &props, &bitmap_);
  }
  BeginUiFrame(dc);
  Painter& p = ui_.p;
  const float travel = thumbW_ + kMargin * 2 + kEdge;
  float x = ui_.AnimFrom(MakeId("thumb.in"), travel, 0.f, 13.f);
  x += ui_.Anim(MakeId("thumb.swipe"), swiping_ ? swipe_ : 0.f, swiping_ ? 60.f : 18.f);
  float alpha = 1.f;
  if (dismissing_) {
    const float t = float(std::min(1.0, (Now() - dismissStart_) / 0.22));
    x += travel * t * t;
    alpha = 1.f - t;
    if (t >= 1.f) {
      EndUiFrame(dc);
      PostMessageW(hwnd(), WM_CLOSE, 0, 0);
      return;
    }
  }
  const D2D1_RECT_F r = RX(kMargin + x, kMargin, thumbW_, thumbH_);
  const bool transparent = image_.HasTransparency();
  if (!transparent) p.SoftShadow(r, kRadius, 16.f, 0.32f * alpha, 5.f);
  if (bitmap_) {
    if (transparent) {
      dc->DrawBitmap(bitmap_.Get(), r, alpha, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
    } else {
      ComPtr<ID2D1Factory> f;
      dc->GetFactory(&f);
      ComPtr<ID2D1RoundedRectangleGeometry> geo;
      f->CreateRoundedRectangleGeometry({r, kRadius, kRadius}, &geo);
      dc->PushLayer(D2D1::LayerParameters1(r, geo.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                           D2D1::IdentityMatrix(), alpha),
                    nullptr);
      dc->DrawBitmap(bitmap_.Get(), r, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
      dc->PopLayer();
      p.StrokeRound(Inset(r, -0.5f), kRadius + 0.5f, Hex(0x000000, 0.18f * alpha), 0.75f);
      p.StrokeRound(r, kRadius, Hex(0xFFFFFF, 0.35f * alpha), 1.f);
    }
  }
  const float hoverA = ui_.Anim(MakeId("thumb.hover"), hovered_ && !dismissing_ && !swiping_ ? 1.f : 0.f, 16.f);
  if (hoverA > 0.01f) {
    const D2D1_RECT_F c = RX(kMargin - 9.f + x, kMargin - 9.f, 20.f, 20.f);
    p.SoftShadow(c, 10.f, 3.f, 0.25f * hoverA, 1.f);
    p.FillRound(c, 10.f, Hex(0x3A3A3C, 0.95f * hoverA));
    p.StrokeRound(c, 10.f, Hex(0xFFFFFF, 0.25f * hoverA), 0.75f);
    p.DrawIcon(Icon::Close, Inset(c, 4.f), Hex(0xFFFFFF, hoverA), 1.6f);
  }
  EndUiFrame(dc);
}

void ThumbnailWindow::OpenEditor() {
  App::Get().OpenEditor(image_, path_, captureScale_);
  Dismiss(false);
}

std::wstring ThumbnailWindow::EnsureFileForDrag() {
  if (!path_.empty() && GetFileAttributesW(path_.c_str()) != INVALID_FILE_ATTRIBUTES) return path_;
  if (!tempPath_.empty()) return tempPath_;
  wchar_t tmp[MAX_PATH];
  GetTempPathW(MAX_PATH, tmp);
  std::wstring dir = std::wstring(tmp) + L"Opal";
  sys::EnsureDirectory(dir);
  std::wstring path = output::MakeCapturePath(dir, AppSettings().filePrefix);
  std::vector<uint8_t> png;
  if (output::EncodePng(image_, &png) && output::WriteFileBytes(path, png)) tempPath_ = path;
  return tempPath_;
}

void ThumbnailWindow::StartFileDrag() {
  const std::wstring file = EnsureFileForDrag();
  if (file.empty()) return;
  ComPtr<IShellItem> item;
  if (FAILED(SHCreateItemFromParsingName(file.c_str(), nullptr, IID_PPV_ARGS(&item)))) return;
  ComPtr<IDataObject> data;
  if (FAILED(item->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data)))) return;
  ComPtr<IDragSourceHelper> helper;
  if (SUCCEEDED(CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&helper)))) {
    SHDRAGIMAGE di{};
    di.sizeDragImage = {preview_.width, preview_.height};
    di.ptOffset = {preview_.width / 2, preview_.height / 2};
    di.hbmpDragImage = MakeDragBitmap(preview_);
    di.crColorKey = CLR_NONE;
    if (di.hbmpDragImage && FAILED(helper->InitializeFromBitmap(&di, data.Get()))) DeleteObject(di.hbmpDragImage);
  }
  ShowWindow(hwnd(), SW_HIDE);
  DWORD effect = DROPEFFECT_NONE;
  const HRESULT hr = SHDoDragDrop(hwnd(), data.Get(), nullptr, DROPEFFECT_COPY, &effect);
  if (hr == DRAGDROP_S_DROP) {
    Dismiss(false);
  } else {
    ShowWindow(hwnd(), SW_SHOWNOACTIVATE);
    shownAt_ = Now();
  }
}

void ThumbnailWindow::SaveAs() {
  ComPtr<IFileSaveDialog> dlg;
  if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
  const COMDLG_FILTERSPEC types[] = {{L"PNG", L"*.png"}};
  dlg->SetFileTypes(1, types);
  dlg->SetDefaultExtension(L"png");
  std::wstring name = path_.empty() ? AppSettings().filePrefix + L".png" : path_.substr(path_.find_last_of(L'\\') + 1);
  dlg->SetFileName(name.c_str());
  if (FAILED(dlg->Show(nullptr))) return;
  ComPtr<IShellItem> result;
  PWSTR out = nullptr;
  if (SUCCEEDED(dlg->GetResult(&result)) && SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &out))) {
    std::vector<uint8_t> png;
    if (!output::EncodePng(image_, &png) || !output::WriteFileBytes(out, png)) {
      MessageBoxW(nullptr, T(S::SaveFailed), L"Opal", MB_ICONWARNING);
    }
    CoTaskMemFree(out);
  }
}

void ThumbnailWindow::DeleteFile() {
  if (path_.empty()) return;
  std::wstring from = path_;
  from.push_back(L'\0');
  SHFILEOPSTRUCTW op{};
  op.wFunc = FO_DELETE;
  op.pFrom = from.c_str();
  op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
  SHFileOperationW(&op);
}

void ThumbnailWindow::ShowMenu() {
  sys::SetDarkMenus(Theme::Current().dark);
  HMENU m = CreatePopupMenu();
  AppendMenuW(m, MF_STRING, kMCopy, T(S::Copy));
  AppendMenuW(m, MF_STRING, kMMarkup, T(S::Markup));
  if (!path_.empty()) AppendMenuW(m, MF_STRING, kMShow, T(S::ShowInFolder));
  AppendMenuW(m, MF_STRING, kMSaveAs, T(S::SaveAs));
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  if (!path_.empty()) AppendMenuW(m, MF_STRING, kMDelete, T(S::Delete));
  AppendMenuW(m, MF_STRING, kMClose, T(S::Close));
  POINT pt;
  GetCursorPos(&pt);
  pressed_ = true;
  SetForegroundWindow(hwnd());
  const UINT cmd = UINT(TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, hwnd(), nullptr));
  DestroyMenu(m);
  pressed_ = false;
  shownAt_ = Now();
  switch (cmd) {
    case kMCopy: App::Get().Deliver(image_, L"", true, nullptr); break;
    case kMMarkup: OpenEditor(); break;
    case kMShow: sys::RevealFile(path_); break;
    case kMSaveAs: SaveAs(); break;
    case kMDelete: DeleteFile(); Dismiss(); break;
    case kMClose: Dismiss(); break;
    default: break;
  }
}

}
