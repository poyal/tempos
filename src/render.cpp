#include "render.h"
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
                    uint32_t rgb, DWRITE_TEXT_ALIGNMENT align) {
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
    font->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    it = fonts_.emplace(key, std::move(font)).first;
  }
  it->second->SetTextAlignment(align);
  brush_->SetColor(color(rgb));
  context_->DrawTextW(value.data(), UINT32(value.size()), it->second.Get(), D2D1::RectF(x, y, x + w, y + h),
                      brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
void Renderer::line(float x, float y, float x2, float y2, uint32_t rgb, float alpha, float stroke) {
  brush_->SetColor(color(rgb, alpha));
  context_->DrawLine({x, y}, {x2, y2}, brush_.Get(), stroke);
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
      line(last.x, last.y, px, py, rgb, .95f, 2);
    last = {px, py};
    previous = true;
  }
  line(x, y + h, x + w, y + h, rgb, .2f);
}
static std::wstring timeLabel(int64_t t, const Widget &w, bool seconds = false) {
  auto local = localTime(t, w.timezone);
  hh_mm_ss tod{local - floor<days>(local)};
  int hour = int(tod.hours().count());
  std::wstring prefix;
  if (w.twelveHour) {
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
          out += L" · 날짜를 선택하여 전체 일정 보기";
          break;
        }
      }
    }
    return out;
  }
  if (!s)
    return out + L" 불러오는 중";
  if (w.kind == Kind::Weather && s->weather) {
    out += L" " + w.region + L" " + number(s->weather->temperature) + L"도 " + s->weather->condition +
           L" 자료: 기상청";
  } else if (std::isfinite(s->value)) {
    out += L" " + number(s->value) +
           (w.kind == Kind::Network  ? L" B/s"
            : w.kind == Kind::System ? L" 초"
                                     : L"%");
  }
  out += L" " + s->message;
  return out;
}
void Renderer::weather(const Widget &w, const Snapshot *s, Extent e) {
  auto d = s ? s->weather : nullptr;
  float pad = w.size == Size::Slim ? 16 : 20;
  bool stale = d && nowUnix() - (d->observed > 0 ? d->observed : d->fetched) > 7200;
  std::wstring place = w.region;
  if (auto p = place.find_last_of(L' '); p != std::wstring::npos)
    place = place.substr(p + 1);
  text(place, pad, w.size == Size::Slim ? 8 : 16, e.width - 2 * pad, 24, w.size == Size::Slim ? 13 : 15);
  if (!d || !std::isfinite(d->temperature)) {
    text(s ? s->message : L"날씨 불러오는 중", pad, w.size == Size::Slim ? 28 : 57, e.width - 2 * pad,
         w.size == Size::Slim ? 24 : 68, w.size == Size::Slim ? 12 : 15);
    text(L"자료: 기상청", pad, e.height - 21, e.width - 2 * pad, 18, 10);
    return;
  }
  double temperature = w.fahrenheit ? d->temperature * 1.8 + 32 : d->temperature;
  float size = w.size == Size::Slim ? 28 : w.size == Size::S ? 42 : 52;
  text(number(temperature) + L"°", w.size == Size::Slim ? 115.f : pad, w.size == Size::Slim ? 14.f : 43,
       e.width - pad, 64, size, true);
  float iconX = w.size == Size::S ? 119.f : e.width - 52, iconY = w.size == Size::Slim ? 32.f : 65.f;
  if (d->rain > 0) {
    dot(iconX - 10, iconY, 14, 0xf2f4ff);
    dot(iconX + 7, iconY - 4, 19, 0xf2f4ff);
    bar(iconX - 26, iconY, 55, 15, 0xf2f4ff, 1, 8);
    for (int i = -1; i <= 1; ++i)
      if (d->rain == 3 || d->rain == 7) {
        dot(iconX + float(i * 15), iconY + 27, 3, 0xffffff);
      } else
        line(iconX + float(i * 14), iconY + 23, iconX + float(i * 14) - 5, iconY + 33, 0xbbe9ff, 1, 2);
  } else if (d->sky >= 3) {
    dot(iconX - 10, iconY, 16, 0xf2f4ff);
    dot(iconX + 8, iconY - 8, 21, 0xf2f4ff);
    bar(iconX - 28, iconY, 59, 15, 0xf2f4ff, 1, 8);
  } else if (d->sky != 1) {
    text(L"—", iconX - 18, iconY - 20, 40, 40, 28);
  } else if (d->night) {
    dot(iconX, iconY, 22, 0xfff2c7);
    dot(iconX + 12, iconY - 9, 19, 0x293d79);
  } else {
    dot(iconX, iconY, 20, 0xffda67);
    for (int i = 0; i < 8; ++i) {
      float a = float(i) * 3.14159265f / 4;
      line(iconX + std::cos(a) * 26, iconY + std::sin(a) * 26, iconX + std::cos(a) * 31,
           iconY + std::sin(a) * 31, 0xffeaa6, 1, 2);
    }
  }
  if (w.size != Size::Slim) {
    text(d->condition, pad, 103, e.width - 2 * pad, 23, 15);
    if (w.size != Size::S) {
      text(L"최저 " + number(w.fahrenheit ? d->low * 1.8 + 32 : d->low) + L"°   최고 " +
               number(w.fahrenheit ? d->high * 1.8 + 32 : d->high) + L"°",
           125, 106, e.width - 143, 22, 13);
      if (w.size == Size::L) {
        line(pad, 147, e.width - pad, 147);
        for (size_t i = 0; i < d->hours.size() && i < 6; ++i) {
          float x = pad + float(i) * (e.width - 2 * pad) / 6;
          auto &h = d->hours[i];
          Widget korea = w;
          korea.timezone = L"Asia/Seoul";
          text(timeLabel(h.time, korea), x, 161, 48, 22, 12);
          text(number(w.fahrenheit ? h.temperature * 1.8 + 32 : h.temperature) + L"°", x, 191, 48, 30, 21,
               true);
          text(std::isfinite(h.probability) ? number(h.probability) + L"%" : L"—", x, 226, 48, 21, 12);
        }
        text(L"습도 " + number(d->humidity) + L"%   바람 " + number(d->wind, 1) + L" m/s", pad, 274,
             e.width - 2 * pad, 24, 14);
      }
    }
  }
  text(stale ? L"자료: 기상청 · 갱신 지연" : L"자료: 기상청", pad, e.height - 22, e.width - 2 * pad, 18, 10);
}
void Renderer::calendar(const Widget &w, const Snapshot *s, Extent e) {
  auto today = localDate(nowUnix(), w.timezone);
  auto d = s ? s->calendar : nullptr;
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
    text(label, x + 16, y - 2, width - 16, size * 2.4f, size);
  };
  if (w.size == Size::Slim) {
    text(std::to_wstring(unsigned(today.month())) + L"/" + std::to_wstring(unsigned(today.day())), 20, 18, 84,
         36, 26, true);
    line(106, 16, 106, 56);
    if (events.empty())
      text(L"예정된 일정 없음", 123, 24, 190, 24, 15);
    else
      eventText(0, 124, 24, 190, 14);
    return;
  }
  if (w.size == Size::S) {
    text(std::to_wstring(int(today.year())) + L"년 " + std::to_wstring(unsigned(today.month())) + L"월", 20,
         17, 120, 23, 14, true);
    text(std::to_wstring(unsigned(today.day())), 20, 40, 120, 64, 50, true, 0xffffff,
         DWRITE_TEXT_ALIGNMENT_CENTER);
    line(20, 109, 140, 109);
    if (events.empty())
      text(L"일정 없음", 20, 119, 120, 24, 13);
    else
      eventText(0, 20, 118, 120, 13);
    return;
  }
  if (w.size == Size::M) {
    text(L"캘린더", 20, 18, 115, 28, 17, true);
    text(std::to_wstring(unsigned(today.day())), 20, 47, 110, 68, 55, true);
    text(std::to_wstring(unsigned(today.month())) + L"월", 22, 118, 106, 23, 14);
    line(140, 20, 140, 140);
    text(L"오늘 일정", 159, 20, 158, 24, 15, true);
    if (events.empty())
      text(L"일정 없음", 159, 59, 158, 28, 14);
    else {
      eventText(0, 159, 62, 157, 14);
      if (events.size() > 1)
        eventText(1, 159, 107, 157, 14);
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
  text(std::to_wstring(y) + L"년 " + std::to_wstring(month) + L"월", pad + 35, 19, e.width - 2 * pad - 70, 45,
       xl ? 28.f : 22, true, 0xffffff, DWRITE_TEXT_ALIGNMENT_CENTER);
  text(L"‹", pad, 14, 35, 39, 30);
  text(L"›", e.width - pad - 32, 14, 32, 39, 30);
  if (xl)
    text(L"오늘", e.width - pad - 92, 24, 54, 30, 16);
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
      text(L"일정 없음", pad, 280, e.width - 2 * pad, 22, 14);
    else {
      eventText(0, pad, 278, e.width - 2 * pad, 14);
      if (events.size() > 1)
        eventText(1, pad, 307, e.width - 2 * pad, 13);
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
    if (d.rain == 3 || d.rain == 7)
      palette = {L"눈", 0x7b9fb8, 0xb2cbdc};
    else if (d.rain > 0)
      palette = {L"비", 0x3a5978, 0x7a96ba};
    else if (d.night)
      palette = {L"밤", 0x192d63, 0x535089};
    else if (d.sky >= 3)
      palette = {L"구름", 0x6b92b1, 0xa6bccd};
    else if (d.sky == 1)
      palette = {L"맑음", 0x3f91d9, 0x91c9ee};
  }
  if (highContrast_) {
    auto rgb = [](COLORREF c) {
      return uint32_t(GetRValue(c)) << 16 | uint32_t(GetGValue(c)) << 8 | GetBValue(c);
    };
    palette.top = palette.bottom = rgb(GetSysColor(COLOR_WINDOW));
    foreground_ = rgb(GetSysColor(COLOR_WINDOWTEXT));
  }
  float alpha = highContrast_ ? 1.f : 1 - w.backgroundTransparency / 100.f;
  if (edit)
    alpha = std::max(.3f, alpha);
  D2D1_GRADIENT_STOP stops[]{{0, color(palette.top, alpha)}, {1, color(palette.bottom, alpha)}};
  ComPtr<ID2D1GradientStopCollection> collection;
  ComPtr<ID2D1LinearGradientBrush> gradient;
  context_->CreateGradientStopCollection(stops, 2, &collection);
  if (collection)
    context_->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties({0, 0}, {e.width * .7f, e.height}), collection.Get(), &gradient);
  auto card = D2D1::RoundedRect(D2D1::RectF(1, 1, e.width - 1, e.height - 1),
                                w.size == Size::Slim ? 34.f : 24.f, w.size == Size::Slim ? 34.f : 24.f);
  if (gradient)
    context_->FillRoundedRectangle(card, gradient.Get());
  if (edit) {
    brush_->SetColor(color(0xffffff, .85f));
    context_->DrawRoundedRectangle(card, brush_.Get(), 2);
  }
  if (w.kind == Kind::Calendar)
    calendar(w, data, e);
  else if (w.kind == Kind::Weather)
    weather(w, data, e);
  else if (w.kind == Kind::Clock) {
    auto now = nowUnix();
    if (w.size == Size::Slim) {
      text(timeLabel(now, w, w.seconds), 17, 15, 190, 43, 30, true);
      text(std::to_wstring(unsigned(localDate(now, w.timezone).month())) + L"월 " +
               std::to_wstring(unsigned(localDate(now, w.timezone).day())) + L"일",
           211, 26, 107, 25, 14);
    } else {
      text(w.timezone.empty() ? L"시계" : w.timezone, 20, 17, e.width - 40, 25, 15);
      text(timeLabel(now, w, w.seconds), 20, w.size == Size::S ? 58.f : 49, e.width - 40, 76,
           w.size == Size::S ? (w.seconds ? 23.f : 36.f) : (w.seconds ? 43.f : 52.f), true);
      text(dateLabel(localDate(now, w.timezone)), 20, w.size == Size::L ? 128.f : 123, e.width - 40, 24,
           w.size == Size::S ? 11.f : 14);
      if (w.size == Size::L) {
        float cx = e.width / 2, cy = 241;
        dot(cx, cy, 61, 0xffffff, .1f);
        auto t = localTime(now, w.timezone);
        hh_mm_ss tod{t - floor<days>(t)};
        float minute = float(tod.minutes().count()), hour = float(tod.hours().count() % 12) + minute / 60;
        auto hand = [&](float angle, float length, float stroke) {
          angle = angle * 6.2831853f - 1.5707963f;
          line(cx, cy, cx + std::cos(angle) * length, cy + std::sin(angle) * length, 0xffffff, 1, stroke);
        };
        hand(hour / 12, 33, 5);
        hand(minute / 60, 49, 3);
        dot(cx, cy, 4, 0xffffff);
      }
    }
  } else {
    float pad = w.size == Size::Slim ? 17.f : 20.f;
    std::wstring title = std::wstring(kindName(w.kind));
    text(title, pad, w.size == Size::Slim ? 8.f : 16, e.width - 2 * pad, 27, w.size == Size::Slim ? 12.f : 16,
         true);
    if (!data || (data->status != Status::Ready && data->status != Status::Stale)) {
      text(data && !data->message.empty() ? data->message : L"불러오는 중", pad,
           w.size == Size::Slim ? 29.f : 65, e.width - 2 * pad, e.height - 55,
           w.size == Size::Slim ? 13.f : 15);
    } else if (w.kind == Kind::System) {
      auto uptime = int64_t(data->value);
      std::wstring detail;
      auto append = [&](const std::wstring &line) {
        if (line.empty())
          return;
        if (!detail.empty())
          detail += L"\n";
        detail += line;
      };
      if (w.systemFields[0])
        append(data->title);
      if (w.systemFields[1])
        append(data->detail);
      auto separator = data->extra.find(L'\n');
      if (w.systemFields[2])
        append(data->extra.substr(0, separator));
      if (w.systemFields[3] && separator != std::wstring::npos)
        append(data->extra.substr(separator + 1));
      if (w.systemFields[4])
        append(L"가동 " + std::to_wstring(uptime / 86400) + L"일 " + std::to_wstring(uptime / 3600 % 24) +
               L"시간");
      if (detail.empty())
        detail = L"표시 항목을 선택하세요";
      text(detail, pad, w.size == Size::Slim ? 28.f : 48, e.width - 2 * pad, e.height - 51,
           w.size == Size::Slim ? 12.f
           : w.size == Size::S  ? 12.f
                                : 14);
    } else if (w.kind == Kind::Network) {
      auto speed = [&](double v) {
        return w.networkBytes ? formatBytes(v) + L"/s" : number(v * 8 / 1e6, 2) + L" Mbps";
      };
      if (w.size == Size::Slim) {
        text(L"↓ " + speed(data->value) + L"  ↑ " + speed(data->secondary), pad, 29, e.width - 2 * pad, 29,
             16, true);
      } else {
        text(L"↓ " + speed(data->value), pad, 52, e.width - 2 * pad, 38, w.size == Size::S ? 19.f : 27, true);
        text(L"↑ " + speed(data->secondary), pad, 94, e.width - 2 * pad, 29, w.size == Size::S ? 16.f : 20);
        if (w.size == Size::L) {
          double maximum = std::max(data->history.maximum(monotonic()).value_or(1),
                                    data->history2.maximum(monotonic()).value_or(1));
          graph(data->history, 20, 177, e.width - 40, 91, 0xffffff, maximum);
          graph(data->history2, 20, 177, e.width - 40, 91, 0x91eac9, maximum);
          text(data->title, pad, 294, e.width - 2 * pad, 24, 13);
        }
      }
    } else {
      std::wstring value = number(data->value) + L"%";
      text(value, w.size == Size::Slim ? 103.f : pad, w.size == Size::Slim ? 13.f : 45, e.width - 2 * pad, 65,
           w.size == Size::Slim ? 31.f
           : w.size == Size::S  ? 42.f
                                : 48,
           true);
      if (w.size != Size::Slim) {
        if (w.kind == Kind::Memory || w.kind == Kind::Disk)
          text(formatBytes(data->total - data->available, w.kind == Kind::Memory || w.binaryDisk) + L" / " +
                   formatBytes(data->total, w.kind == Kind::Memory || w.binaryDisk),
               pad, 108, e.width - 2 * pad, 23, w.size == Size::S ? 11.f : 14);
        else
          text(data->title, pad, 108, e.width - 2 * pad, 23, w.size == Size::S ? 11.f : 13);
        bar(pad, 139, e.width - pad * 2, 5, 0xffffff, .2f, 2);
        if (std::isfinite(data->value))
          bar(pad, 139, (e.width - pad * 2) * float(std::clamp(data->value, 0., 100.) / 100), 5, 0xffffff,
              .9f, 2);
        if (w.size == Size::L) {
          if (w.graph && (w.kind == Kind::Cpu || w.kind == Kind::Gpu)) {
            graph(data->history, pad, 180, e.width - 2 * pad, 88, 0xffffff);
            text(L"최근 60초 · 평균 " + number(data->history.average(monotonic()).value_or(NAN)) + L"%", pad,
                 289, e.width - 2 * pad, 25, 14);
          } else if (w.kind == Kind::Disk) {
            text(L"사용 가능 " + formatBytes(data->available, w.binaryDisk) + L"\n내 계정 가능 " +
                     formatBytes(data->secondary, w.binaryDisk),
                 pad, 185, e.width - 2 * pad, 89, 16);
          } else if (w.kind == Kind::Memory)
            text(L"사용 가능 " + formatBytes(data->available), pad, 185, e.width - 2 * pad, 40, 18);
        }
      }
    }
  }
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
