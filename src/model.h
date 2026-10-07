#pragma once
#include <windows.h>
#include <objbase.h>
#include <chrono>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tempos {
using Clock = std::chrono::steady_clock;
using Utc = std::chrono::system_clock;
using namespace std::chrono_literals;
std::wstring wide(std::string_view value);
std::string utf8(std::wstring_view value);
std::wstring errorText(DWORD code);
std::wstring formatBytes(double value, bool binary = true);
std::wstring number(double value, int decimals = 0);
std::wstring newId();

enum class Kind { Clock, Weather, Cpu, Gpu, Memory, System, Network, Disk, Calendar };
enum class Size { S, Slim, M, L, XL };
enum class Status { Loading, Ready, Stale, Unavailable, Error };
constexpr size_t KindCount = 9;
std::wstring_view kindName(Kind kind);
std::wstring_view sizeName(Size size);
std::vector<int> intervals(Kind kind);
int defaultInterval(Kind kind);
struct Extent {
  float width{}, height{};
};
struct Box {
  float x{}, y{}, w{}, h{};
};
Extent extent(Size size, int scale = 100);
bool overlaps(const Box &a, const Box &b, float gap = 16.f);
float snap(float value);
std::optional<Box> freePosition(Extent area, Extent size, const std::vector<Box> &occupied);

struct Palette {
  const wchar_t *name;
  uint32_t top, bottom;
};
const std::array<Palette, 9> &palettes();
struct Widget {
  std::wstring id = newId();
  Kind kind = Kind::Clock;
  Size size = Size::M;
  int scale = 100;
  float x = 16, y = 16;
  std::wstring monitor;
  int theme = -1;
  int interval = 60;
  int backgroundTransparency = 10, transparency = 0;
  bool locked = false, clickThrough = false, topmost = false;
  bool seconds = false, twelveHour = false, graph = true, weatherAuto = true;
  bool fahrenheit = false, binaryDisk = false, networkBytes = false;
  std::wstring source, timezone;
  std::wstring region = L"서울특별시 종로구";
  int nx = 60, ny = 127;
  double latitude = 37.573, longitude = 126.979;
  std::vector<std::wstring> calendars;
  int weekStart = 0;
  std::array<bool, 5> systemFields{true, true, true, true, true};
};
struct Settings {
  int version = 1, theme = 1;
  bool edit = false, hidden = false;
  std::vector<Widget> widgets;
};
bool validateWidget(Widget &widget);
struct Monitor {
  std::wstring id, name;
  RECT work{};
  UINT dpi = 96;
  bool primary = false;
};
std::vector<Monitor> monitors();
struct Placement {
  std::wstring id, monitor;
  Box box;
  bool visible = false, temporary = false;
};
std::vector<Placement> arrangeWidgets(const std::vector<Widget> &, const std::vector<Monitor> &);
struct Device {
  std::wstring id, name;
  uint32_t color = 0x78b9ff;
};

struct Sample {
  double start{}, end{}, value{};
  bool valid = true;
};
class History {
public:
  void add(Sample sample);
  const std::deque<Sample> &samples() const { return data_; }
  std::optional<double> average(double now) const;
  std::optional<double> maximum(double now) const;

private:
  std::deque<Sample> data_;
};
struct WeatherHour {
  int64_t time{};
  double temperature = NAN, probability = NAN;
  int sky{}, rain{};
};
struct WeatherData {
  double temperature = NAN, low = NAN, high = NAN, humidity = NAN, wind = NAN;
  int sky{}, rain{};
  int64_t observed{}, fetched{};
  bool night = false, skyForecast = false;
  std::wstring condition;
  std::vector<WeatherHour> hours;
};
struct Event {
  std::wstring id, calendar, title;
  int64_t start{}, end{};
  std::chrono::year_month_day first{}, last{};
  bool allDay = false;
  uint32_t color = 0x78b9ff;
};
struct CalendarData {
  std::vector<Event> events;
  bool connected = false, complete = false;
  int64_t fetched{};
};
struct Snapshot {
  Status status = Status::Loading;
  std::wstring title, detail, extra, message;
  double value = NAN, secondary = NAN, total = NAN, available = NAN;
  double sampledAt{};
  History history, history2;
  std::shared_ptr<const WeatherData> weather;
  std::shared_ptr<const CalendarData> calendar;
};
struct CalendarGrid {
  std::chrono::sys_days first;
  int weeks;
};
struct CalendarSegment {
  size_t event{};
  int week{}, column{}, span{}, lane{};
};
std::vector<CalendarSegment> calendarSegments(const std::vector<Event> &, CalendarGrid,
                                              const std::wstring &zone);
CalendarGrid calendarGrid(int year, unsigned month, int weekStart);
std::chrono::year_month_day localDate(int64_t unixSeconds, const std::wstring &zone = L"");
std::chrono::local_seconds localTime(int64_t unixSeconds, const std::wstring &zone = L"");
int64_t localMidnight(std::chrono::year_month_day date, const std::wstring &zone = L"");
int64_t parseRfc3339(std::string_view value);
std::string rfc3339(int64_t time);
int64_t nowUnix();
double monotonic();
std::pair<double, double> sunriseSunset(std::chrono::year_month_day date, double latitude, double longitude);
bool eventOnDate(const Event &event, std::chrono::year_month_day date, const std::wstring &zone);
} // namespace tempos
