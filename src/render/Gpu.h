#pragma once
#include <d2d1_3.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite_3.h>
#include <dxgi1_3.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace opal {

using Microsoft::WRL::ComPtr;

class Gpu {
 public:
  static Gpu& Get();

  void AddRef();
  void Release();
  void TrimIfIdle(unsigned idleMs);

  bool Ensure();
  void HandleDeviceLost();
  unsigned Generation() const { return generation_; }

  ID3D11Device* D3D() const { return d3d_.Get(); }
  ID3D11DeviceContext* D3DContext() const { return d3dCtx_.Get(); }
  IDXGIDevice* Dxgi() const { return dxgi_.Get(); }
  ID2D1Factory6* D2DFactory();
  ID2D1Device5* D2DDevice() const { return d2dDevice_.Get(); }
  IDCompositionDevice* DComp() const { return dcomp_.Get(); }
  IDWriteFactory3* DWrite();
  IWICImagingFactory* Wic();

  ComPtr<ID2D1DeviceContext5> CreateContext();

 private:
  Gpu() = default;
  void ReleaseDevice();

  ComPtr<ID3D11Device> d3d_;
  ComPtr<ID3D11DeviceContext> d3dCtx_;
  ComPtr<IDXGIDevice> dxgi_;
  ComPtr<ID2D1Factory6> d2dFactory_;
  ComPtr<ID2D1Device5> d2dDevice_;
  ComPtr<IDCompositionDevice> dcomp_;
  ComPtr<IDWriteFactory3> dwrite_;
  ComPtr<IWICImagingFactory> wic_;
  int users_ = 0;
  unsigned long long lastUse_ = 0;
  unsigned generation_ = 0;
};

}
