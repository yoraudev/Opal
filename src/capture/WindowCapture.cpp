#include "capture/WindowCapture.h"

#include <d3d11_4.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <cstring>

#include "core/Log.h"
#include "render/Gpu.h"

namespace opal {
namespace {

namespace wgc = winrt::Windows::Graphics::Capture;
namespace wdx = winrt::Windows::Graphics::DirectX;

bool CopyTextureToImage(ID3D11Texture2D* tex, Image* out) {
  Gpu& gpu = Gpu::Get();
  D3D11_TEXTURE2D_DESC desc;
  tex->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  ComPtr<ID3D11Texture2D> staging;
  if (FAILED(gpu.D3D()->CreateTexture2D(&desc, nullptr, &staging))) return false;
  gpu.D3DContext()->CopyResource(staging.Get(), tex);
  D3D11_MAPPED_SUBRESOURCE map{};
  if (FAILED(gpu.D3DContext()->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return false;
  *out = Image(int(desc.Width), int(desc.Height));
  for (UINT y = 0; y < desc.Height; ++y) {
    std::memcpy(out->Row(int(y)), static_cast<const uint8_t*>(map.pData) + size_t(y) * map.RowPitch,
                size_t(desc.Width) * 4);
  }
  gpu.D3DContext()->Unmap(staging.Get(), 0);
  return true;
}

}

bool CaptureWindowContent(const WindowInfo& w, Image* out) {
  try {
    if (!wgc::GraphicsCaptureSession::IsSupported()) return false;
    Gpu& gpu = Gpu::Get();
    if (!gpu.Ensure()) return false;
    {
      ComPtr<ID3D11Multithread> mt;
      if (SUCCEEDED(gpu.D3D()->QueryInterface(IID_PPV_ARGS(&mt)))) mt->SetMultithreadProtected(TRUE);
    }

    auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    wgc::GraphicsCaptureItem item{nullptr};
    winrt::check_hresult(interop->CreateForWindow(w.hwnd, winrt::guid_of<wgc::GraphicsCaptureItem>(),
                                                  winrt::put_abi(item)));
    winrt::com_ptr<::IInspectable> inspectable;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(gpu.Dxgi(), inspectable.put()));
    auto device = inspectable.as<wdx::Direct3D11::IDirect3DDevice>();

    const auto size = item.Size();
    auto pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
        device, wdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, size);
    auto session = pool.CreateCaptureSession(item);
    try {
      session.IsCursorCaptureEnabled(false);
    } catch (...) {
    }
    if (winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(
            L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired")) {
      try {
        session.IsBorderRequired(false);
      } catch (...) {
      }
    }

    HANDLE arrived = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    wgc::Direct3D11CaptureFrame frame{nullptr};
    auto token = pool.FrameArrived([&](const wgc::Direct3D11CaptureFramePool& sender, auto&&) {
      auto f = sender.TryGetNextFrame();
      if (f && !frame) {
        frame = f;
        SetEvent(arrived);
      }
    });
    session.StartCapture();
    const DWORD wait = WaitForSingleObject(arrived, 1500);
    pool.FrameArrived(token);
    CloseHandle(arrived);
    if (wait != WAIT_OBJECT_0 || !frame) {
      session.Close();
      pool.Close();
      Log(L"WGC: no frame for window %p", w.hwnd);
      return false;
    }

    auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    ComPtr<ID3D11Texture2D> tex;
    winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&tex)));
    Image full;
    const bool ok = CopyTextureToImage(tex.Get(), &full);
    const auto content = frame.ContentSize();
    frame.Close();
    session.Close();
    pool.Close();
    if (!ok) return false;

    RectI contentRect{0, 0, std::min(full.width, content.Width), std::min(full.height, content.Height)};
    RectI crop = contentRect;
    if (contentRect.Width() == w.window.Width() && contentRect.Height() == w.window.Height() &&
        !(w.window == w.frame)) {
      crop = w.frame.Offset(-w.window.left, -w.window.top);
    } else if (contentRect.Width() > w.frame.Width() || contentRect.Height() > w.frame.Height()) {
      const int dx = (contentRect.Width() - w.frame.Width()) / 2;
      const int dy = (contentRect.Height() - w.frame.Height()) / 2;
      crop = RectI::FromXYWH(std::max(0, dx), std::max(0, dy), w.frame.Width(), w.frame.Height());
    }
    *out = full.Crop(crop.Intersect(contentRect));
    for (auto& p : out->pixels) p |= 0xFF000000u;
    Log(L"WGC: captured %dx%d (content %dx%d, frame %dx%d)", out->width, out->height, content.Width,
        content.Height, w.frame.Width(), w.frame.Height());
    return !out->Empty();
  } catch (const winrt::hresult_error& e) {
    Log(L"WGC failed: 0x%08X %s", uint32_t(e.code()), e.message().c_str());
    return false;
  } catch (...) {
    Log(L"WGC failed (unknown)");
    return false;
  }
}

}
