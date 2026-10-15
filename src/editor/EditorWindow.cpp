#include "editor/EditorWindow.h"

#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "app/App.h"
#include "core/Log.h"
#include "core/Strings.h"
#include "output/Output.h"

namespace opal {
namespace {

constexpr float kBar = 52.f;
constexpr float kPad = 28.f;

constexpr uint32_t kColors[] = {0xFF3B30, 0xFF9500, 0xFFCC00, 0x34C759, 0x007AFF, 0xAF52DE, 0x1D1D1F, 0xFFFFFF};
constexpr float kWidths[] = {2.f, 4.f, 8.f};
constexpr float kFonts[] = {16.f, 24.f, 36.f};

struct ToolDef {
  Tool tool;
  Icon icon;
  S tip;
  wchar_t key;
};
constexpr ToolDef kTools[] = {
    {Tool::Select, Icon::Select, S::ToolSelect, L'V'},       {Tool::Pen, Icon::Pen, S::ToolPen, L'P'},
    {Tool::Highlighter, Icon::Highlighter, S::ToolHighlighter, L'H'}, {Tool::Arrow, Icon::Arrow, S::ToolArrow, L'A'},
    {Tool::Rect, Icon::Rect, S::ToolRect, L'R'},             {Tool::Ellipse, Icon::Ellipse, S::ToolEllipse, L'O'},
    {Tool::Text, Icon::Text, S::ToolText, L'T'},             {Tool::Blur, Icon::Blur, S::ToolBlur, L'B'},
    {Tool::Pixelate, Icon::Pixelate, S::ToolPixelate, L'X'}, {Tool::Crop, Icon::Crop, S::ToolCrop, L'C'},
};

RectI NormRect(RectI r) {
  if (r.left > r.right) std::swap(r.left, r.right);
  if (r.top > r.bottom) std::swap(r.top, r.bottom);
  return r;
}

}

EditorWindow::EditorWindow(Image image, std::wstring path, float captureScale)
    : image_(std::move(image)), path_(std::move(path)), captureScale_(std::max(1.f, captureScale)) {
  doc_.crop = image_.Bounds();
  cropDraft_ = doc_.crop;
}

bool EditorWindow::Init(const RectI& work) {
  const float s = float(GetDpiForSystem()) / 96.f;
  const int minW = int(760 * s), minH = int(500 * s);
  int w = std::clamp(int(image_.width + 2 * kPad * s), minW, int(work.Width() * 0.9f));
  int h = std::clamp(int(image_.height + (kBar + 2 * kPad) * s), minH, int(work.Height() * 0.9f));
  CreateParams p;
  p.style = WS_OVERLAPPEDWINDOW & ~WS_SYSMENU;
  p.title = T(S::MarkupTitle);
  p.rect = {work.left + (work.Width() - w) / 2, work.top + (work.Height() - h) / 2, 0, 0};
  p.rect.right = p.rect.left + w;
  p.rect.bottom = p.rect.top + h;
  p.customFrame = true;
  p.mica = true;
  if (!Create(p)) return false;
  ApplyDarkFrame(Theme::Current().dark);
  RenderNow();
  ShowWindow(hwnd(), SW_SHOW);
  SetForegroundWindow(hwnd());
  return true;
}

void EditorWindow::OnDeviceLost() {
  base_.Reset();
  for (auto& a : doc_.items) a.patch.Reset();
  for (auto& st : undo_) for (auto& a : st.items) a.patch.Reset();
  for (auto& st : redo_) for (auto& a : st.items) a.patch.Reset();
}

D2D1_COLOR_F EditorWindow::ClearColor() {
  if (MicaSupported()) return {0, 0, 0, 0};
  return Theme::Current().windowBg;
}

float EditorWindow::StrokeWidth() const { return kWidths[width_] * captureScale_; }
float EditorWindow::FontSize() const { return kFonts[width_] * captureScale_; }
D2D1_COLOR_F EditorWindow::CurrentColor() const { return Hex(kColors[color_]); }

D2D1_POINT_2F EditorWindow::ToImage(D2D1_POINT_2F dip) const {
  const float s = Scale();
  return {(dip.x * s - origin_.x) / zoom_, (dip.y * s - origin_.y) / zoom_};
}

void EditorWindow::PushUndo() {
  undo_.push_back(doc_);
  if (undo_.size() > 100) undo_.erase(undo_.begin());
  redo_.clear();
  dirty_ = true;
}

void EditorWindow::Undo() {
  CommitText();
  if (undo_.empty()) return;
  redo_.push_back(doc_);
  doc_ = std::move(undo_.back());
  undo_.pop_back();
  selected_ = -1;
  cropDraft_ = doc_.crop;
  dirty_ = true;
}

void EditorWindow::Redo() {
  if (redo_.empty()) return;
  undo_.push_back(doc_);
  doc_ = std::move(redo_.back());
  redo_.pop_back();
  selected_ = -1;
  cropDraft_ = doc_.crop;
  dirty_ = true;
}

void EditorWindow::CommitText() {
  if (editingText_ < 0) return;
  if (editingText_ < int(doc_.items.size()) && doc_.items[size_t(editingText_)].text.empty()) {
    doc_.items.erase(doc_.items.begin() + editingText_);
    if (!undo_.empty()) undo_.pop_back();
  }
  editingText_ = -1;
}

void EditorWindow::CommitCrop() {
  const RectI draft = NormRect(cropDraft_).Intersect(image_.Bounds());
  if (draft.Width() >= 4 && draft.Height() >= 4 && !(draft == doc_.crop)) {
    PushUndo();
    doc_.crop = draft;
  }
  cropDraft_ = doc_.crop;
}

void EditorWindow::SetTool(Tool t) {
  if (t == tool_) return;
  CommitText();
  if (tool_ == Tool::Crop) CommitCrop();
  tool_ = t;
  if (t == Tool::Crop) cropDraft_ = doc_.crop;
  if (t != Tool::Select) selected_ = -1;
  colorOpen_ = false;
}

bool EditorWindow::Flatten(Image* out) {
  CommitText();
  if (tool_ == Tool::Crop) CommitCrop();
  return RenderDocument(image_, doc_, out);
}

void EditorWindow::Finish(bool save) {
  if (save) {
    Image out;
    if (Flatten(&out)) {
      App::Get().Deliver(out, path_, true, nullptr);
    } else {
      MessageBoxW(hwnd(), T(S::SaveFailed), L"Opal", MB_ICONWARNING);
      return;
    }
  }
  dirty_ = false;
  Close();
}

void EditorWindow::CopyToClipboard() {
  Image out;
  if (Flatten(&out)) App::Get().Deliver(out, L"", true, nullptr);
}

void EditorWindow::SaveAs() {
  Image out;
  if (!Flatten(&out)) return;
  ComPtr<IFileSaveDialog> dlg;
  if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
  const COMDLG_FILTERSPEC types[] = {{L"PNG", L"*.png"}};
  dlg->SetFileTypes(1, types);
  dlg->SetDefaultExtension(L"png");
  if (!path_.empty()) dlg->SetFileName(path_.substr(path_.find_last_of(L'\\') + 1).c_str());
  if (FAILED(dlg->Show(hwnd()))) return;
  ComPtr<IShellItem> item;
  PWSTR file = nullptr;
  if (SUCCEEDED(dlg->GetResult(&item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &file))) {
    std::vector<uint8_t> png;
    if (!output::EncodePng(out, &png) || !output::WriteFileBytes(file, png)) {
      MessageBoxW(hwnd(), T(S::SaveFailed), L"Opal", MB_ICONWARNING);
    } else {
      dirty_ = false;
    }
    CoTaskMemFree(file);
  }
}

void EditorWindow::RequestClose() {
  if (!dirty_) {
    Close();
    return;
  }
  const int r = MessageBoxW(hwnd(), T(S::SaveChangesQ), T(S::MarkupTitle), MB_YESNOCANCEL | MB_ICONQUESTION);
  if (r == IDYES) Finish(true);
  else if (r == IDNO) Finish(false);
}

LRESULT EditorWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM, bool& handled) {
  handled = false;
  if (msg == WM_CLOSE) {
    handled = true;
    RequestClose();
    return 0;
  }
  if (msg == WM_GETMINMAXINFO) {
    (void)wp;
  }
  return 0;
}

