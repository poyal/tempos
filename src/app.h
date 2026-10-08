#pragma once
#include "render.h"
#include "providers.h"
#include "weather.h"
#include "calendar.h"
#include "accessibility.h"
#include <shellapi.h>
namespace tempos {
class App;
struct WidgetWindow {
  App *app{};
  std::wstring id;
  HWND hwnd{};
  RenderSurface surface;
  WidgetAccessibility *accessible{};
  double renderedAt{};
  int64_t contentTime{};
  uint64_t redraws{};
  std::shared_ptr<const Snapshot> displayed;
  bool dragging = false;
  POINT dragStart{};
  RECT original{};
  std::wstring currentMonitor;
  float displayX{}, displayY{};
  bool placed = true;
  UINT dpi = 96;
  ~WidgetWindow();
};
struct Region {
  std::wstring name;
  int nx{}, ny{};
  double lat{}, lon{};
};
class App {
public:
  App(HINSTANCE, std::filesystem::path root, bool fixture, bool visible);
  ~App();
  int run();
  static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM);
  static LRESULT CALLBACK widgetProc(HWND, UINT, WPARAM, LPARAM);
  Widget *find(const std::wstring &);
  void openSettings(const std::wstring &id = {});
  void draw(WidgetWindow &);

private:
  LRESULT message(HWND, UINT, WPARAM, LPARAM);
  LRESULT widgetMessage(WidgetWindow &, UINT, WPARAM, LPARAM);
  void buildSettings();
  void updateViewport();
  void selectTab(int);
  void loadControls();
  void applyControls();
  void command(int, int);
  void rebuild();
  void position(WidgetWindow &);
  void configure();
  void tick();
  void armTimer();
  void add(Kind);
  void removeSelected();
  void save();
  bool commit(Settings);
  void tray(bool add);
  void menu(HWND, bool widget = false);
  void loadRegions();
  void calendarMonth(Widget &, int);
  bool place(Widget &, bool keep = true);
  void updateList();
  void diagnose();
  void details(const Widget &, std::optional<std::chrono::year_month_day> date = {});
  HWND control(int) const;
  std::wstring input(int) const;
  int choice(int) const;
  bool checked(int) const;
  void status(std::wstring);
  HINSTANCE instance_;
  HWND main_{};
  HFONT font_{};
  HICON icon_{};
  UINT taskbarCreated_{};
  NOTIFYICONDATAW tray_{};
  Store store_;
  Settings settings_;
  Renderer renderer_;
  HttpClient http_;
  WeatherService weather_;
  CalendarService calendar_;
  std::unique_ptr<ProviderService> providers_;
  WorkQueue actions_{1};
  std::vector<std::unique_ptr<WidgetWindow>> windows_;
  std::vector<HWND> grids_;
  std::vector<Monitor> monitors_;
  std::vector<Region> regions_;
  std::vector<Device> devices_;
  std::vector<std::pair<HWND, int>> controls_;
  std::vector<int> refreshChoices_;
  std::vector<Size> sizeChoices_;
  std::wstring selected_;
  bool fixture_ = false, visible_ = false, closing_ = false, loading_ = false, suspended_ = false,
       sessionLocked_ = false;
  bool settingsBuilt_ = false;
  UINT settingsDpi_ = 96;
  int tab_ = 0;
  bool trayAdded_ = false;
  uint64_t redraws_ = 0;
  std::wstring status_;
#ifdef TEMPOS_TEST_HOST
  int designCase_ = -1;
#endif
};
} // namespace tempos
