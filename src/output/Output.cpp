#include "output/Output.h"

#include <objidl.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstring>

#include "core/Log.h"
#include "core/Strings.h"
#include "render/Gpu.h"
#include "render/ImageOps.h"

namespace opal::output {
namespace {

using Microsoft::WRL::ComPtr;

bool FileExists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

HGLOBAL MakeDib(const Image& img, bool v5, bool flatten) {
  const int w = img.width, h = img.height;
  const size_t headerSize = v5 ? sizeof(BITMAPV5HEADER) : sizeof(BITMAPINFOHEADER);
  const size_t pixelBytes = size_t(w) * h * 4;
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, headerSize + pixelBytes);
  if (!mem) return nullptr;
  auto* base = static_cast<uint8_t*>(GlobalLock(mem));
  std::memset(base, 0, headerSize);
  if (v5) {
    auto* hdr = reinterpret_cast<BITMAPV5HEADER*>(base);
    hdr->bV5Size = sizeof(BITMAPV5HEADER);
    hdr->bV5Width = w;
    hdr->bV5Height = h;
    hdr->bV5Planes = 1;
    hdr->bV5BitCount = 32;
    hdr->bV5Compression = BI_BITFIELDS;
    hdr->bV5SizeImage = DWORD(pixelBytes);
    hdr->bV5RedMask = 0x00FF0000;
    hdr->bV5GreenMask = 0x0000FF00;
    hdr->bV5BlueMask = 0x000000FF;
    hdr->bV5AlphaMask = 0xFF000000;
    hdr->bV5CSType = LCS_sRGB;
    hdr->bV5Intent = LCS_GM_IMAGES;
  } else {
    auto* hdr = reinterpret_cast<BITMAPINFOHEADER*>(base);
    hdr->biSize = sizeof(BITMAPINFOHEADER);
    hdr->biWidth = w;
    hdr->biHeight = h;
    hdr->biPlanes = 1;
    hdr->biBitCount = 32;
    hdr->biCompression = BI_RGB;
    hdr->biSizeImage = DWORD(pixelBytes);
  }
  auto* px = reinterpret_cast<uint32_t*>(base + headerSize);
  if (flatten) {
    Image flat = FlattenOver(img, 0xFFFFFF);
    for (int y = 0; y < h; ++y) std::memcpy(px + size_t(h - 1 - y) * w, flat.Row(y), size_t(w) * 4);
  } else {
    std::vector<uint32_t> straight = ToStraightAlpha(img);
    for (int y = 0; y < h; ++y) std::memcpy(px + size_t(h - 1 - y) * w, straight.data() + size_t(y) * w, size_t(w) * 4);
  }
  GlobalUnlock(mem);
  return mem;
}

}

bool EncodePng(const Image& img, std::vector<uint8_t>* png, IWICImagingFactory* wic) {
  png->clear();
  if (img.Empty()) return false;
  if (!wic) wic = Gpu::Get().Wic();
  if (!wic) return false;
  const bool alpha = img.HasTransparency();
  std::vector<uint32_t> straight;
  const uint32_t* src = img.pixels.data();
  if (alpha) {
    straight = ToStraightAlpha(img);
    src = straight.data();
  }
  ComPtr<IWICBitmap> bmp;
  HRESULT hr = wic->CreateBitmapFromMemory(UINT(img.width), UINT(img.height), GUID_WICPixelFormat32bppBGRA,
                                           UINT(img.width * 4), UINT(img.pixels.size() * 4),
                                           reinterpret_cast<BYTE*>(const_cast<uint32_t*>(src)), &bmp);
  ComPtr<IStream> stream;
  if (SUCCEEDED(hr)) hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
  ComPtr<IWICBitmapEncoder> enc;
  if (SUCCEEDED(hr)) hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
  if (SUCCEEDED(hr)) hr = enc->Initialize(stream.Get(), WICBitmapEncoderNoCache);
  ComPtr<IWICBitmapFrameEncode> frame;
  ComPtr<IPropertyBag2> props;
  if (SUCCEEDED(hr)) hr = enc->CreateNewFrame(&frame, &props);
  if (SUCCEEDED(hr)) hr = frame->Initialize(props.Get());
  if (SUCCEEDED(hr)) hr = frame->SetSize(UINT(img.width), UINT(img.height));
  WICPixelFormatGUID fmt = alpha ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat24bppBGR;
  if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&fmt);
  if (SUCCEEDED(hr)) hr = frame->WriteSource(bmp.Get(), nullptr);
  if (SUCCEEDED(hr)) hr = frame->Commit();
  if (SUCCEEDED(hr)) hr = enc->Commit();
  if (FAILED(hr)) {
    Log(L"PNG encode failed 0x%08X", hr);
    return false;
  }
  HGLOBAL mem = nullptr;
  GetHGlobalFromStream(stream.Get(), &mem);
  STATSTG st{};
  stream->Stat(&st, STATFLAG_NONAME);
  const size_t size = size_t(st.cbSize.QuadPart);
  const void* data = GlobalLock(mem);
  png->assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
  GlobalUnlock(mem);
  return true;
}