LRESULT EditorWindow::HitTestClient(POINT pt) {
  const float s = Scale();
  const D2D1_POINT_2F dip{pt.x / s, pt.y / s};
  if (dip.y >= kBar) return HTCLIENT;
  for (const auto& r : hitRects_) {
    if (InRect(r, dip)) return HTCLIENT;
  }
  return HTCAPTION;
}

HCURSOR EditorWindow::Cursor() {
  if (InRect(canvasDip_, input_.mouse) && !colorOpen_) {
    switch (tool_) {
      case Tool::Select: return LoadCursorW(nullptr, IDC_ARROW);
      case Tool::Text: return LoadCursorW(nullptr, IDC_IBEAM);
      default: return LoadCursorW(nullptr, IDC_CROSS);
    }
  }
  return LoadCursorW(nullptr, IDC_ARROW);
}

void EditorWindow::HandleKeys() {
  const UiInput& in = ui_.in;
  if (editingText_ >= 0 && editingText_ < int(doc_.items.size())) {
    Annotation& a = doc_.items[size_t(editingText_)];
    for (wchar_t c : in.chars) {
      if (c == L'\r') a.text.push_back(L'\n');
      else if (c >= 0x20 && c != 0x7F) a.text.push_back(c);
    }
    for (unsigned k : in.keys) {
      if (k == VK_BACK && !a.text.empty()) a.text.pop_back();
      if (k == VK_ESCAPE) CommitText();
    }
    return;
  }
  for (unsigned k : in.keys) {
    if (in.ctrl) {
      if (k == 'Z' && in.shift) Redo();
      else if (k == 'Z') Undo();
      else if (k == 'Y') Redo();
      else if (k == 'C') AfterFrame([this] { CopyToClipboard(); });
      else if (k == 'S') AfterFrame([this] { SaveAs(); });
      continue;
    }
    if ((k == VK_DELETE || k == VK_BACK) && selected_ >= 0 && selected_ < int(doc_.items.size())) {
      PushUndo();
      doc_.items.erase(doc_.items.begin() + selected_);
      selected_ = -1;
      continue;
    }
    if (k == VK_ESCAPE) {
      if (tool_ == Tool::Crop) {
        cropDraft_ = doc_.crop;
        SetTool(Tool::Select);
      } else {
        selected_ = -1;
        colorOpen_ = false;
      }
      continue;
    }
    if (k == VK_RETURN && tool_ == Tool::Crop) {
      SetTool(Tool::Select);
      continue;
    }
    for (const auto& t : kTools) {
      if (k == unsigned(t.key)) SetTool(t.tool);
    }
  }
}

