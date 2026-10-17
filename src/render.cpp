#include "render.h"
#include "design.h"
#include <d2d1_1helper.h>
#include <cstdio>
namespace tempos {
using namespace std::chrono;
static D2D1_COLOR_F color(uint32_t rgb, float alpha = 1) {
  return D2D1::ColorF(rgb, alpha);
}
Renderer::Renderer(bool composition) {
  raster_ = !composition;
  D2D1_FACTORY_OPTIONS options{};
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options,
                               reinterpret_cast<void **>(d2d_.GetAddressOf()))))
    return;
  DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                      reinterpret_cast<IUnknown **>(write_.GetAddressOf()));
  if (raster_) {
    software_ = true;
    auto properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(d2d_->CreateDCRenderTarget(&properties, &dcContext_)))
      return;
    dcContext_.As(&context_);
    context_->CreateSolidColorBrush(color(0xffffff), &brush_);
    if (!write_ || !brush_)
      context_.Reset();
    return;
  }
  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  D3D_FEATURE_LEVEL level;
  auto hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                              D3D11_SDK_VERSION, &d3d_, &level, &d3dContext_);
  if (FAILED(hr)) {
    software_ = true;
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
                           &d3d_, &level, &d3dContext_);
  }
  if (FAILED(hr))
    return;
  d3d_.As(&dxgi_);
  CreateDXGIFactory1(IID_PPV_ARGS(&factory_));
  if (FAILED(d2d_->CreateDevice(dxgi_.Get(), &device_)) ||
      FAILED(device_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &gpuContext_)))
    return;
  gpuContext_.As(&context_);
  DCompositionCreateDevice(dxgi_.Get(), IID_PPV_ARGS(&composition_));
  DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                      reinterpret_cast<IUnknown **>(write_.GetAddressOf()));
  context_->CreateSolidColorBrush(color(0xffffff), &brush_);
  if (!composition_ || !write_ || !brush_)
    context_.Reset();
}
bool Renderer::surface(HWND hwnd, RenderSurface &s) {
  RECT r{};
  GetClientRect(hwnd, &r);
  UINT width = std::max<LONG>(1, r.right), height = std::max<LONG>(1, r.bottom);
  if (raster_) {
    if (!s.raster || s.width != width || s.height != height) {
      s.raster = std::make_unique<RasterSurface>();
      auto &resource = *s.raster;
      resource.dc = CreateCompatibleDC(nullptr);
      BITMAPINFO info{};
      info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      info.bmiHeader.biWidth = LONG(width);
      info.bmiHeader.biHeight = -LONG(height);
      info.bmiHeader.biPlanes = 1;
      info.bmiHeader.biBitCount = 32;
      info.bmiHeader.biCompression = BI_RGB;
      void *pixels = nullptr;
      resource.bitmap = CreateDIBSection(resource.dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
      if (!resource.dc || !resource.bitmap)
        return false;
      resource.previous = SelectObject(resource.dc, resource.bitmap);
      s.width = width;
      s.height = height;
    }
    return SUCCEEDED(dcContext_->BindDC(s.raster->dc, &r));
  }
  if (s.chain && s.width == width && s.height == height)
    return true;
  gpuContext_->SetTarget(nullptr);
  s.bitmap.Reset();
  if (s.chain) {
    if (FAILED(s.chain->ResizeBuffers(2, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0)))
      return false;
  } else {
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    if (FAILED(factory_->CreateSwapChainForComposition(d3d_.Get(), &desc, nullptr, &s.chain)))
      return false;
    if (FAILED(composition_->CreateTargetForHwnd(hwnd, TRUE, &s.target)) ||
        FAILED(composition_->CreateVisual(&s.visual)))
      return false;
    composition_->CreateEffectGroup(&s.opacity);
    s.visual->SetEffect(s.opacity.Get());
    s.visual->SetContent(s.chain.Get());
    s.target->SetRoot(s.visual.Get());
  }
  ComPtr<IDXGISurface> buffer;
  if (FAILED(s.chain->GetBuffer(0, IID_PPV_ARGS(&buffer))))
    return false;
  auto properties =
      D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                              D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  if (FAILED(gpuContext_->CreateBitmapFromDxgiSurface(buffer.Get(), &properties, &s.bitmap)))
    return false;
  s.width = width;
  s.height = height;
  return true;
}
void Renderer::text(const std::wstring &value, float x, float y, float w, float h, float size, bool bold,
                    uint32_t rgb, DWRITE_TEXT_ALIGNMENT align, bool wrap) {
  if (highContrast_)
    rgb = foreground_;
  if (value.empty() || w <= 0 || h <= 0)
    return;
  auto key = std::pair{int(size * 10), bold};
  auto it = fonts_.find(key);
  if (it == fonts_.end()) {
    ComPtr<IDWriteTextFormat> font;
    if (FAILED(write_->CreateTextFormat(
            L"Segoe UI", nullptr, bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"ko-KR", &font)))
      return;
    font->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    ComPtr<IDWriteInlineObject> ellipsis;
    write_->CreateEllipsisTrimmingSign(font.Get(), &ellipsis);
    DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    font->SetTrimming(&trimming, ellipsis.Get());
    it = fonts_.emplace(key, std::move(font)).first;
  }
  it->second->SetTextAlignment(align);
  it->second->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
  brush_->SetColor(color(rgb));
  context_->DrawTextW(value.data(), UINT32(value.size()), it->second.Get(), D2D1::RectF(x, y, x + w, y + h),
                      brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
void Renderer::line(float x, float y, float x2, float y2, uint32_t rgb, float alpha, float stroke) {
  brush_->SetColor(color(rgb, alpha));
  context_->DrawLine({x, y}, {x2, y2}, brush_.Get(), stroke);
}
void Renderer::modelText(const std::wstring &value, float x, float y, float width, float height, float size) {
  // Measure the complete single line before choosing its font; only exceptionally long
  // names use the normal ellipsis, never the old half-card text box.
  while (size > 11) {
    ComPtr<IDWriteTextFormat> format;
    ComPtr<IDWriteTextLayout> layout;
    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(write_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                       DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                       size, L"ko-KR", &format)))
      break;
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    if (FAILED(write_->CreateTextLayout(value.c_str(), UINT32(value.size()), format.Get(),
                                       width, height, &layout)) ||
        FAILED(layout->GetMetrics(&metrics)) || metrics.widthIncludingTrailingWhitespace <= width)
      break;
    size -= .5f;
  }
  text(value, x, y, width, height, size);
}
void Renderer::dot(float x, float y, float radius, uint32_t rgb, float alpha) {
  brush_->SetColor(color(rgb, alpha));
  context_->FillEllipse(D2D1::Ellipse({x, y}, radius, radius), brush_.Get());
}
void Renderer::bar(float x, float y, float w, float h, uint32_t rgb, float alpha, float radius) {
  brush_->SetColor(color(rgb, alpha));
  context_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), radius, radius),
                                 brush_.Get());
}
void Renderer::graph(const History &history, float x, float y, float w, float h, uint32_t rgb, double max) {
  double now = monotonic();
  if (max <= 0 || !std::isfinite(max))
    max = 1;
  bool previous = false;
  D2D1_POINT_2F last{};
  for (auto &s : history.samples()) {
    if (!s.valid || !std::isfinite(s.value)) {
      previous = false;
      continue;
    }
    float px = x + float(std::clamp((s.end - now + 60) / 60, 0., 1.)) * w,
          py = y + h - float(std::clamp(s.value / max, 0., 1.)) * h;
    if (previous)
      line(last.x, last.y, px, py, rgb, .95f, design::graphStroke);
    last = {px, py};
    previous = true;
  }
  line(x, y + h, x + w, y + h, rgb, .2f);
}
static std::wstring timeLabel(int64_t t, const Widget &w, bool seconds = false, bool includePeriod = true) {
  auto local = localTime(t, w.timezone);
  hh_mm_ss tod{local - floor<days>(local)};
  int hour = int(tod.hours().count());
  std::wstring prefix;
  if (w.twelveHour) {
    if (includePeriod)
      prefix = hour < 12 ? L"오전 " : L"오후 ";
    hour = hour % 12 ? hour % 12 : 12;
  }
  wchar_t b[40]{};
  if (seconds)
    swprintf_s(b, L"%02d:%02d:%02d", hour, int(tod.minutes().count()), int(tod.seconds().count()));
  else
    swprintf_s(b, L"%02d:%02d", hour, int(tod.minutes().count()));
  return prefix + b;
}
static std::wstring dateLabel(year_month_day d) {
  return std::to_wstring(int(d.year())) + L"년 " + std::to_wstring(unsigned(d.month())) + L"월 " +
         std::to_wstring(unsigned(d.day())) + L"일";
}
static std::wstring eventLabel(const Event &e, const Widget &w) {
  return e.allDay ? L"종일  " + e.title : timeLabel(e.start, w) + L"  " + e.title;
}
void Renderer::icon(Kind kind, float x, float y, float size) {
  const float scale = size / 24;
  auto stroke = [&](float a, float b, float c, float d) {
    line(x + a * scale, y + b * scale, x + c * scale, y + d * scale, highContrast_ ? foreground_ : 0xffffff,
         1, design::iconStroke);
  };
  auto rect = [&](float a, float b, float c, float d) {
    stroke(a, b, c, b);
    stroke(c, b, c, d);
    stroke(c, d, a, d);
    stroke(a, d, a, b);
  };
  switch (kind) {
  case Kind::Clock:
    rect(1, 5, 23, 19);
    stroke(5, 9, 8, 9);
    stroke(8, 9, 8, 15);
    stroke(5, 15, 8, 15);
    stroke(16, 9, 19, 9);
    stroke(19, 9, 19, 15);
    stroke(16, 15, 19, 15);
    stroke(12, 10, 12, 11);
    stroke(12, 13, 12, 14);
    break;
  case Kind::Cpu:
    rect(5, 5, 19, 19);
    rect(9, 9, 15, 15);
    for (float a : {8.f, 12.f, 16.f}) {
      stroke(a, 1, a, 5);
      stroke(a, 19, a, 23);
      stroke(1, a, 5, a);
      stroke(19, a, 23, a);
    }
    break;
  case Kind::Gpu:
    rect(3, 5, 22, 18);
    stroke(1, 3, 1, 21);
    stroke(7, 18, 7, 21);
    stroke(17, 18, 17, 21);
    rect(7, 9, 12, 14);
    stroke(16, 9, 19, 9);
    stroke(16, 13, 19, 13);
    break;
  case Kind::Memory:
    rect(1, 6, 23, 18);
    rect(5, 9, 9, 14);
    rect(14, 9, 18, 14);
    for (float a : {5.f, 9.f, 15.f, 19.f})
      stroke(a, 18, a, 22);
    break;
  case Kind::System:
    rect(1, 3, 23, 18);
    stroke(12, 18, 12, 22);
    stroke(7, 22, 17, 22);
    break;
  case Kind::Network:
    stroke(12, 2, 12, 7);
    stroke(5, 7, 19, 7);
    stroke(5, 7, 5, 15);
    stroke(19, 7, 19, 15);
    rect(1, 15, 9, 21);
    rect(15, 15, 23, 21);
    break;
  case Kind::Disk:
    stroke(5, 3, 19, 3);
    stroke(5, 3, 1, 14);
    stroke(19, 3, 23, 14);
    rect(1, 14, 23, 21);
    stroke(17, 17, 19, 17);
    break;
  case Kind::Calendar:
    rect(2, 4, 22, 22);
    stroke(2, 10, 22, 10);
    stroke(7, 1, 7, 7);
    stroke(17, 1, 17, 7);
    stroke(7, 15, 10, 15);
    stroke(14, 15, 17, 15);
    break;
  default:
    break;
  }
}
void Renderer::heading(const Widget &w, const std::wstring &title) {
  const bool slim = w.size == Size::Slim;
  icon(w.kind, slim ? 17.f : 20.f, slim ? 26.f : 20.f, design::iconSize);
  text(title, slim ? 47.f : design::headerTextX, slim ? 12.f : design::headerY,
       slim ? 113.f : extent(w.size).width - design::headerTextX - 20, 25,
       slim || w.size == Size::S ? design::compactTitleSize : design::titleSize, true);
}
void Renderer::weatherIcon(WeatherState state, bool night, float x, float y, float scale) {
  int sky = state == WeatherState::Clear     ? 1
            : state == WeatherState::Cloudy  ? 3
            : state == WeatherState::Unknown ? 0
                                             : 4;
  int rain = state == WeatherState::Snow ? 3
             : state == WeatherState::Rain || state == WeatherState::Sleet ||
                     state == WeatherState::Showers || state == WeatherState::Drizzle ||
                     state == WeatherState::Thunder
                 ? 1
                 : 0;
  auto circle = [&](float dx, float dy, float r, uint32_t rgb) {
    dot(x + dx * scale, y + dy * scale, r * scale, rgb);
  };
  if (rain > 0 || sky >= 3) {
    if (night)
      weatherIcon(WeatherState::Clear, true, x + 19 * scale, y - 20 * scale, .6f * scale);
    circle(-10, 0, 14, 0xf2f4ff);
    circle(7, -4, 19, 0xf2f4ff);
    bar(x - 26 * scale, y, 55 * scale, 15 * scale, 0xf2f4ff, 1, 8 * scale);
    if (state == WeatherState::Fog)
      for (int i = 0; i < 3; ++i)
        line(x - 25 * scale, y + (21 + i * 7) * scale, x + 25 * scale, y + (21 + i * 7) * scale, 0xdce5ed,
             .9f, 2 * scale);
    if (state == WeatherState::Thunder) {
      line(x + 4 * scale, y + 13 * scale, x - 5 * scale, y + 27 * scale, 0xffda67, 1, 4 * scale);
      line(x - 5 * scale, y + 27 * scale, x + 4 * scale, y + 27 * scale, 0xffda67, 1, 4 * scale);
      line(x + 4 * scale, y + 27 * scale, x - 5 * scale, y + 41 * scale, 0xffda67, 1, 4 * scale);
    }
    if (rain > 0 && state != WeatherState::Thunder)
      for (int i = -1; i <= 1; ++i)
        if (rain == 3 || rain == 7)
          circle(float(i * 15), 27, 3, 0xffffff);
        else
          line(x + i * 14 * scale, y + 23 * scale, x + (i * 14 - 5) * scale, y + 33 * scale, 0xbbe9ff, 1,
               2 * scale);
  } else if (sky != 1) {
    text(L"—", x - 15 * scale, y - 16 * scale, 32 * scale, 32 * scale, 24 * scale);
  } else if (night) {
    // A crescent geometry avoids painting a false dark disk over a fixed theme.
    ComPtr<ID2D1PathGeometry> moon;
    if (SUCCEEDED(d2d_->CreatePathGeometry(&moon))) {
      ComPtr<ID2D1GeometrySink> sink;
      moon->Open(&sink);
      if (sink) {
        sink->BeginFigure({x + 8 * scale, y - 21 * scale}, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddBezier({{x - 31 * scale, y - 25 * scale},
                         {x - 26 * scale, y + 32 * scale},
                         {x + 20 * scale, y + 15 * scale}});
        sink->AddBezier({{x - 5 * scale, y + 15 * scale},
                         {x - 10 * scale, y - 6 * scale},
                         {x + 8 * scale, y - 21 * scale}});
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        brush_->SetColor(color(0xfff2c7));
        context_->FillGeometry(moon.Get(), brush_.Get());
      }
    }
  } else {
    circle(0, 0, 20, 0xffda67);
    for (int i = 0; i < 8; ++i) {
      float a = float(i) * 3.14159265f / 4;
      line(x + std::cos(a) * 26 * scale, y + std::sin(a) * 26 * scale, x + std::cos(a) * 31 * scale,
           y + std::sin(a) * 31 * scale, 0xffeaa6, 1, 2 * scale);
    }
  }
}
void Renderer::weatherBackdrop(const WeatherData &weather, Extent e, bool sunset) {
  const auto state = weather.state;
  if (state == WeatherState::Unknown)
    return;
  // Static vector scenery; the enclosing card layer supplies clipping and user opacity.
  auto ellipse = [&](float x, float y, float rx, float ry, uint32_t rgb, float opacity) {
    brush_->SetColor(color(rgb, opacity));
    context_->FillEllipse(D2D1::Ellipse({x * e.width, y * e.height}, rx * e.width, ry * e.height),
                          brush_.Get());
  };
  auto bank = [&](float x, float y, float scale, uint32_t rgb, float opacity) {
    ComPtr<ID2D1PathGeometry> shape;
    if (FAILED(d2d_->CreatePathGeometry(&shape)))
      return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(shape->Open(&sink)))
      return;
    auto p = [&](float dx, float dy) {
      return D2D1::Point2F((x + dx * scale) * e.width, (y + dy * scale) * e.height);
    };
    sink->BeginFigure(p(-.42f, .2f), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddBezier({p(-.48f, -.02f), p(-.26f, -.14f), p(-.18f, -.04f)});
    sink->AddBezier({p(-.14f, -.3f), p(.12f, -.3f), p(.2f, -.09f)});
    sink->AddBezier({p(.39f, -.22f), p(.53f, .04f), p(.43f, .2f)});
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (SUCCEEDED(sink->Close())) {
      brush_->SetColor(color(rgb, opacity));
      context_->FillGeometry(shape.Get(), brush_.Get());
    }
  };
  auto ribbon = [&](float y, float slope, uint32_t rgb, float opacity) {
    ComPtr<ID2D1PathGeometry> shape;
    if (FAILED(d2d_->CreatePathGeometry(&shape)))
      return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(shape->Open(&sink)))
      return;
    sink->BeginFigure({0, e.height * y}, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddBezier({{e.width * .35f, e.height * (y + .1f)},
                     {e.width * .65f, e.height * (y - slope)},
                     {e.width, e.height * (y - slope)}});
    sink->AddLine({e.width, e.height * (y - slope + .06f)});
    sink->AddBezier({{e.width * .65f, e.height * (y - slope + .03f)},
                     {e.width * .35f, e.height * (y + .14f)},
                     {0, e.height * (y + .07f)}});
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (SUCCEEDED(sink->Close())) {
      brush_->SetColor(color(rgb, opacity));
      context_->FillGeometry(shape.Get(), brush_.Get());
    }
  };
  if (sunset) {
    ellipse(.86f, .8f, .16f, .2f, 0xffcb83, .22f);
    ribbon(.92f, .16f, 0xbab8e1, .15f);
    ribbon(1.03f, .1f, 0x252952, .3f);
  } else if (state == WeatherState::Clear) {
    if (weather.night) {
      for (auto p : {D2D1::Point2F(.62f, .19f), D2D1::Point2F(.89f, .26f), D2D1::Point2F(.7f, .43f),
                     D2D1::Point2F(.94f, .54f)}) {
        float x = p.x * e.width, y = p.y * e.height;
        line(x - 2, y, x + 2, y, 0xffffff, .55f);
        line(x, y - 2, x, y + 2, 0xffffff, .55f);
      }
      bank(.8f, 1.06f, .9f, 0xa8bfea, .1f);
    } else {
      ribbon(.98f, .24f, 0xbbddf4, .16f);
      ribbon(.88f, .31f, 0xd8edfc, .09f);
    }
  } else if (state == WeatherState::Fog) {
    for (int i = 0; i < 4; ++i)
      bar(e.width * (.57f + float(i % 2) * .08f), e.height * (.22f + float(i) * .19f),
          e.width * (.34f - float(i % 2) * .07f), 5, 0xdce6ed, .18f, 3);
    ribbon(.98f, .15f, 0xd4dee7, .15f);
  } else {
    bank(.93f, .26f, .8f, weather.night ? 0x16243e : 0xc4d9e9, .14f);
    bank(.83f, .95f, 1.f, 0xabc6df, .15f);
    bank(.13f, 1.12f, .8f, 0xc0d7ed, .08f);
    const bool snow = state == WeatherState::Snow || state == WeatherState::Sleet;
    const bool rain = state == WeatherState::Rain || state == WeatherState::Drizzle ||
                      state == WeatherState::Showers || state == WeatherState::Sleet ||
                      state == WeatherState::Thunder;
    if (snow || rain)
      for (int i = 0; i < 9; ++i) {
        float x = e.width * (.53f + float(i % 3) * .17f),
              y = e.height * (.36f + float(i / 3) * .26f + float(i % 2) * .05f);
        if (snow && (!rain || i % 2 == 0)) {
          line(x - 2, y, x + 2, y, 0xffffff, .24f);
          line(x, y - 2, x, y + 2, 0xffffff, .24f);
        } else
          line(x, y, x - 4, y + 9, 0xc1e4ff, .2f, 1.5f);
      }
  }
}
std::wstring widgetSummary(const Widget &w, const Snapshot *s) {
  std::wstring out = std::wstring(kindName(w.kind));
  if (w.kind == Kind::Clock)
    return out + L" " + timeLabel(nowUnix(), w, w.seconds) + L" " +
           dateLabel(localDate(nowUnix(), w.timezone));
  if (w.kind == Kind::Calendar) {
    out += L" " + dateLabel(localDate(nowUnix(), w.timezone));
    if (s && s->calendar) {
      size_t count = 0;
      for (auto &e : s->calendar->events) {
        if (!eventOnDate(e, localDate(nowUnix(), w.timezone), w.timezone))
          continue;
        out += L" · " + eventLabel(e, w);
        if (++count == 10) {
          out += L" · 추가 일정 있음";
          break;
        }
      }
    }
    return out;
  }
  if (!s)
    return out + L" 불러오는 중";
  if (w.kind == Kind::Weather) {
    out += L" " + w.region;
    if (s->weather) {
      double temperature = w.fahrenheit ? s->weather->temperature * 1.8 + 32 : s->weather->temperature;
      out += L" " + number(temperature) + (w.fahrenheit ? L"°F " : L"°C ") + s->weather->condition;
      if (w.size == Size::L) {
        out += L" · 앞으로 4일";
        for (const auto &day : s->weather->days) {
          auto degrees = [&](double value) { return number(w.fahrenheit ? value * 1.8 + 32 : value); };
          out += L" · " + dateLabel(localDate(day.time, L"Asia/Seoul")) + L" 최고 " + degrees(day.high) +
                 L"° 최저 " + degrees(day.low) + L"° 강수 " + number(day.probability) + L"%";
        }
      }
    }
  } else if (w.kind == Kind::Disk && w.size == Size::L) {
    out += L" 전체 " + std::to_wstring(s->disks.size()) + L"개 드라이브";
    for (const auto &volume : s->disks)
      out += L" · " + volume.name + L" 사용 " + formatBytes(volume.total - volume.available, w.binaryDisk) +
             L" 전체 " + formatBytes(volume.total, w.binaryDisk) + L" 여유 " +
             formatBytes(volume.freeForUser, w.binaryDisk);
  } else if (w.kind == Kind::System) {
    if (w.systemFields[0])
      out += L" " + s->title;
    if (w.systemFields[1])
      out += L" " + s->detail;
    auto separator = s->extra.find(L'\n');
    if (w.systemFields[2])
      out += L" CPU " + s->extra.substr(0, separator);
    if (w.systemFields[3] && separator != std::wstring::npos)
      out += L" GPU " + s->extra.substr(separator + 1);
    if (w.systemFields[4])
      out += L" 가동 " + number(s->value) + L" 초";
  } else if (w.kind == Kind::Network && std::isfinite(s->value)) {
    auto rate = [&](double value) {
      return w.networkBytes ? formatBytes(value) + L"/s" : number(value * 8 / 1e6, 2) + L" Mbps";
    };
    out += L" 수신 " + rate(s->value) + L" 송신 " + rate(s->secondary);
  } else if (std::isfinite(s->value)) {
    out += L" " + number(s->value) + (w.kind == Kind::System ? L" 초" : L"%");
    if (w.kind == Kind::Disk)
      out += L" 사용자 가용 " + formatBytes(s->secondary, w.binaryDisk);
  }
  if (w.kind == Kind::Cpu || w.kind == Kind::Gpu) {
    out += L" " + s->title;
    if (validTemperature(s->temperature, monotonic(), w.interval))
      out += L" 온도 " + number(s->temperature.celsius) + L"°C";
    else
      out += L" 온도 — · " + temperatureReason(s->temperature.status == TemperatureStatus::Ready
                                                   ? TemperatureStatus::Stale : s->temperature.status);
  }
  out += L" " + s->message;
  return out;
}
void Renderer::weather(const Widget &w, const Snapshot *s, Extent e) {
  auto d = s ? s->weather : nullptr;
  float pad = w.size == Size::Slim ? 16 : 20;
  bool stale = s && (s->status == Status::Stale ||
                     (d && nowUnix() - (d->observed > 0 ? d->observed : d->fetched) > 7200));
  std::wstring place = w.region;
  if (auto p = place.find_last_of(L' '); p != std::wstring::npos)
    place = place.substr(p + 1);
  text(place, pad, w.size == Size::Slim ? 8 : 16,
       w.size == Size::S ? 80.f : w.size == Size::Slim ? 92.f : e.width - 2 * pad - (stale ? 70.f : 0.f), 24,
       w.size == Size::Slim ? 13 : 15);
  if (!d || !std::isfinite(d->temperature)) {
    text(s ? s->message : L"날씨 불러오는 중", pad, w.size == Size::Slim ? 28 : 57, e.width - 2 * pad,
         w.size == Size::Slim ? 24 : 68, w.size == Size::Slim ? 12 : 15);
    return;
  }
  double temperature = w.fahrenheit ? d->temperature * 1.8 + 32 : d->temperature;
  float size = w.size == Size::Slim ? 28 : w.size == Size::S ? 42 : 52;
  const auto degrees = number(temperature) + L"°";
  float temperatureWidth = w.size == Size::Slim ? 90.f : w.size == Size::S ? 120.f :
                           degrees.size() >= 4 ? 146.f : 110.f;
  text(degrees, w.size == Size::Slim ? 115.f : pad, w.size == Size::Slim ? 14.f : 43,
       temperatureWidth, 64, size, true);
  float iconX = w.size == Size::S ? 121.f : e.width - 52, iconY = w.size == Size::S      ? 30.f
                                                                  : w.size == Size::Slim ? 30.f
                                                                                         : 65.f;
  weatherIcon(d->state, d->night, iconX, iconY, w.size == Size::S ? .5f : w.size == Size::Slim ? .7f : 1.f);
  if (w.size == Size::Slim) {
    text(d->condition, pad, 34, 92, 20, 12);
  } else {
    if (w.size == Size::S)
      text(d->condition, pad, 112, 120, 23, 13);
    else
      text(d->condition, pad, 120, 93, 22, 13);
    if (w.size != Size::S) {
      text(L"최저 " + number(w.fahrenheit ? d->low * 1.8 + 32 : d->low) + L"°   최고 " +
               number(w.fahrenheit ? d->high * 1.8 + 32 : d->high) + L"°",
           117, 120, e.width - pad - 117, 22, 13, false, 0xffffff, DWRITE_TEXT_ALIGNMENT_TRAILING);
      if (w.size == Size::L) {
        line(pad, 147, e.width - pad, 147);
        const auto today = localMidnight(localDate(nowUnix(), L"Asia/Seoul"), L"Asia/Seoul");
        const wchar_t *weekdays[]{L"일", L"월", L"화", L"수", L"목", L"금", L"토"};
        for (size_t i = 0; i < 4; ++i) {
          float x = pad + float(i) * (e.width - 2 * pad) / 4;
          const auto time = today + (int64_t(i) + 1) * 86400;
          const auto date = localDate(time, L"Asia/Seoul");
          auto label = i == 0 ? std::wstring(L"내일") :
              std::to_wstring(unsigned(date.month())) + L"/" + std::to_wstring(unsigned(date.day()));
          label += L" (" + std::wstring(weekdays[std::chrono::weekday{sys_days{date}}.c_encoding()]) + L")";
          text(label, x, 167, 70, 20, 11);
          const auto found = std::find_if(d->days.begin(), d->days.end(),
                                         [time](const auto &day) { return day.time == time; });
          if (found == d->days.end()) {
            text(L"—", x, 205, 70, 25, 18);
            text(L"예보 없음", x, 265, 70, 20, 11);
            continue;
          }
          const auto &day = *found;
          weatherIcon(day.state, false, x + 28, 205, .38f);
          auto forecastDegrees = [&](double value) { return number(w.fahrenheit ? value * 1.8 + 32 : value) + L"°"; };
          text(L"최고 " + forecastDegrees(day.high), x, 237, 70, 24, 14, true);
          text(L"최저 " + forecastDegrees(day.low), x, 266, 70, 20, 12);
          text(L"강수 " + (std::isfinite(day.probability) ? number(day.probability) + L"%" : L"—"),
               x, 295, 70, 20, 11);
        }
      }
    }
  }
  if (stale)
    text(L"갱신 지연", e.width - 77, w.size == Size::Slim ? 8 : 16, 66, 20, 10);
}
void Renderer::calendar(const Widget &w, const Snapshot *s, Extent e) {
  auto today = localDate(nowUnix(), w.timezone);
  auto d = s ? s->calendar : nullptr;
  auto emptyLabel = !d ? L"일정 불러오는 중" : !d->connected ? L"설정에서 일정 연결" : L"일정 없음";
  if (w.size != Size::L && w.size != Size::XL)
    heading(w, L"캘린더");
  std::vector<const Event *> events;
  if (d)
    for (auto &v : d->events)
      events.push_back(&v);
  std::stable_sort(events.begin(), events.end(), [&](auto *a, auto *b) {
    auto key = [&](const Event &v) {
      bool on = eventOnDate(v, today, w.timezone);
      int category = v.allDay ? (on ? 2 : 3)
                              : (v.start <= nowUnix() && v.end > nowUnix() ? 0
                                 : on && v.start >= nowUnix()              ? 1
                                                                           : 3);
      int64_t start = v.allDay ? localMidnight(v.first, w.timezone) : v.start;
      return std::pair{category, start};
    };
    return key(*a) < key(*b);
  });
  events.erase(std::remove_if(events.begin(), events.end(),
                              [&](auto *v) {
                                if ((w.size == Size::M || w.size == Size::L) &&
                                    !eventOnDate(*v, today, w.timezone))
                                  return true;
                                return v->allDay ? sys_days{v->last} <= sys_days{today} : v->end <= nowUnix();
                              }),
               events.end());
  auto eventText = [&](size_t index, float x, float y, float width, float size) {
    if (index >= events.size())
      return;
    dot(x + 4, y + 9, 4, events[index]->color);
    std::wstring label = eventLabel(*events[index], w);
    if (!eventOnDate(*events[index], today, w.timezone)) {
      auto date = events[index]->allDay ? events[index]->first : localDate(events[index]->start, w.timezone);
      label = std::to_wstring(unsigned(date.month())) + L"/" + std::to_wstring(unsigned(date.day())) + L" " +
              label;
    }
    text(label, x + 16, y - 2, width - 16, size * 1.6f, size);
  };
  if (w.size == Size::Slim) {
    text(std::to_wstring(unsigned(today.month())) + L"/" + std::to_wstring(unsigned(today.day())), 47, 31,
         119, 34, 26, true);
    line(176, 19, 176, 53);
    if (events.empty())
      text(emptyLabel, 193, 26, 123, 24, 12);
    else
      eventText(0, 193, 26, 123, 12);
    return;
  }
  if (w.size == Size::S) {
    text(std::to_wstring(unsigned(today.month())) + L"월", 20, 93, 44, 24, 16);
    text(std::to_wstring(unsigned(today.day())), 68, 61, 72, 62, 46, true);
    if (events.empty())
      text(emptyLabel, 20, 133, 120, 22, 11);
    else
      eventText(0, 20, 135, 120, 11);
    return;
  }
  if (w.size == Size::M) {
    text(std::to_wstring(unsigned(today.day())), 20, 49, 146, 59, 42, true);
    text(std::to_wstring(unsigned(today.month())) + L"월", 22, 118, 106, 23, 14);
    line(176, 56, 176, 140);
    text(L"오늘 일정", 192, 55, 124, 23, 12, true);
    if (events.empty())
      text(emptyLabel, 192, 87, 124, 25, 12);
    else {
      eventText(0, 192, 89, 124, 12);
      if (events.size() > 1)
        eventText(1, 192, 121, 124, 12);
    }
    return;
  }
  int y = int(today.year());
  unsigned month = unsigned(today.month());
  int yy = 0, mm = 0;
  if (swscanf_s(w.source.c_str(), L"%d-%d", &yy, &mm) == 2 && yy >= 1900 && yy <= 2200 && mm >= 1 &&
      mm <= 12) {
    y = yy;
    month = unsigned(mm);
  }
  auto grid = calendarGrid(y, month, w.weekStart);
  bool xl = w.size == Size::XL;
  float pad = xl ? 24.f : 20, top = xl ? 110.f : 83, header = xl ? 46.f : 23;
  float width = e.width - pad * 2, cellW = width / 7,
        cellH = xl ? (e.height - top - 24) / float(grid.weeks) : float(140) / float(grid.weeks);
  auto segments = xl && d ? calendarSegments(d->events, grid, w.timezone) : std::vector<CalendarSegment>{};
  int rowsAvailable = std::max(1, int((cellH - 41) / 26));
  std::vector<int> barLimits(grid.weeks, rowsAvailable);
  if (xl && d)
    for (int row = 0; row < grid.weeks; ++row)
      for (int col = 0; col < 7; ++col) {
        auto date = year_month_day{grid.first + std::chrono::days{row * 7 + col}};
        int total = 0, lanes = 0, timed = 0;
        for (auto &v : d->events)
          if (eventOnDate(v, date, w.timezone))
            ++total;
        for (auto &seg : segments)
          if (seg.week == row && seg.column <= col && col < seg.column + seg.span)
            lanes = std::max(lanes, seg.lane + 1);
        for (auto &v : d->events)
          if (!v.allDay && localDate(v.start, w.timezone) == localDate(v.end - 1, w.timezone) &&
              eventOnDate(v, date, w.timezone))
            ++timed;
        if (total > rowsAvailable || lanes + timed > rowsAvailable)
          barLimits[row] = std::max(0, rowsAvailable - 1);
      }
  heading(w, std::to_wstring(y) + L"년 " + std::to_wstring(month) + L"월");
  const wchar_t *days[]{L"일", L"월", L"화", L"수", L"목", L"금", L"토"};
  for (int col = 0; col < 7; ++col)
    text(days[(col + w.weekStart) % 7], pad + col * cellW, top - header, cellW, 25, xl ? 16.f : 13, false,
         0xffffff, DWRITE_TEXT_ALIGNMENT_CENTER);
  for (int row = 0; row < grid.weeks; ++row)
    for (int col = 0; col < 7; ++col) {
      auto date = year_month_day{grid.first + std::chrono::days{row * 7 + col}};
      float x = pad + col * cellW, cy = top + row * cellH;
      bool current = unsigned(date.month()) == month;
      float center = x + cellW / 2;
      if (date == today)
        dot(xl ? x + 19 : center, cy + 16, xl ? 14.f : 12, 0xd9d9ff);
      text(std::to_wstring(unsigned(date.day())), xl ? x + 5 : x, cy + 4, xl ? 29 : cellW, 24, xl ? 16.f : 13,
           date == today,
           date == today ? 0x242879
           : current     ? 0xffffff
                         : 0xd5c7e5,
           DWRITE_TEXT_ALIGNMENT_CENTER);
      if (xl) {
        line(x, top - header, x, top + grid.weeks * cellH);
        line(pad, cy, e.width - pad, cy);
        int count = 0, total = 0, lineIndex = 0;
        if (d)
          for (auto &v : d->events)
            if (eventOnDate(v, date, w.timezone))
              ++total;
        for (auto &seg : segments)
          if (seg.week == row && seg.column <= col && col < seg.column + seg.span &&
              seg.lane < barLimits[row]) {
            ++count;
            lineIndex = std::max(lineIndex, seg.lane + 1);
          }
        if (d)
          for (auto &v : d->events) {
            if (v.allDay || localDate(v.start, w.timezone) != localDate(v.end - 1, w.timezone) ||
                !eventOnDate(v, date, w.timezone))
              continue;
            if (lineIndex >= barLimits[row])
              break;
            float ey = cy + 36 + lineIndex * 26;
            dot(x + 8, ey + 9, 3, v.color);
            text(eventLabel(v, w), x + 17, ey, cellW - 23, 23, 12);
            ++count;
            ++lineIndex;
          }
        if (total > count)
          text(L"+" + std::to_wstring(total - count) + L"개", x + 9, cy + 36 + (rowsAvailable - 1) * 26,
               cellW - 18, 24, 12);
      } else if (d && std::any_of(d->events.begin(), d->events.end(),
                                  [&](auto &v) { return eventOnDate(v, date, w.timezone); }))
        dot(center, cy + 26, 2, 0x82d9ff);
    }
  if (xl) {
    line(pad, top - header, e.width - pad, top - header);
    line(e.width - pad, top - header, e.width - pad, top + grid.weeks * cellH);
    line(pad, top + grid.weeks * cellH, e.width - pad, top + grid.weeks * cellH);
    for (auto &seg : segments)
      if (seg.lane < barLimits[seg.week]) {
        auto &v = d->events[seg.event];
        float x = pad + seg.column * cellW + 4, y = top + seg.week * cellH + 36 + seg.lane * 26,
              width = seg.span * cellW - 8;
        bar(x, y, width, 22, v.color, .95f);
        text(v.allDay ? v.title : eventLabel(v, w), x + 6, y + 1, width - 12, 20, 12, false, 0x22213c);
      }
  } else {
    line(pad, 237, e.width - pad, 237);
    text(L"오늘 일정", pad, 246, e.width - 2 * pad, 23, 14, true);
    if (events.empty())
      text(emptyLabel, pad, 280, e.width - 2 * pad, 22, 14);
    else {
      eventText(0, pad, 273, e.width - 2 * pad, 14);
      if (events.size() > 1)
        eventText(1, pad, 298, e.width - 2 * pad, 13);
    }
  }
}
void Renderer::content(const Widget &w, const Snapshot *data, Extent e) {
  const bool slim = w.size == Size::Slim, compact = w.size == Size::S, medium = w.size == Size::M;
  const float pad = 20, width = e.width - 40;
  std::wstring title(kindName(w.kind));
  if (w.kind == Kind::Disk) {
    if (w.size == Size::L)
      title = L"전체 디스크";
    else if (data && !data->title.empty()) {
      auto drive = data->title;
      if (drive.size() == 3 && drive.back() == L'\\')
        drive.pop_back();
      title = slim ? drive : L"디스크 " + drive;
    }
  }
  heading(w, title);
  if (w.kind == Kind::Clock) {
    auto now = nowUnix();
    auto date = localDate(now, w.timezone);
    const wchar_t *weekdays[]{L"일요일", L"월요일", L"화요일", L"수요일", L"목요일", L"금요일", L"토요일"};
    std::wstring weekday = weekdays[std::chrono::weekday{sys_days{date}}.c_encoding()];
    auto shortDate =
        std::to_wstring(unsigned(date.month())) + L"월 " + std::to_wstring(unsigned(date.day())) + L"일";
    auto value = timeLabel(now, w, w.seconds, false);
    auto period = w.twelveHour ? timeLabel(now, w).substr(0, 2) : L"";
    if (slim) {
      text(value, 47, 31, 119, 34, w.seconds ? 21.f : 26.f, true);
      line(176, 19, 176, 53);
      text(shortDate, 193, 15, 126, 24, 14);
      text(period.empty() ? weekday : period + L" · " + weekday, 193, 39, 126, 22, 12);
    } else if (compact) {
      text(period, 20, 46, 120, 17, 11);
      text(value, 20, period.empty() ? 53.f : 63.f, 120, 49, w.seconds ? 25.f : 37.f, true);
      text(shortDate, 20, 109, 120, 22, 13);
      text(weekday, 20, 131, 120, 20, 12);
    } else if (medium) {
      text(period, 20, 47, 175, 20, 12);
      text(value, 20, period.empty() ? 53.f : 67.f, 175, 61, w.seconds ? 35.f : 51.f, true);
      line(197, 56, 197, 140);
      text(std::to_wstring(int(date.year())) + L"년", 214, 61, 102, 22, 13);
      text(shortDate, 214, 88, 102, 24, 16, true);
      text(weekday, 214, 118, 102, 22, 13);
    } else {
      text(period, 20, 65, width, 24, 15);
      text(value, 20, 91, width, 100, w.seconds ? 60.f : 86.f, true);
      text(dateLabel(date), 20, 208, width, 32, 22);
      line(20, 253, 316, 253);
      text(weekday, 20, 268, width, 25, 16);
      if (!w.timezone.empty())
        text(w.timezone, 20, 301, width, 22, 12);
    }
    return;
  }
  if (!data || (data->status != Status::Ready && data->status != Status::Stale)) {
    text(data && !data->message.empty() ? data->message : L"불러오는 중", slim ? 47.f : pad,
         slim ? 36.f : 65.f, slim ? 272.f : width, slim ? 23.f : 60.f, slim ? 12.f : 14.f, false, 0xffffff,
         DWRITE_TEXT_ALIGNMENT_LEADING, !slim);
    return;
  }
  auto usage = [&](float x, float y, float length, double percent) {
    bar(x, y, length, 5, 0xffffff, .2f, 2);
    if (std::isfinite(percent))
      bar(x, y, length * float(std::clamp(percent, 0., 100.) / 100), 5,
          highContrast_ ? foreground_ : design::metricColor(
              w.kind == Kind::Cpu || w.kind == Kind::Gpu ? design::Metric::Load : design::Metric::Capacity,
              percent, data->status == Status::Ready), .9f, 2);
  };
  const auto usageColor = design::metricColor(
      w.kind == Kind::Cpu || w.kind == Kind::Gpu ? design::Metric::Load : design::Metric::Capacity,
      data->value, data->status == Status::Ready);
  if (w.kind == Kind::Cpu || w.kind == Kind::Gpu || w.kind == Kind::Memory) {
    const bool thermal = w.kind != Kind::Memory;
    if (w.graph && !compact)
      text(L"최근 60초", 214, slim ? 10.f : 18.f, 102, 20, slim ? 11.f : 12.f,
           false, 0xffffff, DWRITE_TEXT_ALIGNMENT_TRAILING);
    text(number(data->value) + L"%", slim ? 47.f : pad, slim ? 31.f : medium && thermal ? 73.f : 49.f,
         slim     ? (thermal ? 72.f : 119.f)
         : medium ? 146.f
                  : width,
         slim ? 29.f : 55.f, slim ? 24.f : 42.f, true, usageColor);
    if (thermal) {
      const bool measured = validTemperature(data->temperature, monotonic(), w.interval);
      auto label = measured ? number(data->temperature.celsius) + L"°C" : L"—";
      text(label, slim ? 123.f : medium ? 128.f : pad,
           slim ? 38.f : medium ? 103.f : 104.f,
           slim ? 56.f : medium ? 46.f : width, 21, slim || medium ? 12.f : 14.f, false,
           design::metricColor(design::Metric::Temperature, data->temperature.celsius, measured));
    }
    auto footer = thermal ? data->title :
                  formatBytes(data->total - data->available) + L" / " + formatBytes(data->total);
    if (!thermal || (!slim && !compact))
      modelText(footer, slim ? 17.f : pad,
              slim ? 55.f : compact ? 137.f : medium ? 134.f : 302.f,
              slim ? 302.f : width, slim || compact ? 15.f : 21.f,
              slim || compact ? 11.f : 12.f);
    if (w.graph && !compact) {
      if (slim)
        graph(data->history, 192, 32, 124, 18, 0xffffff);
      else if (medium) {
        line(176, 56, 176, 122);
        graph(data->history, 192, 58, 124, 62, 0xffffff);
      } else {
        graph(data->history, pad, 151, width, 105, 0xffffff);
        text(L"평균 " + number(data->history.average(monotonic()).value_or(NAN)) + L"%", pad, 272, 143, 24,
             15);
        text(L"최대 " + number(data->history.maximum(monotonic()).value_or(NAN)) + L"%", 173, 272, 143, 24,
             15);
      }
    } else
      usage(slim ? 192.f : pad, slim ? 42.f : (compact || medium) ? 126.f : 151.f,
            slim ? 124.f : width, data->value);
  } else if (w.kind == Kind::System) {
    auto uptime = int64_t(std::max(0., std::isfinite(data->value) ? data->value : 0.));
    auto duration = std::to_wstring(uptime / 86400) + L"일 " + std::to_wstring(uptime / 3600 % 24) + L"시간";
    auto separator = data->extra.find(L'\n');
    auto cpu = data->extra.substr(0, separator);
    auto gpu = separator == std::wstring::npos ? L"" : data->extra.substr(separator + 1);
    auto os = data->detail.substr(0, data->detail.find(L" · "));
    if (slim) {
      if (w.systemFields[0])
        text(data->title, 47, 34, 119, 26, 17, true);
      line(176, 19, 176, 53);
      if (w.systemFields[1])
        text(os, 193, 14, 126, 25, 14);
      if (w.systemFields[4])
        text(duration, 193, 40, 126, 22, 12);
    } else if (compact) {
      if (w.systemFields[0])
        text(data->title, pad, 55, width, 34, 24, true);
      if (w.systemFields[1])
        text(os, pad, 94, width, 23, 12);
      line(pad, 120, e.width - pad, 120);
      if (w.systemFields[4])
        text(L"가동 " + duration, pad, 129, width, 22, 12);
    } else {
      if (w.systemFields[0])
        text(data->title, pad, 53, width, medium ? 31.f : 50.f,
             medium                    ? 24.f
             : data->title.size() > 16 ? 24.f
                                       : 32.f,
             true);
      std::vector<std::pair<std::wstring, std::wstring>> rows;
      if (!medium && w.systemFields[1])
        rows.push_back({L"OS", data->detail});
      if (w.systemFields[2])
        rows.push_back({L"CPU", cpu});
      if (w.systemFields[3])
        rows.push_back({L"GPU", gpu});
      if (!medium && w.systemFields[4])
        rows.push_back({L"가동 시간", duration});
      if (!medium)
        line(pad, 113, e.width - pad, 113);
      for (size_t i = 0; i < rows.size(); ++i) {
        float y = (medium ? 87.f : 127.f) + float(i) * (medium ? 23.f : 49.f);
        float label = medium ? 39.f : 76.f;
        text(rows[i].first, pad, y, label, 22, 12);
        text(rows[i].second, pad + label, y, width - label, medium ? 22.f : 40.f, medium ? 12.f : 14.f, false,
             0xffffff, DWRITE_TEXT_ALIGNMENT_LEADING, !medium);
        if (!medium && i + 1 < rows.size())
          line(pad, y + 39, e.width - pad, y + 39);
      }
      if (medium) {
        std::wstring footer = w.systemFields[1] ? os : L"";
        if (w.systemFields[4])
          footer += (footer.empty() ? L"" : L" · ") + std::wstring(L"가동 ") + duration;
        text(footer, pad, 137, width, 19, 11);
      }
    }
    if (std::none_of(w.systemFields.begin(), w.systemFields.end(), [](bool value) { return value; }))
      text(L"표시 항목 없음", slim ? 47.f : pad, slim ? 36.f : 64.f, width, 24, 12);
  } else if (w.kind == Kind::Network) {
    auto speed = [&](double value) {
      return w.networkBytes ? formatBytes(value) + L"/s" : number(value * 8 / 1e6, 2) + L" Mbps";
    };
    const auto incoming = L"↓ " + speed(data->value), outgoing = L"↑ " + speed(data->secondary);
    double maximum = std::max(data->history.maximum(monotonic()).value_or(1),
                              data->history2.maximum(monotonic()).value_or(1));
    if (slim) {
      text(incoming, 47, 36, 129, 24, 14, true);
      line(184, 19, 184, 53);
      text(L"송신", 200, 12, 116, 24, 14);
      text(outgoing, 200, 36, 116, 24, 14, true, 0xc3ffe9);
    } else {
      const float summary = medium ? 146.f : width;
      text(incoming, pad, 57, summary, 36, compact ? 17.f : medium ? 19.f : 27.f, true);
      text(outgoing, pad, 99, summary, 30, compact ? 15.f : medium ? 17.f : 22.f, false, 0xc3ffe9);
      if (w.graph && !compact) {
        float x = medium ? 192.f : pad, y = medium ? 76.f : 164.f, gw = medium ? 124.f : width,
              gh = medium ? 64.f : 98.f;
        if (medium)
          line(176, 56, 176, 140);
        graph(data->history, x, y, gw, gh, 0xffffff, maximum);
        graph(data->history2, x, y, gw, gh, 0xc3ffe9, maximum);
        text(L"최근 60초", 214, 18, 102, 20, 12, false, 0xffffff,
             DWRITE_TEXT_ALIGNMENT_TRAILING);
      }
      if (w.size == Size::L)
        text(w.source.empty() ? L"전체 네트워크 · ↓ 수신 / ↑ 송신" : data->title, pad, 301, width, 23, 12);
    }
  } else if (w.kind == Kind::Disk) {
    if (w.size == Size::L) {
      const size_t count = data->disks.size(), pages = (count + 5) / 6;
      const size_t first = pages > 1 ? size_t(nowUnix() / 12) % pages * 6 : 0;
      const size_t shown = std::min<size_t>(6, count - first);
      auto summary = std::to_wstring(count) + L"개 드라이브";
      if (pages > 1)
        summary += L" · " + std::to_wstring(first + 1) + L"–" + std::to_wstring(first + shown) + L" / " +
                   std::to_wstring(count);
      text(summary, pad, 53, width, 24, 14);
      if (!count)
        text(L"표시할 로컬 볼륨 없음", pad, 100, width, 30, 14);
      const bool dense = shown > 3;
      const float row = dense ? 37.f : 74.f;
      for (size_t i = 0; i < shown; ++i) {
        const auto &volume = data->disks[first + i];
        const float y = 92 + float(i) * row;
        text(volume.name, pad, y, dense ? 100.f : 127.f, 24, dense ? 12.f : 16.f, true);
        if (std::isfinite(volume.total) && volume.total > 0 && std::isfinite(volume.available)) {
          const auto total = formatBytes(volume.total, w.binaryDisk);
          const auto used = formatBytes(volume.total - volume.available, w.binaryDisk);
          text(used + L" / " + total, dense ? 124.f : 151.f, y + (dense ? 0 : 2), dense ? 192.f : 165.f, 22,
               dense ? 11.f : 12.f, false, 0xffffff, DWRITE_TEXT_ALIGNMENT_TRAILING);
          usage(pad, y + (dense ? 26.f : 33.f), 152.f,
                (volume.total - volume.available) / volume.total * 100);
          text(formatBytes(volume.freeForUser, w.binaryDisk) + L" 여유", 184, y + (dense ? 18.f : 28.f), 132,
               dense ? 18.f : 24.f, dense ? 10.f : 12.f, false, 0xffffff, DWRITE_TEXT_ALIGNMENT_TRAILING);
        } else
          text(L"조회 불가 · 잠김 또는 제거됨", 124, y + 2, 192, 22, 11, false, 0xffffff,
               DWRITE_TEXT_ALIGNMENT_TRAILING);
        if (!dense && i + 1 < shown)
          line(pad, y + 61, e.width - pad, y + 61);
      }
      if (dense)
        text(L"사용 중 / 전체 용량" + std::wstring(pages > 1 ? L" · 12초마다 다음 목록" : L""), pad, 315,
             width, 18, 10);
      return;
    }
    auto available = formatBytes(data->secondary, w.binaryDisk);
    if (slim) {
      text(L"디스크", 47, 37, 85, 22, 12);
      line(126, 19, 126, 53);
      text(L"여유 " + available, 143, 13, 173, 28, 17, true);
      usage(143, 48, 173, data->value);
    } else if (compact) {
      text(number(data->value) + L"%", pad, 49, width, 58, 42, true, usageColor);
      text(L"사용", pad, 101, width, 23, 13);
      text(available + L" 여유", pad, 121, width, 20, 11);
      usage(pad, 144, width, data->value);
    } else {
      text(available, pad, 55, 165, 52, 34, true);
      text(L"사용 가능", pad, 117, 165, 23, 14);
      line(187, 56, 187, 140);
      text(formatBytes(data->total - data->available, w.binaryDisk) + L" / " +
               formatBytes(data->total, w.binaryDisk),
           204, 61, 112, 23, 12);
      usage(204, 96, 112, data->value);
      text(number(data->value) + L"% 사용", 204, 117, 112, 23, 14, false, usageColor);
    }
  }
}

bool Renderer::draw(HWND hwnd, RenderSurface &s, const Widget &w, const Snapshot *data, int theme, bool edit,
                    UINT dpi) {
  if (!ready() || !surface(hwnd, s))
    return false;
  auto e = extent(w.size);
  if (!raster_)
    gpuContext_->SetTarget(s.bitmap.Get());
  context_->SetDpi(float(dpi) * w.scale / 100, float(dpi) * w.scale / 100);
  context_->SetTransform(D2D1::Matrix3x2F::Identity());
  context_->BeginDraw();
  context_->Clear({0, 0, 0, 0});
  int index = w.theme < 0 ? theme : w.theme;
  auto palette = palettes()[std::clamp(index, 0, 8)];
  HIGHCONTRASTW highContrast{sizeof highContrast};
  highContrast_ = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof highContrast, &highContrast, 0) &&
                  (highContrast.dwFlags & HCF_HIGHCONTRASTON);
  if (w.kind == Kind::Weather && w.weatherAuto && data && data->weather && data->weather->fetched &&
      nowUnix() - (data->weather->observed > 0 ? data->weather->observed : data->weather->fetched) < 7200) {
    auto &d = *data->weather;
    if (d.state == WeatherState::Snow)
      palette = d.night ? Palette{L"눈 오는 밤", 0x293951, 0x576884} : Palette{L"눈", 0x7b9fb8, 0xb2cbdc};
    else if (d.state == WeatherState::Rain || d.state == WeatherState::Sleet ||
             d.state == WeatherState::Showers || d.state == WeatherState::Drizzle ||
             d.state == WeatherState::Thunder)
      palette = d.night ? Palette{L"비 오는 밤", 0x233750, 0x45577b} : Palette{L"비", 0x3a5978, 0x7a96ba};
    else if (d.night && d.state != WeatherState::Unknown)
      palette = {L"밤", 0x192d63, 0x535089};
    else if (d.state == WeatherState::Cloudy || d.state == WeatherState::Overcast ||
             d.state == WeatherState::Fog)
      palette = {L"구름", 0x6b92b1, 0xa6bccd};
    else if (d.state == WeatherState::Clear)
      palette = {L"맑음", 0x3f91d9, 0x91c9ee};
  }
  if (highContrast_) {
    auto rgb = [](COLORREF c) {
      return uint32_t(GetRValue(c)) << 16 | uint32_t(GetGValue(c)) << 8 | GetBValue(c);
    };
    palette.top = palette.bottom = rgb(GetSysColor(COLOR_WINDOW));
    foreground_ = rgb(GetSysColor(COLOR_WINDOWTEXT));
  } else {
    // Preserve hue; transparency remains independent of this contrast adjustment.
    static std::map<uint32_t, uint32_t> surfaces;
    auto readable = [&](uint32_t rgb) {
      auto [it, inserted] = surfaces.try_emplace(rgb, 0);
      if (inserted)
        it->second = design::readableSurface(rgb);
      return it->second;
    };
    palette.top = readable(palette.top);
    palette.bottom = readable(palette.bottom);
  }
  bool scene =
      w.kind == Kind::Weather && w.weatherAuto && !highContrast_ && data && data->weather &&
      data->weather->fetched && data->weather->state != WeatherState::Unknown &&
      nowUnix() - (data->weather->observed > 0 ? data->weather->observed : data->weather->fetched) < 7200;
  bool sunset = false;
  if (scene && data->weather->state == WeatherState::Clear) {
    auto local = localTime(nowUnix(), L"Asia/Seoul");
    auto sun = sunriseSunset(localDate(nowUnix(), L"Asia/Seoul"), w.latitude, w.longitude);
    double hour = duration<double, std::ratio<3600>>(local - floor<days>(local)).count();
    sunset = std::isfinite(sun.second) && std::abs(hour - sun.second) < .55;
    if (sunset) {
      palette.top = design::readableSurface(0x63518c);
      palette.bottom = design::readableSurface(0xb5758f);
    }
  }
  float alpha = highContrast_ ? 1.f : 1 - w.backgroundTransparency / 100.f;
  if (edit)
    alpha = std::max(.3f, alpha);
  ComPtr<ID2D1RoundedRectangleGeometry> mask;
  auto card = D2D1::RoundedRect(D2D1::RectF(1, 1, e.width - 1, e.height - 1), design::radius, design::radius);
  if (scene && FAILED(d2d_->CreateRoundedRectangleGeometry(card, &mask)))
    scene = false;
  D2D1_GRADIENT_STOP stops[]{{0, color(palette.top, scene ? 1.f : alpha)},
                             {1, color(palette.bottom, scene ? 1.f : alpha)}};
  ComPtr<ID2D1GradientStopCollection> collection;
  ComPtr<ID2D1LinearGradientBrush> gradient;
  context_->CreateGradientStopCollection(stops, 2, &collection);
  if (collection)
    context_->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties({0, 0}, {e.width * .7f, e.height}), collection.Get(), &gradient);
  if (scene)
    context_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), mask.Get(),
                                              D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::Matrix3x2F::Identity(),
                                              alpha),
                        nullptr);
  if (gradient)
    context_->FillRoundedRectangle(card, gradient.Get());
  if (scene) {
    weatherBackdrop(*data->weather, e, sunset);
    context_->PopLayer();
  }
  if (edit) {
    brush_->SetColor(color(0xffffff, .85f));
    context_->DrawRoundedRectangle(card, brush_.Get(), 2);
  }
  if (w.kind == Kind::Calendar)
    calendar(w, data, e);
  else if (w.kind == Kind::Weather)
    weather(w, data, e);
  else
    content(w, data, e);

  HRESULT hr = context_->EndDraw();
  if (!raster_)
    gpuContext_->SetTarget(nullptr);
  if (FAILED(hr))
    return false;
  if (raster_) {
    POINT zero{};
    SIZE size{LONG(s.width), LONG(s.height)};
    float opacity = highContrast_ ? 1.f
                    : edit        ? std::max(.35f, 1 - w.transparency / 100.f)
                                  : 1 - w.transparency / 100.f;
    BLENDFUNCTION blend{AC_SRC_OVER, 0, BYTE(std::round(opacity * 255)), AC_SRC_ALPHA};
    return UpdateLayeredWindow(hwnd, nullptr, nullptr, &size, s.raster->dc, &zero, 0, &blend, ULW_ALPHA) !=
           FALSE;
  }
  s.opacity->SetOpacity(highContrast_ ? 1.f
                        : edit        ? std::max(.35f, 1 - w.transparency / 100.f)
                                      : 1 - w.transparency / 100.f);
  hr = s.chain->Present(0, 0);
  composition_->Commit();
  return SUCCEEDED(hr);
}
} // namespace tempos