bool WriteFileBytes(const std::wstring& path, const std::vector<uint8_t>& bytes) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const BOOL ok = WriteFile(f, bytes.data(), DWORD(bytes.size()), &written, nullptr);
  CloseHandle(f);
  return ok && written == bytes.size();
}

bool LoadImageFile(const std::wstring& path, Image* out) {
  IWICImagingFactory* wic = Gpu::Get().Wic();
  if (!wic) return false;
  ComPtr<IWICBitmapDecoder> dec;
  HRESULT hr = wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec);
  ComPtr<IWICBitmapFrameDecode> frame;
  if (SUCCEEDED(hr)) hr = dec->GetFrame(0, &frame);
  ComPtr<IWICFormatConverter> conv;
  if (SUCCEEDED(hr)) hr = wic->CreateFormatConverter(&conv);
  if (SUCCEEDED(hr)) {
    hr = conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                          WICBitmapPaletteTypeCustom);
  }
  UINT w = 0, h = 0;
  if (SUCCEEDED(hr)) hr = conv->GetSize(&w, &h);
  if (FAILED(hr)) return false;
  *out = Image(int(w), int(h));
  return SUCCEEDED(conv->CopyPixels(nullptr, w * 4, UINT(out->pixels.size() * 4),
                                    reinterpret_cast<BYTE*>(out->pixels.data())));
}

std::wstring MakeCapturePath(const std::wstring& folder, const std::wstring& prefix) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t stamp[64];
  if (CurrentLanguage() == Lang::Tr) {
    swprintf_s(stamp, L"%04d-%02d-%02d %02d.%02d.%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
  } else {
    swprintf_s(stamp, L"%04d-%02d-%02d at %02d.%02d.%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
  }
  std::wstring base = folder + L"\\" + prefix + L" " + stamp;
  std::wstring path = base + L".png";
  for (int i = 2; FileExists(path) && i < 1000; ++i) path = base + L" (" + std::to_wstring(i) + L").png";
  return path;
}

bool CopyImage(HWND owner, const Image& img, const std::vector<uint8_t>* png) {
  if (img.Empty()) return false;
  bool opened = false;
  for (int i = 0; i < 10 && !opened; ++i) {
    opened = OpenClipboard(owner) != FALSE;
    if (!opened) Sleep(15);
  }
  if (!opened) {
    Log(L"OpenClipboard failed");
    return false;
  }
  EmptyClipboard();
  if (png && !png->empty()) {
    static const UINT cfPng = RegisterClipboardFormatW(L"PNG");
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, png->size());
    if (mem) {
      std::memcpy(GlobalLock(mem), png->data(), png->size());
      GlobalUnlock(mem);
      if (!SetClipboardData(cfPng, mem)) GlobalFree(mem);
    }
  }
  const bool alpha = img.HasTransparency();
  if (alpha) {
    if (HGLOBAL v5 = MakeDib(img, true, false)) {
      if (!SetClipboardData(CF_DIBV5, v5)) GlobalFree(v5);
    }
  }
  if (HGLOBAL dib = MakeDib(img, false, alpha)) {
    if (!SetClipboardData(CF_DIB, dib)) GlobalFree(dib);
  }
  CloseClipboard();
  return true;
}

}
