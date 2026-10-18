#include "render/Gpu.h"

#include "core/Log.h"

namespace opal {

Gpu& Gpu::Get() {
  static Gpu g;
  return g;
}

void Gpu::AddRef() {
  ++users_;
  lastUse_ = GetTickCount64();
}

void Gpu::Release() {
  if (users_ > 0) --users_;
  lastUse_ = GetTickCount64();
}

void Gpu::TrimIfIdle(unsigned idleMs) {
  if (users_ == 0 && d3d_ && GetTickCount64() - lastUse_ > idleMs) {
    Log(L"GPU idle, releasing device");
    ReleaseDevice();
  }
}

ID2D1Factory6* Gpu::D2DFactory() {
  if (!d2dFactory_) {
    D2D1_FACTORY_OPTIONS opts{};
#ifdef _DEBUG
    opts.debugLevel = D2D1_DEBUG_LEVEL_WARNING;
#endif
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory6), &opts,
                                 reinterpret_cast<void**>(d2dFactory_.GetAddressOf())))) {
      opts.debugLevel = D2D1_DEBUG_LEVEL_NONE;
      D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory6), &opts,
                        reinterpret_cast<void**>(d2dFactory_.GetAddressOf()));
    }
  }
  return d2dFactory_.Get();
}

IDWriteFactory3* Gpu::DWrite() {
  if (!dwrite_) {
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3),
                        reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()));
  }
  return dwrite_.Get();
}

IWICImagingFactory* Gpu::Wic() {
  if (!wic_) {
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_));
  }
  return wic_.Get();
}

bool Gpu::Ensure() {
  lastUse_ = GetTickCount64();
  if (d3d_ && d2dDevice_ && dcomp_) {
    if (d3d_->GetDeviceRemovedReason() == S_OK) return true;
    HandleDeviceLost();
  }
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                      D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                                 ARRAYSIZE(levels), D3D11_SDK_VERSION, &d3d_, nullptr, &d3dCtx_);
  if (FAILED(hr)) {
    Log(L"Hardware D3D11 failed (0x%08X), using WARP", hr);
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, ARRAYSIZE(levels),
                           D3D11_SDK_VERSION, &d3d_, nullptr, &d3dCtx_);
    if (FAILED(hr)) return false;
  }
  d3d_.As(&dxgi_);
  hr = D2DFactory()->CreateDevice(dxgi_.Get(), d2dDevice_.GetAddressOf());
  if (FAILED(hr)) {
    Log(L"D2D device failed 0x%08X", hr);
    ReleaseDevice();
    return false;
  }
  hr = DCompositionCreateDevice(dxgi_.Get(), IID_PPV_ARGS(&dcomp_));
  if (FAILED(hr)) {
    Log(L"DComp device failed 0x%08X", hr);
    ReleaseDevice();
    return false;
  }
  ++generation_;
  Log(L"GPU device ready (generation %u)", generation_);
  return true;
}

void Gpu::HandleDeviceLost() {
  Log(L"GPU device lost");
  ReleaseDevice();
}

void Gpu::ReleaseDevice() {
  dcomp_.Reset();
  d2dDevice_.Reset();
  dxgi_.Reset();
  if (d3dCtx_) {
    d3dCtx_->ClearState();
    d3dCtx_->Flush();
  }
  d3dCtx_.Reset();
  d3d_.Reset();
  ++generation_;
}

void Gpu::Shutdown() {
  ReleaseDevice();
  d2dFactory_.Reset();
  dwrite_.Reset();
  wic_.Reset();
}

ComPtr<ID2D1DeviceContext5> Gpu::CreateContext() {
  ComPtr<ID2D1DeviceContext5> dc;
  if (!Ensure()) return dc;
  d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, dc.GetAddressOf());
  return dc;
}

}
