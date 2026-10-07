#pragma once
#include "model.h"
#include <d2d1_1.h>
#include <dwrite.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <map>
namespace tempos {
using Microsoft::WRL::ComPtr;
struct RasterSurface {
  HDC dc{};
  HBITMAP bitmap{};
  HGDIOBJ previous{};
  ~RasterSurface() {
    if (dc && previous)
      SelectObject(dc, previous);
    if (bitmap)
      DeleteObject(bitmap);
    if (dc)
      DeleteDC(dc);
  }
};
struct RenderSurface {
  std::unique_ptr<RasterSurface> raster;
  ComPtr<IDXGISwapChain1> chain;
  ComPtr<IDCompositionTarget> target;
  ComPtr<IDCompositionVisual> visual;
  ComPtr<IDCompositionEffectGroup> opacity;
  ComPtr<ID2D1Bitmap1> bitmap;
  UINT width{}, height{};
};
class Renderer {
public:
  explicit Renderer(bool composition = false);
  bool ready() const { return bool(context_); }
  bool software() const { return software_; }
  bool layered() const { return raster_; }
  bool draw(HWND, RenderSurface &, const Widget &, const Snapshot *, int theme, bool edit, UINT dpi);

private:
  bool surface(HWND, RenderSurface &);
  void text(const std::wstring &, float x, float y, float width, float height, float size = 16,
            bool bold = false, uint32_t color = 0xffffff,
            DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING);
  void line(float x, float y, float x2, float y2, uint32_t color = 0xffffff, float alpha = .25f,
            float stroke = 1);
  void dot(float x, float y, float radius, uint32_t color, float alpha = 1);
  void bar(float x, float y, float width, float height, uint32_t color, float alpha = 1, float radius = 4);
  void graph(const History &, float x, float y, float width, float height, uint32_t color, double max = 100);
  void calendar(const Widget &, const Snapshot *, Extent);
  void weather(const Widget &, const Snapshot *, Extent);
  ComPtr<ID3D11Device> d3d_;
  ComPtr<ID3D11DeviceContext> d3dContext_;
  ComPtr<IDXGIDevice> dxgi_;
  ComPtr<IDXGIFactory2> factory_;
  ComPtr<ID2D1Factory1> d2d_;
  ComPtr<ID2D1Device> device_;
  ComPtr<ID2D1RenderTarget> context_;
  ComPtr<ID2D1DeviceContext> gpuContext_;
  ComPtr<ID2D1DCRenderTarget> dcContext_;
  ComPtr<IDCompositionDevice> composition_;
  ComPtr<IDWriteFactory> write_;
  ComPtr<ID2D1SolidColorBrush> brush_;
  std::map<std::pair<int, bool>, ComPtr<IDWriteTextFormat>> fonts_;
  bool software_ = false;
  bool raster_ = false;
  bool highContrast_ = false;
  uint32_t foreground_ = 0xffffff;
};
std::wstring widgetSummary(const Widget &, const Snapshot *);
} // namespace tempos
