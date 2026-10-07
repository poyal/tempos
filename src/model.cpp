#include "model.h"
#include <shellapi.h>
#include <sstream>
#include <iomanip>
#include <numbers>

namespace tempos {
std::wstring wide(std::string_view s) {
  if (s.empty())
    return {};
  int n =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0)
    return L"";
  std::wstring result(n, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), result.data(), n);
  return result;
}
std::string utf8(std::wstring_view s) {
  if (s.empty())
    return {};
  int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0,
                              nullptr, nullptr);
  if (n <= 0)
    return {};
  std::string result(n, '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), result.data(), n,
                      nullptr, nullptr);
  return result;
}
std::wstring newId() {
  GUID id{};
  CoCreateGuid(&id);
  wchar_t text[40]{};
  StringFromGUID2(id, text, 40);
  return text;
}
std::wstring errorText(DWORD code) {
  wchar_t *p = nullptr;
  FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                 nullptr, code, 0, reinterpret_cast<wchar_t *>(&p), 0, nullptr);
  std::wstring result = p ? p : L"요청 실패";
  if (p)
    LocalFree(p);
  return result;
}
std::wstring number(double value, int decimals) {
  if (!std::isfinite(value))
    return L"—";
  std::wostringstream s;
  s << std::fixed << std::setprecision(decimals) << value;
  return s.str();
}
std::wstring formatBytes(double v, bool binary) {
  if (!std::isfinite(v) || v < 0)
    return L"—";
  double step = binary ? 1024. : 1000.;
  const wchar_t *u[] = {L"B", L"KiB", L"MiB", L"GiB", L"TiB"};
  const wchar_t *b[] = {L"B", L"kB", L"MB", L"GB", L"TB"};
  int i = 0;
  while (v >= step && i < 4) {
    v /= step;
    ++i;
  }
  return number(v, i > 0 ? 1 : 0) + L" " + (binary ? u[i] : b[i]);
}
std::wstring_view kindName(Kind k) {
  constexpr std::wstring_view names[] = {L"시계",        L"날씨",     L"CPU",    L"GPU",   L"메모리",
                                         L"시스템 정보", L"네트워크", L"디스크", L"캘린더"};
  auto i = static_cast<size_t>(k);
  return i < KindCount ? names[i] : L"위젯";
}
std::wstring_view sizeName(Size s) {
  constexpr std::wstring_view n[] = {L"S", L"Slim", L"M", L"L", L"XL"};
  auto i = static_cast<size_t>(s);
  return i < 5 ? n[i] : n[2];
}
Extent extent(Size s, int scale) {
  constexpr Extent sizes[] = {{160, 160}, {336, 72}, {336, 160}, {336, 336}, {1008, 840}};
  auto i = static_cast<size_t>(s);
  if (i > 4)
    i = 2;
  auto e = sizes[i];
  float f = scale / 100.f;
  return {e.width * f, e.height * f};
}
std::vector<int> intervals(Kind k) {
  switch (k) {
  case Kind::Clock:
    return {1, 60};
  case Kind::Weather:
    return {300, 900, 1800, 3600};
  case Kind::Memory:
    return {2, 5, 10, 30, 60};
  case Kind::System:
    return {30, 60, 300};
  case Kind::Disk:
    return {10, 30, 60, 300};
  case Kind::Calendar:
    return {60, 300, 900, 1800};
  default:
    return {1, 2, 5, 10};
  }
}
int defaultInterval(Kind k) {
  switch (k) {
  case Kind::Clock:
  case Kind::System:
  case Kind::Disk:
    return 60;
  case Kind::Weather:
    return 900;
  case Kind::Calendar:
    return 300;
  case Kind::Memory:
    return 5;
  default:
    return 2;
  }
}
bool overlaps(const Box &u, const Box &b, float gap) {
  return u.x < b.x + b.w + gap && u.x + u.w + gap > b.x && u.y < b.y + b.h + gap && u.y + u.h + gap > b.y;
}
float snap(float v) {
  return std::round(v / 4.f) * 4.f;
}
std::optional<Box> freePosition(Extent area, Extent s, const std::vector<Box> &occupied) {
  for (float x = 16; x + s.width <= area.width - 16; x += 4)
    for (float y = 16; y + s.height <= area.height - 16; y += 4) {
      Box b{x, y, s.width, s.height};
      if (std::none_of(occupied.begin(), occupied.end(), [&](auto &u) { return overlaps(u, b); }))
        return b;
    }
  return {};
}
const std::array<Palette, 9> &palettes() {
  static const std::array<Palette, 9> p = {{{L"스카이", 0x286bcc, 0x6ea5d1},
                                            {L"블루", 0x2550ad, 0x668bca},
                                            {L"코발트", 0x315dd1, 0x6c7cc5},
                                            {L"인디고", 0x445ab0, 0x8188bb},
                                            {L"틸", 0x277d91, 0x72aca9},
                                            {L"슬레이트", 0x426780, 0x819caf},
                                            {L"오션", 0x256f9b, 0x70a7c7},
                                            {L"스틸", 0x476cb0, 0x849fc6},
                                            {L"라벤더", 0x7952b5, 0xa28bcd}}};
  return p;
}
bool validateWidget(Widget &w) {
  if (static_cast<int>(w.kind) < 0 || static_cast<int>(w.kind) >= 9)
    return false;
  if (static_cast<int>(w.size) < 0 || static_cast<int>(w.size) > 4)
    return false;
  if (w.size == Size::XL && w.kind != Kind::Calendar)
    return false;
  if (w.scale != 100 && w.scale != 150 && w.scale != 200)
    return false;
  if (!std::isfinite(w.x) || !std::isfinite(w.y) || std::abs(w.x) > 100000 || std::abs(w.y) > 100000)
    return false;
  w.x = snap(w.x);
  w.y = snap(w.y);
  if (w.theme < -1 || w.theme >= 9)
    return false;
  if (w.transparency < 0 || w.transparency > 100 || w.backgroundTransparency < 0 ||
      w.backgroundTransparency > 100)
    return false;
  auto rates = intervals(w.kind);
  if (std::find(rates.begin(), rates.end(), w.interval) == rates.end())
    return false;
  if (w.weekStart < 0 || w.weekStart > 6)
    return false;
  if (w.kind == Kind::Calendar && !w.source.empty()) {
    try {
      auto dash = w.source.find(L'-');
      if (dash == std::wstring::npos)
        return false;
      size_t yearEnd = 0, monthEnd = 0;
      auto yearText = w.source.substr(0, dash), monthText = w.source.substr(dash + 1);
      int y = std::stoi(yearText, &yearEnd), m = std::stoi(monthText, &monthEnd);
      if (yearEnd != yearText.size() || monthEnd != monthText.size() || y < 1900 || y > 2200 || m < 1 ||
          m > 12)
        return false;
    } catch (...) {
      return false;
    }
  }
  if (w.nx < 1 || w.nx > 149 || w.ny < 1 || w.ny > 253)
    return false;
  if (!std::isfinite(w.latitude) || !std::isfinite(w.longitude) || w.latitude < 31 || w.latitude > 44 ||
      w.longitude < 123 || w.longitude > 133)
    return false;
  if (w.id.empty() || w.id.size() > 64 || w.source.size() > 1024 || w.region.size() > 256 ||
      w.timezone.size() > 128 || w.monitor.size() > 1024 || w.calendars.size() > 32)
    return false;
  return true;
}
std::vector<Monitor> monitors() {
  std::vector<Monitor> out;
  EnumDisplayMonitors(
      nullptr, nullptr,
      [](HMONITOR h, HDC, LPRECT, LPARAM p) -> BOOL {
        auto &v = *reinterpret_cast<std::vector<Monitor> *>(p);
        MONITORINFOEXW m{};
        m.cbSize = sizeof m;
        if (GetMonitorInfoW(h, &m)) {
          DISPLAY_DEVICEW d{};
          d.cb = sizeof d;
          EnumDisplayDevicesW(m.szDevice, 0, &d, EDD_GET_DEVICE_INTERFACE_NAME);
          Monitor result;
          result.id = *d.DeviceID ? d.DeviceID : m.szDevice;
          result.name = *d.DeviceString ? d.DeviceString : m.szDevice;
          result.work = m.rcWork;
          result.primary = (m.dwFlags & MONITORINFOF_PRIMARY) != 0;
          using GetDpiFn = HRESULT(WINAPI *)(HMONITOR, int, UINT *, UINT *);
          static HMODULE lib = LoadLibraryW(L"shcore.dll");
          static auto fn =
              lib ? reinterpret_cast<GetDpiFn>(GetProcAddress(lib, "GetDpiForMonitor")) : nullptr;
          UINT y = 96;
          if (fn)
            fn(h, 0, &result.dpi, &y);
          v.push_back(result);
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&out));
  std::stable_sort(out.begin(), out.end(), [](auto &u, auto &b) { return u.primary > b.primary; });
  return out;
}
std::vector<Placement> arrangeWidgets(const std::vector<Widget> &widgets,
                                      const std::vector<Monitor> &screens) {
  std::vector<Placement> result(widgets.size());
  std::vector<std::vector<Box>> occupied(screens.size());
  auto area = [&](size_t index) {
    auto &m = screens[index];
    return Extent{(m.work.right - m.work.left) * 96.f / m.dpi, (m.work.bottom - m.work.top) * 96.f / m.dpi};
  };
  // Reserve valid original positions before placing temporarily displaced widgets.
  for (size_t i = 0; i < widgets.size(); ++i) {
    auto &w = widgets[i];
    auto &p = result[i];
    p.id = w.id;
    auto size = extent(w.size, w.scale);
    p.box = {w.x, w.y, size.width, size.height};
    for (size_t index = 0; index < screens.size(); ++index) {
      auto &m = screens[index];
      if (w.monitor != m.id && !(w.monitor.empty() && m.primary))
        continue;
      auto bounds = area(index);
      if (w.x < 16 || w.y < 16 || w.x + size.width > bounds.width - 16 ||
          w.y + size.height > bounds.height - 16)
        break;
      if (std::any_of(occupied[index].begin(), occupied[index].end(),
                      [&](auto &b) { return overlaps(p.box, b); }))
        break;
      p.monitor = m.id;
      p.visible = true;
      occupied[index].push_back(p.box);
      break;
    }
  }
  for (size_t i = 0; i < widgets.size(); ++i) {
    auto &p = result[i];
    if (p.visible)
      continue;
    auto &w = widgets[i];
    for (size_t pass = 0; pass < 2 && !p.visible; ++pass)
      for (size_t index = 0; index < screens.size(); ++index) {
        bool preferred = screens[index].id == w.monitor;
        if ((pass == 0) != preferred)
          continue;
        auto position = freePosition(area(index), extent(w.size, w.scale), occupied[index]);
        if (!position)
          continue;
        p.monitor = screens[index].id;
        p.box = *position;
        p.visible = true;
        p.temporary = true;
        occupied[index].push_back(p.box);
        break;
      }
  }
  return result;
}
void History::add(Sample s) {
  if (s.end <= s.start)
    return;
  data_.push_back(s);
  while (data_.size() > 61 || (!data_.empty() && data_.front().end < s.end - 60))
    data_.pop_front();
}
std::optional<double> History::average(double now) const {
  double sum = 0, seconds = 0;
  for (auto &s : data_) {
    double d = std::max(0., std::min(now, s.end) - std::max(now - 60, s.start));
    if (s.valid && std::isfinite(s.value)) {
      sum += s.value * d;
      seconds += d;
    }
  }
  return seconds > 0 ? std::optional(sum / seconds) : std::nullopt;
}
std::optional<double> History::maximum(double now) const {
  std::optional<double> m;
  for (auto &s : data_)
    if (s.valid && s.end > now - 60 && s.start < now && std::isfinite(s.value))
      m = m ? std::max(*m, s.value) : s.value;
  return m;
}
double monotonic() {
  return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}
int64_t nowUnix() {
  return std::chrono::duration_cast<std::chrono::seconds>(Utc::now().time_since_epoch()).count();
}
static const std::chrono::time_zone *timezone(const std::wstring &z) {
  try {
    return z.empty() ? std::chrono::current_zone() : std::chrono::locate_zone(utf8(z));
  } catch (...) {
    return std::chrono::locate_zone("UTC");
  }
}
std::chrono::local_seconds localTime(int64_t s, const std::wstring &z) {
  return timezone(z)->to_local(std::chrono::sys_seconds{std::chrono::seconds{s}});
}
std::chrono::year_month_day localDate(int64_t s, const std::wstring &z) {
  return std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(localTime(s, z))};
}
int64_t localMidnight(std::chrono::year_month_day date, const std::wstring &z) {
  auto t = std::chrono::local_days{date};
  return std::chrono::duration_cast<std::chrono::seconds>(
             timezone(z)->to_sys(t, std::chrono::choose::earliest).time_since_epoch())
      .count();
}
CalendarGrid calendarGrid(int y, unsigned m, int weekStart) {
  using namespace std::chrono;
  sys_days first = year{y} / month{m} / 1;
  int offset = (int(weekday{first}.c_encoding()) - weekStart + 7) % 7;
  unsigned count = unsigned(year_month_day_last{year{y}, month_day_last{month{m}}}.day());
  return {first - days{offset}, (int(count) + offset + 6) / 7};
}
int64_t parseRfc3339(std::string_view value) {
  constexpr size_t digits[]{0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18};
  for (size_t i : digits) {
    if (i >= value.size() || value[i] < '0' || value[i] > '9')
      return INT64_MIN;
  }
  size_t suffix = 19;
  if (value.size() > suffix && value[suffix] == '.') {
    size_t begin = ++suffix;
    while (suffix < value.size() && value[suffix] >= '0' && value[suffix] <= '9')
      ++suffix;
    if (begin == suffix)
      return INT64_MIN;
  }
  if (suffix >= value.size() ||
      (value[suffix] != 'Z' && value[suffix] != 'z' && value[suffix] != '+' && value[suffix] != '-'))
    return INT64_MIN;
  if ((value[suffix] == 'Z' || value[suffix] == 'z') && suffix + 1 != value.size())
    return INT64_MIN;
  int y, m, d, h = 0, min = 0, sec = 0;
  if (value.size() < 20 || value[4] != '-' || value[7] != '-' || value[10] != 'T' || value[13] != ':' ||
      value[16] != ':' ||
      sscanf_s(std::string(value).c_str(), "%d-%d-%dT%d:%d:%d", &y, &m, &d, &h, &min, &sec) != 6)
    return INT64_MIN;
  using namespace std::chrono;
  year_month_day date{year{y}, month{unsigned(m)}, day{unsigned(d)}};
  if (!date.ok() || y < 1 || h < 0 || min < 0 || sec < 0 || h > 23 || min > 59 || sec > 59)
    return INT64_MIN;
  int offset = 0;
  if (value.size() > 19) {
    size_t pos = value.find_first_of("+-", 19);
    if (pos != std::string::npos) {
      int oh = 0, om = 0;
      if (value.size() != pos + 6 || value[pos + 3] != ':' ||
          sscanf_s(std::string(value.substr(pos + 1)).c_str(), "%d:%d", &oh, &om) != 2 || oh < 0 || om < 0 ||
          oh > 23 || om > 59)
        return INT64_MIN;
      offset = (oh * 60 + om) * 60 * (value[pos] == '+' ? 1 : -1);
    } else if (value.back() != 'Z' && value.back() != 'z')
      return INT64_MIN;
  }
  auto t = sys_days{date} + hours{h} + minutes{min} + seconds{sec - offset};
  return duration_cast<seconds>(t.time_since_epoch()).count();
}
std::vector<CalendarSegment> calendarSegments(const std::vector<Event> &events, CalendarGrid grid,
                                              const std::wstring &zone) {
  using namespace std::chrono;
  std::vector<CalendarSegment> result;
  for (int row = 0; row < grid.weeks; ++row) {
    auto first = grid.first + days{row * 7}, last = first + days{7};
    std::vector<CalendarSegment> rowSegments;
    for (size_t i = 0; i < events.size(); ++i) {
      auto &e = events[i];
      auto start = e.allDay ? sys_days{e.first} : sys_days{localDate(e.start, zone)};
      auto end = e.allDay ? sys_days{e.last} : sys_days{localDate(e.end - 1, zone)} + days{1};
      if (!e.allDay && end - start <= days{1})
        continue;
      auto a = std::max(first, start), b = std::min(last, end);
      if (b > a)
        rowSegments.push_back({i, row, int((a - first).count()), int((b - a).count()), 0});
    }
    std::stable_sort(rowSegments.begin(), rowSegments.end(), [](auto &a, auto &b) {
      return a.column != b.column ? a.column < b.column : a.span > b.span;
    });
    std::vector<unsigned> occupied;
    for (auto &segment : rowSegments) {
      unsigned mask = ((1u << segment.span) - 1) << segment.column;
      size_t lane = 0;
      while (lane < occupied.size() && (occupied[lane] & mask))
        ++lane;
      if (lane == occupied.size())
        occupied.push_back(0);
      occupied[lane] |= mask;
      segment.lane = int(lane);
      result.push_back(segment);
    }
  }
  return result;
}
std::string rfc3339(int64_t time) {
  using namespace std::chrono;
  sys_seconds t{seconds{time}};
  year_month_day d{floor<days>(t)};
  hh_mm_ss tod{t - floor<days>(t)};
  char b[32]{};
  sprintf_s(b, "%04d-%02u-%02uT%02d:%02d:%02dZ", int(d.year()), unsigned(d.month()), unsigned(d.day()),
            int(tod.hours().count()), int(tod.minutes().count()), int(tod.seconds().count()));
  return b;
}
bool eventOnDate(const Event &e, std::chrono::year_month_day d, const std::wstring &z) {
  using namespace std::chrono;
  if (e.allDay)
    return sys_days{d} >= sys_days{e.first} && sys_days{d} < sys_days{e.last};
  auto start = localMidnight(d, z), end = localMidnight(year_month_day{sys_days{d} + days{1}}, z);
  return e.end > start && e.start < end;
}
std::pair<double, double> sunriseSunset(std::chrono::year_month_day d, double lat, double lon) {
  using namespace std::chrono;
  constexpr double pi = std::numbers::pi;
  auto deg = [&](double x) { return x * pi / 180; };
  auto norm = [](double x) {
    x = std::fmod(x, 360.);
    return x < 0 ? x + 360 : x;
  };
  int n = int((sys_days{d} - sys_days{d.year() / January / 1}).count()) + 1;
  auto calculate = [&](bool rise) -> double {
    double lngHour = lon / 15., t = n + ((rise ? 6. : 18.) - lngHour) / 24.;
    double m = .9856 * t - 3.289;
    double l = norm(m + 1.916 * std::sin(deg(m)) + .020 * std::sin(2 * deg(m)) + 282.634);
    double ra = norm(std::atan(.91764 * std::tan(deg(l))) * 180 / pi);
    ra += (std::floor(l / 90) * 90 - std::floor(ra / 90) * 90);
    ra /= 15;
    double sd = .39782 * std::sin(deg(l)), cd = std::cos(std::asin(sd));
    double ch = (std::cos(deg(90.833)) - sd * std::sin(deg(lat))) / (cd * std::cos(deg(lat)));
    if (ch > 1 || ch < -1)
      return NAN;
    double h = std::acos(ch) * 180 / pi;
    if (rise)
      h = 360 - h;
    double ut = h / 15 + ra - .06571 * t - 6.622 - lngHour;
    double kst = std::fmod(ut + 9 + 48, 24.);
    return kst;
  };
  return {calculate(true), calculate(false)};
}
} // namespace tempos
