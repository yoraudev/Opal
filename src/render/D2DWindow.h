#pragma once
#include <windows.h>

#include <functional>
#include <vector>

#include "render/Gpu.h"
#include "ui/Ui.h"

namespace opal {

class D2DWindow {
 public:
  D2DWindow();
  virtual ~D2DWindow();
  D2DWindow(const D2DWindow&) = delete;
  D2DWindow& operator=(const D2DWindow&) = delete;

  HWND hwnd() const { return hwnd_; }
  bool Alive() const { return hwnd_ != nullptr; }
  float Scale() const { return float(dpi_) / 96.f; }
  UINT Dpi() const { return dpi_; }
  void Invalidate();
  void RenderNow();
  void Close();
  static double Now();
  static bool MicaSupported();

  static void SetDestroyedHandler(std::function<void(D2DWindow*)> fn);

 protected:
  struct CreateParams {
    DWORD exStyle = 0;
    DWORD style = WS_POPUP;
    const wchar_t* title = L"Opal";
    RECT rect{0, 0, 100, 100};
    HWND owner = nullptr;
    bool customFrame = false;
    bool mica = false;
  };
  bool Create(const CreateParams& params);

  virtual void OnRender(ID2D1DeviceContext5* dc) = 0;
  virtual bool WantsFrame() { return false; }
  virtual LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp, bool& handled);
  virtual LRESULT HitTestClient(POINT clientPx) { (void)clientPx; return HTCLIENT; }
  virtual HCURSOR Cursor() { return LoadCursorW(nullptr, IDC_ARROW); }
  virtual void OnDeviceLost() {}
  virtual void OnDestroyed() {}
  virtual void OnDpiChanged() {}
  virtual D2D1_COLOR_F ClearColor() { return {0, 0, 0, 0}; }

  void BeginUiFrame(ID2D1DeviceContext5* dc);
  void EndUiFrame(ID2D1DeviceContext5* dc);
  SIZE ClientSizePx() const { return size_; }
  D2D1_SIZE_F ClientSizeDip() const { return {size_.cx / Scale(), size_.cy / Scale()}; }
  void ApplyDarkFrame(bool dark);
  void AfterFrame(std::function<void()> fn);

  UiInput input_;
  Ui ui_;
  POINT mousePx_{-100000, -100000};
  bool mouseInside_ = false;

 private:
  static LRESULT CALLBACK WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);
  bool EnsureTarget();
  void ReleaseTarget();
  void Render();
  void UpdateModifiers();

  HWND hwnd_ = nullptr;
  UINT dpi_ = 96;
  bool customFrame_ = false;
  bool resizable_ = false;
  SIZE size_{0, 0};
  bool gpuRef_ = false;
  unsigned gen_ = 0;
  bool frameRequested_ = false;
  bool tracking_ = false;
  ComPtr<IDXGISwapChain1> swap_;
  ComPtr<IDCompositionTarget> target_;
  ComPtr<IDCompositionVisual> visual_;
  ComPtr<ID2D1DeviceContext5> dc_;
  ComPtr<ID2D1Bitmap1> backBitmap_;
  std::vector<std::function<void()>> afterFrame_;
};

void ExcludeFromCapture(HWND hwnd);

}