void EditorWindow::HandleCanvasInput(ID2D1DeviceContext5* dc) {
  const UiInput& in = ui_.in;
  const D2D1_POINT_2F pt = ToImage(in.mouse);
  const bool inCanvas = InRect(canvasDip_, in.mouse);
  const float tol = 6.f * Scale() / zoom_;

  if (in.pressed && inCanvas && !colorOpen_) {
    switch (tool_) {
      case Tool::Select: {
        selected_ = -1;
        for (int i = int(doc_.items.size()) - 1; i >= 0; --i) {
          if (doc_.items[size_t(i)].HitTest(pt, tol, &ui_.p)) {
            selected_ = i;
            break;
          }
        }
        moving_ = selected_ >= 0;
        moveUndoPushed_ = false;
        lastPt_ = pt;
        if (in.doubleClick && selected_ >= 0 && doc_.items[size_t(selected_)].kind == Tool::Text) {
          editingText_ = selected_;
          moving_ = false;
        }
        break;
      }
      case Tool::Pen:
      case Tool::Highlighter:
      case Tool::Arrow:
      case Tool::Rect:
      case Tool::Ellipse:
      case Tool::Blur:
      case Tool::Pixelate: {
        PushUndo();
        Annotation a;
        a.kind = tool_;
        a.color = CurrentColor();
        a.width = StrokeWidth();
        a.a = a.b = pt;
        if (tool_ == Tool::Pen || tool_ == Tool::Highlighter) a.points.push_back(pt);
        doc_.items.push_back(std::move(a));
        drawing_ = int(doc_.items.size()) - 1;
        break;
      }
      case Tool::Text: {
        const bool wasEditing = editingText_ >= 0;
        CommitText();
        int hit = -1;
        for (int i = int(doc_.items.size()) - 1; i >= 0; --i) {
          const Annotation& a = doc_.items[size_t(i)];
          if (a.kind == Tool::Text && a.HitTest(pt, tol, &ui_.p)) {
            hit = i;
            break;
          }
        }
        if (hit >= 0) {
          PushUndo();
          editingText_ = hit;
        } else if (!wasEditing) {
          PushUndo();
          Annotation a;
          a.kind = Tool::Text;
          a.color = CurrentColor();
          a.fontSize = FontSize();
          a.a = {pt.x, pt.y - a.fontSize * 0.65f};
          doc_.items.push_back(std::move(a));
          editingText_ = int(doc_.items.size()) - 1;
        }
        break;
      }
      case Tool::Crop: {
        const RectI r = NormRect(cropDraft_);
        const float hx[3] = {float(r.left), float(r.left + r.right) / 2, float(r.right)};
        const float hy[3] = {float(r.top), float(r.top + r.bottom) / 2, float(r.bottom)};
        const int order[8][2] = {{0, 0}, {1, 0}, {2, 0}, {2, 1}, {2, 2}, {1, 2}, {0, 2}, {0, 1}};
        cropHandle_ = -1;
        for (int i = 0; i < 8; ++i) {
          if (std::fabs(pt.x - hx[order[i][0]]) <= tol * 1.6f && std::fabs(pt.y - hy[order[i][1]]) <= tol * 1.6f) {
            cropHandle_ = i;
            break;
          }
        }
        if (cropHandle_ < 0) cropHandle_ = r.Contains(int(pt.x), int(pt.y)) ? 8 : 9;
        cropStart_ = r;
        cropAnchor_ = pt;
        break;
      }
    }
  }

  if (in.mouseDown) {
    if (drawing_ >= 0 && drawing_ < int(doc_.items.size())) {
      Annotation& a = doc_.items[size_t(drawing_)];
      D2D1_POINT_2F q = pt;
      if (in.shift && a.kind != Tool::Pen && a.kind != Tool::Highlighter) {
        const float dx = q.x - a.a.x, dy = q.y - a.a.y;
        if (a.kind == Tool::Arrow) {
          const float ang = std::round(std::atan2(dy, dx) / 0.785398f) * 0.785398f;
          const float len = std::hypot(dx, dy);
          q = {a.a.x + std::cos(ang) * len, a.a.y + std::sin(ang) * len};
        } else {
          const float side = std::max(std::fabs(dx), std::fabs(dy));
          q = {a.a.x + (dx < 0 ? -side : side), a.a.y + (dy < 0 ? -side : side)};
        }
      }
      if (a.kind == Tool::Pen || a.kind == Tool::Highlighter) {
        const auto& last = a.points.back();
        if (std::hypot(q.x - last.x, q.y - last.y) >= 1.5f / zoom_) a.points.push_back(q);
      } else {
        a.b = q;
        UpdateEffectPatch(dc, a, image_, captureScale_);
      }
    } else if (moving_ && selected_ >= 0 && selected_ < int(doc_.items.size())) {
      const float dx = pt.x - lastPt_.x, dy = pt.y - lastPt_.y;
      if (dx != 0 || dy != 0) {
        if (!moveUndoPushed_) {
          PushUndo();
          moveUndoPushed_ = true;
        }
        Annotation& a = doc_.items[size_t(selected_)];
        a.Offset(dx, dy);
        UpdateEffectPatch(dc, a, image_, captureScale_);
        lastPt_ = pt;
      }
    } else if (tool_ == Tool::Crop && cropHandle_ >= 0) {
      const int dx = int(std::lround(pt.x - cropAnchor_.x)), dy = int(std::lround(pt.y - cropAnchor_.y));
      RectI r = cropStart_;
      switch (cropHandle_) {
        case 8: r = r.Offset(dx, dy); break;
        case 9: r = RectI::FromPoints({int(cropAnchor_.x), int(cropAnchor_.y)}, {int(pt.x), int(pt.y)}); break;
        default:
          if (cropHandle_ == 0 || cropHandle_ == 6 || cropHandle_ == 7) r.left += dx;
          if (cropHandle_ == 2 || cropHandle_ == 3 || cropHandle_ == 4) r.right += dx;
          if (cropHandle_ == 0 || cropHandle_ == 1 || cropHandle_ == 2) r.top += dy;
          if (cropHandle_ == 4 || cropHandle_ == 5 || cropHandle_ == 6) r.bottom += dy;
          break;
      }
      if (cropHandle_ == 8) {
        const RectI b = image_.Bounds();
        if (r.left < 0) r = r.Offset(-r.left, 0);
        if (r.top < 0) r = r.Offset(0, -r.top);
        if (r.right > b.right) r = r.Offset(b.right - r.right, 0);
        if (r.bottom > b.bottom) r = r.Offset(0, b.bottom - r.bottom);
      }
      cropDraft_ = NormRect(r).Intersect(image_.Bounds());
    }
  }

  if (in.released) {
    if (drawing_ >= 0 && drawing_ < int(doc_.items.size())) {
      const Annotation& a = doc_.items[size_t(drawing_)];
      const bool tiny = (a.kind == Tool::Pen || a.kind == Tool::Highlighter)
                            ? false
                            : std::hypot(a.b.x - a.a.x, a.b.y - a.a.y) < 3.f;
      if (tiny) {
        doc_.items.erase(doc_.items.begin() + drawing_);
        if (!undo_.empty()) undo_.pop_back();
      }
    }
    drawing_ = -1;
    moving_ = false;
    cropHandle_ = -1;
  }
}

void EditorWindow::OnRender(ID2D1DeviceContext5* dc) {
  if (!base_) {
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    dc->CreateBitmap(D2D1::SizeU(UINT(image_.width), UINT(image_.height)), image_.pixels.data(),
                     UINT(image_.width * 4), &props, &base_);
    for (auto& a : doc_.items) UpdateEffectPatch(dc, a, image_, captureScale_);
  }
  BeginUiFrame(dc);
  HandleKeys();
  const D2D1_SIZE_F sz = ClientSizeDip();
  canvasDip_ = {0, kBar, sz.width, sz.height};
  if (!(colorOpen_ && ui_.in.pressed)) HandleCanvasInput(dc);
  DrawCanvas(dc);
  DrawToolbar(dc);
  EndUiFrame(dc);
}

void EditorWindow::DrawCanvas(ID2D1DeviceContext5* dc) {
  const Theme& t = Theme::Current();
  Painter& p = ui_.p;
  p.FillRect(canvasDip_, t.dark ? Hex(0x1A1A1A, 0.92f) : Hex(0xE9E9EB, 0.92f));
  p.Line({canvasDip_.left, canvasDip_.top + 0.5f}, {canvasDip_.right, canvasDip_.top + 0.5f}, t.separator, 1.f);

  const float s = Scale();
  const RectI view = tool_ == Tool::Crop ? image_.Bounds() : doc_.crop;
  const float availW = (RW(canvasDip_) - 2 * kPad) * s, availH = (RH(canvasDip_) - 2 * kPad) * s;
  zoom_ = std::min({availW / std::max(1, view.Width()), availH / std::max(1, view.Height()), 1.f});
  zoom_ = std::max(zoom_, 0.02f);
  const float cx = (canvasDip_.left + canvasDip_.right) / 2 * s, cy = (canvasDip_.top + canvasDip_.bottom) / 2 * s;
  origin_ = {cx - (view.left + view.Width() / 2.f) * zoom_, cy - (view.top + view.Height() / 2.f) * zoom_};

  const D2D1_RECT_F viewPx{origin_.x + view.left * zoom_, origin_.y + view.top * zoom_,
                           origin_.x + view.right * zoom_, origin_.y + view.bottom * zoom_};
  const D2D1_RECT_F viewDip{viewPx.left / s, viewPx.top / s, viewPx.right / s, viewPx.bottom / s};
  if (!image_.HasTransparency()) p.SoftShadow(viewDip, 2.f, 14.f, t.dark ? 0.45f : 0.22f, 4.f);

  const D2D1::Matrix3x2F world = D2D1::Matrix3x2F::Scale(zoom_, zoom_) * D2D1::Matrix3x2F::Translation(origin_.x, origin_.y);
  dc->SetTransform(world);
  const D2D1_RECT_F viewImg{float(view.left), float(view.top), float(view.right), float(view.bottom)};
  dc->PushAxisAlignedClip(viewImg, D2D1_ANTIALIAS_MODE_ALIASED);
  if (base_) {
    dc->DrawBitmap(base_.Get(), D2D1::RectF(0, 0, float(image_.width), float(image_.height)), 1.f,
                   zoom_ < 1.f ? D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC : D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
  }
  for (const auto& a : doc_.items) DrawAnnotation(dc, p, a);
  dc->PopAxisAlignedClip();

  const float px1 = 1.f / zoom_;
  if (selected_ >= 0 && selected_ < int(doc_.items.size())) {
    const D2D1_RECT_F b = doc_.items[size_t(selected_)].Bounds(&p);
    ComPtr<ID2D1Factory> f;
    dc->GetFactory(&f);
    ComPtr<ID2D1StrokeStyle> dash;
    const float dashes[] = {4.f, 3.f};
    f->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                                                     D2D1_LINE_JOIN_MITER, 10.f, D2D1_DASH_STYLE_CUSTOM),
                         dashes, 2, &dash);
    dc->DrawRectangle(Inset(b, -3.f * px1), p.Brush(t.accent), 1.5f * px1 * s, dash.Get());
  }
  if (editingText_ >= 0 && editingText_ < int(doc_.items.size())) {
    const Annotation& a = doc_.items[size_t(editingText_)];
    const D2D1_RECT_F b = a.Bounds(&p);
    dc->DrawRectangle(Inset(b, -4.f * px1), p.Brush(WithAlpha(t.accent, 0.6f)), 1.f * px1 * s);
    std::wstring_view lastLine = a.text;
    if (size_t nl = lastLine.find_last_of(L'\n'); nl != std::wstring_view::npos) lastLine = lastLine.substr(nl + 1);
    const D2D1_SIZE_F lw = p.Measure(lastLine.empty() ? std::wstring_view() : lastLine, a.fontSize, FontWeight::Semibold);
    const int lines = int(std::count(a.text.begin(), a.text.end(), L'\n'));
    const float lineH = a.fontSize * 1.33f;
    const float x = a.a.x + lw.width + 1.f * px1;
    const float y0 = a.a.y + lines * lineH + a.fontSize * 0.12f;
    dc->DrawLine({x, y0}, {x, y0 + a.fontSize * 1.1f}, p.Brush(t.accent), 2.f * px1 * s);
  }

  if (tool_ == Tool::Crop) {
    const RectI r = NormRect(cropDraft_);
    const D2D1_RECT_F cr{float(r.left), float(r.top), float(r.right), float(r.bottom)};
    const D2D1_RECT_F full{0, 0, float(image_.width), float(image_.height)};
    const D2D1_COLOR_F dim = Hex(0x000000, 0.5f);
    p.FillRect({full.left, full.top, full.right, cr.top}, dim);
    p.FillRect({full.left, cr.bottom, full.right, full.bottom}, dim);
    p.FillRect({full.left, cr.top, cr.left, cr.bottom}, dim);
    p.FillRect({cr.right, cr.top, full.right, cr.bottom}, dim);
    dc->DrawRectangle(cr, p.Brush(Hex(0xFFFFFF)), 1.5f * px1 * s);
    for (int i = 1; i < 3; ++i) {
      const float gx = cr.left + RW(cr) * i / 3, gy = cr.top + RH(cr) * i / 3;
      p.Line({gx, cr.top}, {gx, cr.bottom}, Hex(0xFFFFFF, 0.35f), px1 * s);
      p.Line({cr.left, gy}, {cr.right, gy}, Hex(0xFFFFFF, 0.35f), px1 * s);
    }
    const float hs = 5.f * s * px1;
    const float hx[3] = {cr.left, (cr.left + cr.right) / 2, cr.right};
    const float hy[3] = {cr.top, (cr.top + cr.bottom) / 2, cr.bottom};
    for (int ix = 0; ix < 3; ++ix) {
      for (int iy = 0; iy < 3; ++iy) {
        if (ix == 1 && iy == 1) continue;
        dc->FillRectangle({hx[ix] - hs, hy[iy] - hs, hx[ix] + hs, hy[iy] + hs}, p.Brush(Hex(0xFFFFFF)));
      }
    }
  }
  dc->SetTransform(D2D1::Matrix3x2F::Scale(s, s));
}

void EditorWindow::DrawToolbar(ID2D1DeviceContext5* dc) {
  (void)dc;
  const Theme& t = Theme::Current();
  Painter& p = ui_.p;
  hitRects_.clear();
  const D2D1_SIZE_F sz = ClientSizeDip();
  if (!MicaSupported()) p.FillRect({0, 0, sz.width, kBar}, t.windowBg);

  float x = 14.f;
  const float y = (kBar - 34.f) / 2;
  for (const auto& td : kTools) {
    const D2D1_RECT_F r = RX(x, y, 34.f, 34.f);
    hitRects_.push_back(r);
    std::wstring tip = std::wstring(T(td.tip)) + L"  (" + td.key + L")";
    if (ui_.IconButton(MakeId("ed.tool", uint64_t(td.tool)), r, td.icon, tool_ == td.tool, tip.c_str(), false, 18.f)) {
      SetTool(td.tool);
    }
    x += 36.f;
  }
  x += 6.f;
  p.Line({x, y + 8.f}, {x, y + 26.f}, t.separator, 1.f);
  x += 8.f;

  colorAnchor_ = RX(x, y, 34.f, 34.f);
  hitRects_.push_back(colorAnchor_);
  bool swHover = false;
  if (ui_.Press(MakeId("ed.swatch"), colorAnchor_, &swHover)) colorOpen_ = !colorOpen_;
  if (swHover || colorOpen_) p.FillRound(colorAnchor_, 7.f, WithAlpha(t.text, 0.07f));
  const D2D1_POINT_2F sc = Center(colorAnchor_);
  p.FillCircle(sc, 9.f, CurrentColor());
  p.StrokeRound(RX(sc.x - 9.f, sc.y - 9.f, 18.f, 18.f), 9.f, Hex(0x000000, 0.2f), 1.f);
  ui_.Tooltip(MakeId("ed.swatch"), colorAnchor_, T(S::Color));
  x += 38.f;

  for (int i = 0; i < 3; ++i) {
    const D2D1_RECT_F r = RX(x, y, 30.f, 34.f);
    hitRects_.push_back(r);
    bool hov = false;
    if (ui_.Press(MakeId("ed.width", uint64_t(i)), r, &hov)) width_ = i;
    if (width_ == i) p.FillRound(r, 7.f, WithAlpha(t.accent, t.dark ? 0.28f : 0.16f));
    else if (hov) p.FillRound(r, 7.f, WithAlpha(t.text, 0.07f));
    const float lw = 1.5f + i * 1.75f;
    const D2D1_POINT_2F c = Center(r);
    p.FillRound(RX(c.x - 8.f, c.y - lw / 2, 16.f, lw), lw / 2, width_ == i ? (t.dark ? t.accentHover : t.accent) : t.text);
    x += 32.f;
  }

  float capLeft = 0;
  const int cap = ui_.CaptionButtons(MakeId("ed.caption"), sz.width, 0.f, 36.f, IsZoomed(hwnd()) != FALSE, true, &capLeft);
  hitRects_.push_back({capLeft, 0, sz.width, 36.f});
  float rx = capLeft - 10.f;
  const D2D1_RECT_F done = RX(rx - 76.f, (kBar - 28.f) / 2, 76.f, 28.f);
  hitRects_.push_back(done);
  if (ui_.Button(MakeId("ed.done"), done, T(S::Done), true)) AfterFrame([this] { Finish(true); });
  rx = done.left - 8.f;
  const D2D1_RECT_F copy = RX(rx - 34.f, y, 34.f, 34.f);
  hitRects_.push_back(copy);
  if (ui_.IconButton(MakeId("ed.copy"), copy, Icon::Copy, false, T(S::Copy))) AfterFrame([this] { CopyToClipboard(); });
  rx = copy.left - 2.f;
  const D2D1_RECT_F save = RX(rx - 34.f, y, 34.f, 34.f);
  hitRects_.push_back(save);
  if (ui_.IconButton(MakeId("ed.save"), save, Icon::Download, false, T(S::SaveAs))) AfterFrame([this] { SaveAs(); });
  rx = save.left - 8.f;
  p.Line({rx, y + 8.f}, {rx, y + 26.f}, t.separator, 1.f);
  rx -= 8.f;
  const D2D1_RECT_F redo = RX(rx - 34.f, y, 34.f, 34.f);
  hitRects_.push_back(redo);
  if (ui_.IconButton(MakeId("ed.redo"), redo, Icon::Redo, false, T(S::Redo))) Redo();
  const D2D1_RECT_F undo = RX(redo.left - 36.f, y, 34.f, 34.f);
  hitRects_.push_back(undo);
  if (ui_.IconButton(MakeId("ed.undo"), undo, Icon::Undo, false, T(S::Undo))) Undo();

  if (cap == 1) ShowWindow(hwnd(), SW_MINIMIZE);
  else if (cap == 2) ShowWindow(hwnd(), IsZoomed(hwnd()) ? SW_RESTORE : SW_MAXIMIZE);
  else if (cap == 3) PostMessageW(hwnd(), WM_CLOSE, 0, 0);

  if (colorOpen_) {
    const int n = int(std::size(kColors));
    const D2D1_RECT_F panel = RX(colorAnchor_.left - 6.f, colorAnchor_.bottom + 6.f, n * 30.f + 12.f, 42.f);
    if (ui_.in.pressed && !InRect(panel, ui_.in.mouse) && !InRect(colorAnchor_, ui_.in.mouse)) colorOpen_ = false;
    ui_.Defer([this, panel, n] {
      const Theme& th = Theme::Current();
      Painter& pp = ui_.p;
      pp.SoftShadow(panel, 10.f, 12.f, 0.25f, 5.f);
      pp.FillRound(panel, 10.f, th.popoverBg);
      pp.StrokeRound(panel, 10.f, th.popoverBorder, 1.f);
      for (int i = 0; i < n; ++i) {
        const D2D1_POINT_2F c{panel.left + 6.f + 15.f + i * 30.f, (panel.top + panel.bottom) / 2};
        const D2D1_RECT_F hit = RX(c.x - 13.f, c.y - 13.f, 26.f, 26.f);
        const bool hov = InRect(hit, ui_.in.mouse);
        if (i == color_) pp.StrokeRound(Inset(hit, 0.5f), 13.f, th.accent, 2.f);
        pp.FillCircle(c, hov ? 10.5f : 9.5f, Hex(kColors[i]));
        pp.StrokeRound(RX(c.x - 9.5f, c.y - 9.5f, 19.f, 19.f), 9.5f, Hex(0x000000, 0.18f), 1.f);
        if (hov && ui_.in.released) {
          color_ = i;
          colorOpen_ = false;
          const int target = editingText_ >= 0 ? editingText_ : selected_;
          if (target >= 0 && target < int(doc_.items.size())) {
            PushUndo();
            doc_.items[size_t(target)].color = Hex(kColors[i]);
          }
          ui_.RequestFrame();
        }
      }
    });
  }
}

}
