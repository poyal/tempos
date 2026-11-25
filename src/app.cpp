#include "app.h"
#include "test_isolation.h"
#include "update_install.h"
#include <commctrl.h>
#include <windowsx.h>
#include <wtsapi32.h>
#include <psapi.h>
#include <commdlg.h>
#include <fstream>
#include <ctime>
#ifdef TEMPOS_TEST_HOST
#include "api_fixture.h"
#endif
namespace tempos {
constexpr UINT WM_TRAY = WM_APP + 2, WM_ACTION = WM_APP + 3, WM_LOCATION = WM_APP + 5, WM_UPDATE = WM_APP + 6, WM_UPDATE_HEALTH = WM_APP + 7;
static LRESULT CALLBACK gridProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
  if (msg == WM_NCHITTEST)
    return HTTRANSPARENT;
  if (msg == WM_PAINT) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    RECT r{};
    GetClientRect(hwnd, &r);
    FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(150, 180, 220));
    auto old = SelectObject(dc, pen);
    const UINT dpi = GetDpiForWindow(hwnd);
    for (int dip = 0; MulDiv(dip, dpi, 96) < r.right; dip += gridStep) {
      int x = MulDiv(dip, dpi, 96);
      MoveToEx(dc, x, 0, nullptr);
      LineTo(dc, x, r.bottom);
    }
    for (int dip = 0; MulDiv(dip, dpi, 96) < r.bottom; dip += gridStep) {
      int y = MulDiv(dip, dpi, 96);
      MoveToEx(dc, 0, y, nullptr);
      LineTo(dc, r.right, y);
    }
    SelectObject(dc, old);
    DeleteObject(pen);
    EndPaint(hwnd, &paint);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, w, l);
}
static LRESULT CALLBACK detailsProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
  if (msg == WM_CREATE) {
    auto *create = reinterpret_cast<CREATESTRUCTW *>(l);
    auto text = static_cast<const wchar_t *>(create->lpCreateParams);
    auto edit = CreateWindowExW(
        0, L"EDIT", text, WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        12, 12, 1, 1, hwnd, reinterpret_cast<HMENU>(1800), create->hInstance, nullptr);
    SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return 0;
  }
  if (msg == WM_SIZE) {
    MoveWindow(GetDlgItem(hwnd, 1800), 12, 12, std::max(1, int(LOWORD(l)) - 24),
               std::max(1, int(HIWORD(l)) - 24), TRUE);
    return 0;
  }
  if (msg == WM_CLOSE) {
    DestroyWindow(hwnd);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, w, l);
}
WidgetWindow::~WidgetWindow() {
  if (hwnd)
    DestroyWindow(hwnd);
  if (accessible) {
    accessible->disconnect();
    accessible->Release();
  }
}
static HWND desktopHost() {
  static HWND cached = nullptr;
  if (cached && IsWindow(cached))
    return cached;
  HWND progman = FindWindowW(L"Progman", nullptr);
  if (!progman)
    return nullptr;
  // Attach above the icon list within its view. Wallpaper containers may be
  // composited behind that view and hide layered children on recent Windows 11.
  if (auto view = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr)) {
    cached = view;
    return cached;
  }
  HWND result{};
  EnumWindows(
      [](HWND hwnd, LPARAM param) -> BOOL {
        if (auto view = FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr)) {
          *reinterpret_cast<HWND *>(param) = view;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&result));
  cached = result;
  return result;
}
App::App(HINSTANCE instance, std::filesystem::path root, bool fixture, bool visible, bool afterUpdate)
    : instance_(instance), store_(root), settings_(store_.load()),
      startupRoot_(root.empty() ? root : std::filesystem::absolute(root)),
#ifdef TEMPOS_TEST_HOST
      http_(mockApi(store_.root())),
      weather_(store_, http_, fixture), calendar_(store_, http_, fixture, mockBrowser(store_.root())),
#else
      weather_(store_, http_, fixture), calendar_(store_, http_, fixture),
#endif
      fixture_(fixture),
      visible_(visible), afterUpdate_(afterUpdate) {
  if (!renderer_.ready())
    throw std::runtime_error("renderer");
  monitors_ = monitors();
#ifdef TEMPOS_TEST_HOST
  startup_ = StartupRegistration(L"Software\\Tempos\\Tests\\" +
                                 std::to_wstring(std::hash<std::wstring>{}(store_.root().wstring())));
#endif
}
App::~App() {
  closing_ = true;
  location_.cancel();
  http_.cancel();
  if (updates_) updates_->stop();
  actions_.stop();
  if (providers_)
    providers_->stop();
  windows_.clear();
  for (auto hwnd : grids_)
    DestroyWindow(hwnd);
  if (trayAdded_)
    Shell_NotifyIconW(NIM_DELETE, &tray_);
  if (main_) {
    WTSUnRegisterSessionNotification(main_);
    DestroyWindow(main_);
  }
  for (auto brush : {paneBrush_, canvasBrush_, inputBrush_})
    if (brush)
      DeleteObject(brush);
  for (auto font : {titleFont_, sectionFont_, navIconFont_})
    if (font)
      DeleteObject(font);
  if (font_)
    DeleteObject(font_);
}
Widget *App::find(const std::wstring &id) {
  auto i =
      std::find_if(settings_.widgets.begin(), settings_.widgets.end(), [&](auto &w) { return w.id == id; });
  return i == settings_.widgets.end() ? nullptr : &*i;
}
HWND App::control(int id) const {
  return GetDlgItem(main_, id);
}
std::wstring App::input(int id) const {
  auto hwnd = control(id);
  int n = GetWindowTextLengthW(hwnd);
  std::wstring s(size_t(n) + 1, L'\0');
  GetWindowTextW(hwnd, s.data(), n + 1);
  s.resize(n);
  return s;
}
int App::choice(int id) const {
  return int(SendMessageW(control(id), CB_GETCURSEL, 0, 0));
}
bool App::checked(int id) const {
  return SendMessageW(control(id), BM_GETCHECK, 0, 0) == BST_CHECKED;
}
void App::status(std::wstring message) {
  status_ = std::move(message);
  SetWindowTextW(control(1999), status_.c_str());
}
int App::run() {
  auto managementClass = managementWindowClass(store_.root());
  icon_ = LoadIconW(instance_, MAKEINTRESOURCEW(101));
  WNDCLASSEXW wc{sizeof wc};
  wc.hInstance = instance_;
  wc.hIcon = icon_;
  wc.hIconSm = icon_;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
  wc.lpfnWndProc = windowProc;
  wc.lpszClassName = managementClass.c_str();
  RegisterClassExW(&wc);
  wc.hbrBackground = nullptr;
  wc.lpfnWndProc = widgetProc;
  wc.lpszClassName = L"Tempos.Widget";
  wc.style = CS_DBLCLKS;
  RegisterClassExW(&wc);
  wc.lpfnWndProc = gridProc;
  wc.lpszClassName = L"Tempos.Grid";
  wc.style = 0;
  RegisterClassExW(&wc);
  wc.lpfnWndProc = detailsProc;
  wc.lpszClassName = L"Tempos.Details";
  wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
  RegisterClassExW(&wc);
  taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
  main_ = CreateWindowExW(0, managementClass.c_str(), L"Tempos · 위젯 설정",
                          WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME |
                              WS_VSCROLL | WS_CLIPCHILDREN,
                          CW_USEDEFAULT, CW_USEDEFAULT, 880, 790, nullptr, nullptr, instance_, this);
  if (!main_)
    return 1;
  UINT dpi = GetDpiForWindow(main_);
  RECT size{0, 0, MulDiv(1024, dpi, 96), MulDiv(780, dpi, 96)};
  AdjustWindowRectExForDpi(&size, DWORD(GetWindowLongPtrW(main_, GWL_STYLE)), FALSE, 0, dpi);
  MONITORINFO screen{sizeof screen};
  GetMonitorInfoW(MonitorFromWindow(main_, MONITOR_DEFAULTTONEAREST), &screen);
  const int width = std::min(size.right - size.left, screen.rcWork.right - screen.rcWork.left);
  const int height = std::min(size.bottom - size.top, screen.rcWork.bottom - screen.rcWork.top);
  SetWindowPos(main_, nullptr, screen.rcWork.left + (screen.rcWork.right - screen.rcWork.left - width) / 2,
               screen.rcWork.top + (screen.rcWork.bottom - screen.rcWork.top - height) / 2, width, height,
               SWP_NOZORDER);
  font_ = CreateFontW(-MulDiv(14, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  ProviderService::TemperatureRead temperatureFixture;
#ifdef TEMPOS_TEST_HOST
  temperatureFixture = [this](Kind kind) {
    auto response = boundedRead(store_.root() / L"sensor-scenario.json", 4096);
    return parseCpuTemperature(response.value_or(Json{{"version", 1}, {"status", "ready"},
        {"celsius", kind == Kind::Cpu ? 45 : 52}, {"ageMs", 0}}.dump()), monotonic());
  };
#endif
  providers_ = std::make_unique<ProviderService>(
      main_,
      [this](const Widget &w, std::stop_token stop, WeatherProvider provider) {
        return w.kind == Kind::Weather ? weather_.read(w, stop, provider) : calendar_.read(w, stop);
      },
      fixture_, std::move(temperatureFixture));
  WTSRegisterSessionNotification(main_, NOTIFY_FOR_THIS_SESSION);
  tray(true);
  updates_ = std::make_unique<UpdateService>(http_, store_.root(), [this] { PostMessageW(main_, WM_UPDATE, 0, 0); }, TEMPOS_VERSION_STRING, updatePublicKey());
  rebuild();
  if (visible_ || !store_.warning.empty() || !trayAdded_)
    openSettings();
  if (!store_.warning.empty())
    status(store_.warning);
  if (afterUpdate_) {
    openSettings();
    selectTab(4);
  }
  if (settings_.checkUpdatesOnStart) updates_->check();
  if (afterUpdate_) PostMessageW(main_, WM_UPDATE_HEALTH, 0, 0);
  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    if (message.message == WM_KEYDOWN && message.wParam == VK_TAB &&
        (message.hwnd == main_ || IsChild(main_, message.hwnd)) && !tabStops_.empty()) {
      int step = GetKeyState(VK_SHIFT) < 0 ? -1 : 1;
      auto current = std::find_if(tabStops_.begin(), tabStops_.end(), [&](HWND item) {
        return item == message.hwnd || IsChild(item, message.hwnd);
      });
      int count = int(tabStops_.size()),
          index = current == tabStops_.end() ? (step > 0 ? -1 : 0) : int(current - tabStops_.begin());
      for (int i = 0; i < count; ++i) {
        index = (index + count + step) % count;
        auto target = tabStops_[index];
        if (IsWindowEnabled(target)) {
          SetFocus(target);
          break;
        }
      }
      continue;
    }
    if (!IsDialogMessageW(main_, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
  return int(message.wParam);
}
LRESULT CALLBACK App::windowProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
  App *app = reinterpret_cast<App *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    app = static_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    app->main_ = hwnd;
  }
  return app ? app->message(hwnd, msg, w, l) : DefWindowProcW(hwnd, msg, w, l);
}
LRESULT CALLBACK App::widgetProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
  auto *widget = reinterpret_cast<WidgetWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    widget = static_cast<WidgetWindow *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(widget));
    widget->hwnd = hwnd;
  }
  return widget ? widget->app->widgetMessage(*widget, msg, w, l) : DefWindowProcW(hwnd, msg, w, l);
}
void App::updateControls() {
  if (!updates_ || !control(1700)) return;
  auto state = updates_->state();
  auto message = updateInstallError_.empty() ? state.message : updateInstallError_;
  if (state.phase == UpdatePhase::Downloading && state.release)
    message += L" " + std::to_wstring(state.received * 100 / state.release->bytes) + L"%";
  SetWindowTextW(control(1701), message.c_str());
  std::wstring checked = L"아직 확인하지 않았습니다.";
  if (state.checkedAt) {
    std::time_t stamp = state.checkedAt; std::tm local{}; wchar_t formatted[64]{};
    if (localtime_s(&local, &stamp) == 0 && std::wcsftime(formatted, std::size(formatted), L"%Y-%m-%d %H:%M:%S", &local))
      checked = L"마지막 확인: " + std::wstring(formatted);
  }
  SetWindowTextW(control(1709), checked.c_str());
  EnableWindow(control(1702), !state.busy() && state.phase != UpdatePhase::Ready);
  SetWindowTextW(control(1703), state.phase == UpdatePhase::Ready ? L"재시작하여 적용" : L"업데이트 다운로드");
  bool installed = installedUpdateTarget(runningExecutable());
  EnableWindow(control(1703), installed && !state.busy() && !installRequested_ && state.release &&
      state.release->installable() && compareVersions(state.release->version, TEMPOS_VERSION_STRING) > 0);
  EnableWindow(control(1704), state.busy());
  EnableWindow(control(1705), state.release.has_value());
  SendMessageW(control(1713), BM_SETCHECK, settings_.checkUpdatesOnStart ? BST_CHECKED : BST_UNCHECKED, 0);
  SetWindowTextW(control(1710), installed
      ? L"패치를 내려받은 뒤 원하는 때 재시작하여 적용하세요.\r\n다운로드 중에도 위젯을 사용할 수 있습니다.\r\n저장한 위젯 설정과 연동 정보는 유지됩니다."
      : L"무설치 실행 중입니다. 변경 내역에서 새 ZIP을 받아 앱을 종료한 뒤 교체해 주세요.\r\n등록된 설치본은 이 창에서 변경하지 않습니다.");
  if (auto previous = boundedRead(store_.root() / L"update-result.json", 16384)) {
    try {
      auto result = Json::parse(*previous);
      auto resultMessage = L"최근 업데이트 · " + wide(result.value("previous", std::string())) + L" → " + wide(result.at("version").get<std::string>()) + L"\r\n" + wide(result.at("message").get<std::string>());
      SetWindowTextW(control(1714), resultMessage.c_str());
    } catch (...) { SetWindowTextW(control(1714), L"이전 업데이트 결과를 읽을 수 없습니다."); }
  }
}
void App::openAboutLink(int id) {
  std::wstring url;
  if (id == 1706) url = L"https://github.com/poyal/tempos";
  if (id == 1707) url = L"https://github.com/poyal/tempos/issues";
  if (id == 1708) url = L"https://github.com/poyal/tempos/blob/main/README.md";
  if (id == 1712) url = L"mailto:poyal.work@gmail.com";
  if (id == 1705 && updates_) { auto s = updates_->state(); if (s.release) url = s.release->page; }
  if (url.empty()) return;
#ifdef TEMPOS_TEST_HOST
  atomicWrite(store_.root() / L"about-link.json", Json{{"url", utf8(url)}, {"mode", "mock"}}.dump());
  status(L"목업: About 링크 대상을 확인했습니다.");
#else
  if (reinterpret_cast<INT_PTR>(ShellExecuteW(main_, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
    status(L"링크를 열지 못했습니다. 기본 브라우저 또는 메일 앱을 확인해 주세요.");
#endif
}
void App::sensorControls() {
  if (!control(1510)) return;
  SendMessageW(control(1510), BM_SETCHECK, settings_.cpuTemperatureEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
  std::wstring state = settings_.cpuTemperatureEnabled ? L"CPU 온도 수집 켜짐" : L"CPU 온도 꺼짐 · 설치 시 관리자 승인 필요";
  if (settings_.cpuTemperatureEnabled && providers_)
    for (const auto &widget : settings_.widgets)
      if (widget.kind == Kind::Cpu) {
        auto data = providers_->snapshot(widget);
        state = temperatureReason(data->temperature.status);
        if (validTemperature(data->temperature, monotonic(), widget.interval))
          state += L" · " + number(data->temperature.celsius) + L"°C";
        break;
      }
  if (sensorSetup_) state = L"센서 도우미 설치 프로그램이 실행 중입니다.";
  SetWindowTextW(control(1511), state.c_str());
  for (int id : {1510, 1512, 1513}) EnableWindow(control(id), !sensorSetup_);
}
static bool sensorServiceRunning() {
  auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!manager) return false;
  auto service = OpenServiceW(manager, L"TemposSensors", SERVICE_QUERY_STATUS);
  SERVICE_STATUS status{};
  bool running = service && QueryServiceStatus(service, &status) && status.dwCurrentState == SERVICE_RUNNING;
  if (service) CloseServiceHandle(service);
  CloseServiceHandle(manager);
  return running;
}
void App::startSensorSetup(bool removing) {
  if (sensorSetup_) return;
  sensorRemoving_ = removing;
#ifdef TEMPOS_TEST_HOST
  // TestHost never installs/removes a service or driver, including --live-system.
  DWORD result = ERROR_CANCELLED;
  if (auto bytes = boundedRead(store_.root() / L"sensor-setup-scenario.json", 4096)) {
    try { result = Json::parse(*bytes).at("exitCode").get<DWORD>(); } catch (...) {}
  }
  completeSensorSetup(result);
#else
  wchar_t module[32768]{};
  if (!GetModuleFileNameW(nullptr, module, DWORD(std::size(module)))) return;
  auto exe = std::filesystem::path(module).parent_path() / L"Tempos.Sensors.Setup.exe";
  if (!std::filesystem::is_regular_file(exe)) {
    status(L"센서 설치 프로그램이 없어요. 센서 구성요소가 포함된 배포본을 사용하세요.");
    sensorControls();
    return;
  }
  SHELLEXECUTEINFOW info{sizeof info};
  info.fMask = SEE_MASK_NOCLOSEPROCESS;
  info.hwnd = main_;
  info.lpVerb = L"runas";
  info.lpFile = exe.c_str();
  info.lpParameters = removing ? L"/REMOVE" : nullptr;
  info.nShow = SW_SHOWNORMAL;
  if (!ShellExecuteExW(&info) || !info.hProcess) {
    completeSensorSetup(GetLastError());
    return;
  }
  sensorSetup_ = info.hProcess;
  SetTimer(main_, 2, 500, nullptr);
  sensorControls();
#endif
}
void App::completeSensorSetup(DWORD result) {
  bool success = result == ERROR_SUCCESS;
#ifndef TEMPOS_TEST_HOST
  if (success && !sensorRemoving_) success = sensorServiceRunning();
#endif
  if (success) {
    auto next = settings_;
    next.cpuTemperatureEnabled = !sensorRemoving_;
    if (commit(std::move(next))) {
      configure();
      providers_->invalidate(Kind::Cpu);
      status(sensorRemoving_ ? L"센서 도우미를 제거했어요. 공유 PawnIO 드라이버는 유지합니다."
                             : L"센서 도우미를 설치하고 CPU 온도를 켰어요.");
    }
  } else
    status(result == ERROR_CANCELLED || result == 2 ? L"센서 설치를 취소했어요. 기존 설정을 유지합니다."
           : result == ERROR_SUCCESS_REBOOT_REQUIRED ? L"센서 설치 후 Windows 재시작이 필요합니다. 재시작 후 CPU 온도를 켜세요."
           : L"센서 도우미 설치 실패 · 기존 설정을 유지합니다. 설치 / 복구로 다시 시도하세요.");
  sensorControls();
}
void App::weatherControls() {
  SendMessageW(control(1403), CB_SETCURSEL, int(settings_.weatherProvider), 0);
  bool kma = settings_.weatherProvider == WeatherProvider::Kma;
  EnableWindow(control(1400), kma);
  EnableWindow(control(1401), kma);
  SetWindowTextW(control(1405),
                 kma ? L"기상청: 개인 인증키 필요 · 외부 요청 최소 10분 (5분 선택 시 캐시 사용)"
                     : L"Open-Meteo: 키 없이 사용 · 비상업적 이용 · 외부 요청 최소 15분");
}
void App::findLocation() {
  auto *w = find(selected_);
  if (!w || w->kind != Kind::Weather || !locationTarget_.empty())
    return;
  locationTarget_ = selected_;
  locationRegionChoice_ = choice(1314);
  EnableWindow(control(1320), FALSE);
  status(L"현재 위치를 한 번 찾고 있어요…");
#ifdef TEMPOS_TEST_HOST
  {
    LocationResult result{35.1796, 129.0756, {}};
    try {
      if (auto text = boundedRead(store_.root() / L"location-scenario.json")) {
        auto scenario = Json::parse(*text);
        result.latitude = scenario.value("latitude", result.latitude);
        result.longitude = scenario.value("longitude", result.longitude);
        result.error = wide(scenario.value("error", std::string{}));
      }
    } catch (...) {
      result.error = L"테스트 위치를 읽을 수 없음";
    }
    applyLocation(std::move(result));
    return;
  }
#endif
  location_.start(main_, WM_LOCATION);
}
void App::applyLocation(LocationResult result) {
  bool same = selected_ == locationTarget_ && choice(1314) == locationRegionChoice_;
  locationTarget_.clear();
  EnableWindow(control(1320), TRUE);
  layoutWidgetFields();
  if (!same || !find(selected_) || find(selected_)->kind != Kind::Weather) {
    status(L"선택한 위젯이나 지역이 바뀌어 위치 결과를 적용하지 않았어요.");
    return;
  }
  if (!result.error.empty()) {
    status(std::move(result.error));
    return;
  }
  auto index = nearestRegion(regions_, result.latitude, result.longitude);
  if (!index) {
    status(L"가까운 국내 날씨 지역이 없어요. 지역을 직접 선택하세요.");
    return;
  }
  SendMessageW(control(1314), CB_SETCURSEL, *index, 0);
  editorChanged();
  status(regions_[*index].name + L"을 선택했어요. 적용을 누르면 이 지역을 계속 사용합니다.");
}
void App::updateList() {
  if (!settingsBuilt_)
    return;
  SendMessageW(control(1500), CB_SETCURSEL, settings_.theme, 0);
  auto list = control(1200);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  for (size_t i = 0; i < settings_.widgets.size(); ++i) {
    auto &w = settings_.widgets[i];
    auto name = (w.alias.empty() ? std::wstring(kindName(w.kind)) : w.alias) + L" · " +
                std::wstring(sizeName(w.size));
    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    if (w.id == selected_)
      SendMessageW(list, LB_SETCURSEL, i, 0);
  }
  if (!find(selected_) && !settings_.widgets.empty()) {
    selected_ = settings_.widgets.front().id;
    SendMessageW(list, LB_SETCURSEL, 0, 0);
  }
  if (!dirty_ || !find(selected_))
    loadControls();
  if (tab_ == 3)
    refreshLayouts();
}
void App::loadControls() {
  loading_ = true;
  dirty_ = false;
  errorField_ = 0;
  auto *w = find(selected_);
  for (int id = 1300; id <= 1320; ++id)
    EnableWindow(control(id), w != nullptr);
  if (!w) {
    layoutWidgetFields();
    loading_ = false;
    return;
  }
  auto clear = [&](int id) { SendMessageW(control(id), CB_RESETCONTENT, 0, 0); };
  auto item = [&](int id, const std::wstring &t) {
    SendMessageW(control(id), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t.c_str()));
  };
  auto set = [&](int id, int index) { SendMessageW(control(id), CB_SETCURSEL, index, 0); };
  sizeChoices_ = {Size::S, Size::Slim, Size::M, Size::L};
  if (w->kind == Kind::Calendar)
    sizeChoices_.push_back(Size::XL);
  clear(1300);
  for (auto size : sizeChoices_)
    item(1300, std::wstring(sizeName(size)));
  set(1300, int(std::find(sizeChoices_.begin(), sizeChoices_.end(), w->size) - sizeChoices_.begin()));
  set(1301, w->scale == 100 ? 0 : w->scale == 150 ? 1 : 2);
  set(1302, w->theme + 1);
  refreshChoices_ = intervals(w->kind);
  clear(1303);
  for (int interval : refreshChoices_)
    item(1303, interval >= 60 ? std::to_wstring(interval / 60) + L"분" : std::to_wstring(interval) + L"초");
  set(1303,
      int(std::find(refreshChoices_.begin(), refreshChoices_.end(), w->interval) - refreshChoices_.begin()));
  clear(1304);
  for (size_t i = 0; i < monitors_.size(); ++i) {
    auto &m = monitors_[i];
    item(1304, m.name + L" · " + std::to_wstring(m.dpi * 100 / 96) + L"%");
    if (w->monitor == m.id || (w->monitor.empty() && m.primary))
      set(1304, int(i));
  }
  SetWindowTextW(control(1305), std::to_wstring(w->backgroundTransparency).c_str());
  SetWindowTextW(control(1306), std::to_wstring(w->transparency).c_str());
  auto check = [&](int id, bool yes) {
    SendMessageW(control(id), BM_SETCHECK, yes ? BST_CHECKED : BST_UNCHECKED, 0);
  };
  SetWindowTextW(control(1324), w->alias.c_str());
  check(1600, w->twelveHour);
  check(1601, w->fahrenheit);
  check(1602, w->networkBytes);
  check(1603, w->binaryDisk);
  check(1604, w->weekStart == 1);
  for (int i = 0; i < 5; ++i)
    check(1610 + i, w->systemFields[i]);
  check(1307, w->clickThrough);
  check(1308, w->locked);
  check(1309, w->topmost);
  check(1310, w->seconds);
  check(1311, w->graph);
  check(1312, w->weatherAuto);
  EnableWindow(control(1310), w->kind == Kind::Clock);
  EnableWindow(control(1312), w->kind == Kind::Weather);
  devices_.clear();
  clear(1313);
  if (providers_ && (w->kind == Kind::Gpu || w->kind == Kind::Network || w->kind == Kind::Disk))
    devices_ = providers_->devices(w->kind);
  if (!w->source.empty() && (w->kind == Kind::Gpu || w->kind == Kind::Network || w->kind == Kind::Disk) &&
      std::none_of(devices_.begin(), devices_.end(),
                   [&](const Device &device) { return device.id == w->source; }))
    devices_.push_back({w->source, L"연결되지 않은 장치 · " + w->source});
  item(1313, w->kind == Kind::Network ? L"전체 네트워크" : L"기본 장치");
  set(1313, 0);
  for (size_t i = 0; i < devices_.size(); ++i) {
    item(1313, devices_[i].name);
    if (devices_[i].id == w->source)
      set(1313, int(i) + 1);
  }
  EnableWindow(control(1313), w->kind == Kind::Gpu || w->kind == Kind::Network || w->kind == Kind::Disk);
  for (size_t i = 0; i < regions_.size(); ++i) {
    if (regions_[i].name == w->region)
      set(1314, int(i));
  }
  EnableWindow(control(1314), w->kind == Kind::Weather);
  SetWindowTextW(control(1315), w->timezone.c_str());
  EnableWindow(control(1315), w->kind == Kind::Clock || w->kind == Kind::Calendar);
  for (int id = 1317; id <= 1319; ++id)
    EnableWindow(control(id), w->kind == Kind::Calendar && (w->size == Size::L || w->size == Size::XL));
  layoutWidgetFields();
  loading_ = false;
}
bool App::place(Widget &w, bool keep) {
  if (monitors_.empty())
    return false;
  auto mi = std::find_if(monitors_.begin(), monitors_.end(), [&](auto &m) { return m.id == w.monitor; });
  if (mi == monitors_.end())
    mi = monitors_.begin();
  float factor = 96.f / mi->dpi;
  Extent area{(mi->work.right - mi->work.left) * factor, (mi->work.bottom - mi->work.top) * factor};
  auto size = extent(w.size, w.scale);
  std::vector<Box> occupied;
  // Temporarily relocated widgets still occupy their displayed monitor, even
  // though their saved monitor/position must remain available for reconnection.
  for (auto &other : arrangeWidgets(settings_.widgets, monitors_))
    if (other.id != w.id && other.visible && other.monitor == mi->id)
      occupied.push_back(other.box);
  Box proposed{snap(w.x), snap(w.y), size.width, size.height};
  bool valid =
      proposed.x >= 16 && proposed.y >= 16 && proposed.x + proposed.w <= area.width - 16 &&
      proposed.y + proposed.h <= area.height - 16 &&
      !std::any_of(occupied.begin(), occupied.end(), [&](auto &box) { return overlaps(box, proposed); });
  if (!keep || !valid) {
    auto position = keep ? nearestPosition(area, proposed, occupied) : freePosition(area, size, occupied);
    if (!position)
      return false;
    w.x = position->x;
    w.y = position->y;
  } else {
    w.x = proposed.x;
    w.y = proposed.y;
  }
  w.monitor = mi->id;
  return true;
}
std::optional<Widget> App::editorValue(int &field, std::wstring &error) {
  auto *current = find(selected_);
  if (!current)
    return {};
  field = 1300;
  error = L"입력값을 확인하세요.";
  Widget w = *current;
  try {
    int size = choice(1300), scale = choice(1301), refresh = choice(1303), monitor = choice(1304);
    if (size < 0 || size >= int(sizeChoices_.size()) || refresh < 0 || refresh >= int(refreshChoices_.size()))
      throw std::runtime_error("selection");
    w.size = sizeChoices_[size];
    w.scale = scale == 0 ? 100 : scale == 1 ? 150 : 200;
    w.theme = choice(1302) - 1;
    w.interval = refreshChoices_[refresh];
    if (monitor >= 0 && monitor < int(monitors_.size()))
      w.monitor = monitors_[monitor].id;
    auto integer = [&](int id) {
      auto value = input(id);
      size_t end = 0;
      int n = std::stoi(value, &end);
      if (end != value.size())
        throw std::runtime_error("integer");
      return n;
    };
    auto percent = [&](int id) {
      field = id;
      error = L"입력값은 0부터 100까지의 정수여야 합니다.";
      int n = integer(id);
      if (n < 0 || n > 100)
        throw std::runtime_error("percentage");
      return n;
    };
    w.backgroundTransparency = percent(1305);
    w.transparency = percent(1306);
    w.alias = input(1324);
    switch (w.kind) {
    case Kind::Clock:
      w.twelveHour = checked(1600);
      break;
    case Kind::Weather:
      w.fahrenheit = checked(1601);
      break;
    case Kind::Network:
      w.networkBytes = checked(1602);
      break;
    case Kind::Disk:
      w.binaryDisk = checked(1603);
      break;
    case Kind::Calendar:
      w.weekStart = checked(1604) ? 1 : 0;
      break;
    case Kind::System:
      for (int i = 0; i < 5; ++i)
        w.systemFields[i] = checked(1610 + i);
      break;
    default:
      break;
    }
    w.clickThrough = checked(1307);
    w.locked = checked(1308);
    w.topmost = checked(1309);
    w.seconds = checked(1310);
    if (w.kind == Kind::Clock) {
      if (w.seconds != current->seconds)
        w.interval = w.seconds ? 1 : 60;
      else
        w.seconds = w.interval == 1;
    }
    w.graph = checked(1311);
    w.weatherAuto = checked(1312);
    if (w.kind == Kind::Gpu || w.kind == Kind::Network || (w.kind == Kind::Disk && w.size != Size::L)) {
      int i = choice(1313);
      w.source = i > 0 && i <= int(devices_.size()) ? devices_[i - 1].id : L"";
    }
    if (w.kind == Kind::Weather) {
      int i = choice(1314);
      if (i >= 0 && i < int(regions_.size()) && regions_[i].name != w.region) {
        auto &r = regions_[i];
        w.region = r.name;
        w.nx = r.nx;
        w.ny = r.ny;
        w.latitude = r.lat;
        w.longitude = r.lon;
      }
    }
    field = 1315;
    error = L"시간대 입력값을 확인하세요. 예: Asia/Seoul";
    w.timezone = input(1315);
    if (!w.timezone.empty())
      static_cast<void>(std::chrono::locate_zone(utf8(w.timezone)));
    field = 1300;
    error = L"선택한 설정의 입력값을 확인하세요.";
    if (!validateWidget(w))
      throw std::runtime_error("values");
    return w;
  } catch (...) {
    return {};
  }
}
bool App::applyControls() {
  int field = 0;
  std::wstring error;
  auto w = editorValue(field, error);
  auto current = find(selected_);
  if (!current)
    return false;
  if (w && !place(*w)) {
    w.reset();
    field = 1300;
    error = L"선택한 크기를 배치할 빈 공간이 없습니다.";
  }
  if (!w) {
    errorField_ = field;
    SetWindowTextW(control(1231), error.c_str());
    status(error);
    updateViewport();
    focusField(control(1231));
    SetFocus(control(field));
    return false;
  }
  auto next = settings_;
  next.widgets[size_t(current - settings_.widgets.data())] = std::move(*w);
  if (!commit(std::move(next)))
    return false;
  dirty_ = false;
  rebuild();
  status(L"설정을 적용했어요.");
  return true;
}
void App::save() {
  if (!store_.save(settings_))
    status(L"설정 저장 실패 · 데이터 폴더 권한을 확인하세요.");
}
bool App::commit(Settings next) {
  if (!store_.save(next)) {
    status(L"설정 저장 실패 · 기존 설정을 유지했어요. 데이터 폴더 권한을 확인하세요.");
    return false;
  }
  settings_ = std::move(next);
  return true;
}
void App::add(Kind kind) {
  if (settings_.widgets.size() >= 64) {
    status(L"최대 64개까지 추가할 수 있어요.");
    return;
  }
  Widget w;
  w.kind = kind;
  w.interval = defaultInterval(kind);
  w.theme = -1;
  bool found = false;
  for (auto &m : monitors_) {
    w.monitor = m.id;
    if (place(w, false)) {
      found = true;
      break;
    }
  }
  if (!found) {
    w.size = Size::S;
    for (auto &m : monitors_) {
      w.monitor = m.id;
      if (place(w, false)) {
        found = true;
        break;
      }
    }
  }
  if (!found) {
    status(L"위젯을 놓을 빈 공간이 없어요. 크기를 줄이거나 기존 위젯을 이동하세요.");
    return;
  }
  auto next = settings_;
  next.widgets.push_back(w);
  if (!commit(std::move(next)))
    return;
  selected_ = w.id;
  rebuild();
  status(L"위젯을 추가했어요.");
}
void App::removeSelected() {
  auto next = settings_;
  next.widgets.erase(
      std::remove_if(next.widgets.begin(), next.widgets.end(), [&](auto &w) { return w.id == selected_; }),
      next.widgets.end());
  if (!commit(std::move(next)))
    return;
  selected_.clear();
  rebuild();
  status(L"위젯을 제거했어요.");
}
void App::rebuild() {
  for (auto &window : windows_) {
    if (window->dragging) {
      window->dragging = false;
      if (GetCapture() == window->hwnd) ReleaseCapture();
    }
  }
  monitors_ = monitors();
  for (auto hwnd : grids_)
    DestroyWindow(hwnd);
  grids_.clear();
  if (settings_.edit && !settings_.hidden)
    for (auto &m : monitors_) {
      auto hwnd =
          CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
                              WS_EX_TOPMOST,
                          L"Tempos.Grid", L"", WS_POPUP, m.work.left, m.work.top, m.work.right - m.work.left,
                          m.work.bottom - m.work.top, nullptr, nullptr, instance_, nullptr);
      SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 95, LWA_COLORKEY | LWA_ALPHA);
      SetWindowPos(hwnd, HWND_TOPMOST, m.work.left, m.work.top, m.work.right - m.work.left,
                   m.work.bottom - m.work.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
      grids_.push_back(hwnd);
    }
  windows_.erase(std::remove_if(windows_.begin(), windows_.end(), [&](auto &w) { return !find(w->id); }),
                 windows_.end());
  for (auto &w : settings_.widgets) {
    if (w.monitor.empty() && !monitors_.empty())
      w.monitor = monitors_.front().id;
    auto i = std::find_if(windows_.begin(), windows_.end(), [&](auto &p) { return p->id == w.id; });
    if (i == windows_.end()) {
      auto window = std::make_unique<WidgetWindow>();
      window->app = this;
      window->id = w.id;
      windows_.push_back(std::move(window));
      i = windows_.end() - 1;
    }
    auto &window = **i;
    window.displayed.reset();
    if (!window.hwnd) {
      CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
                          (renderer_.layered() ? WS_EX_LAYERED : WS_EX_NOREDIRECTIONBITMAP),
                      L"Tempos.Widget", std::wstring(kindName(w.kind)).c_str(), WS_POPUP, 0, 0, 1, 1, nullptr,
                      nullptr, instance_, &window);
      if (window.hwnd)
        window.accessible = new WidgetAccessibility(window.hwnd, w.id);
    }
    position(window);
    ShowWindow(window.hwnd, settings_.hidden || !window.placed ? SW_HIDE : SW_SHOWNOACTIVATE);
  }
  configure();
  for (auto &window : windows_)
    if (!settings_.hidden && window->placed)
      draw(*window);
  updateList();
  armTimer();
  // Keep the settings controls accessible above the temporary edit layer.
  SetWindowPos(main_, settings_.edit && !settings_.hidden ? HWND_TOPMOST : HWND_NOTOPMOST,
               0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}
void App::position(WidgetWindow &window) {
  auto *w = find(window.id);
  if (!w || !window.hwnd || monitors_.empty() || window.positioning)
    return;
  // SetParent can synchronously notify a DPI change on a secondary monitor.
  // Do not reparent/paint again while this transition is only half complete.
  window.positioning = true;
  struct PositionGuard {
    bool &active;
    ~PositionGuard() { active = false; }
  } guard{window.positioning};
  auto placements = arrangeWidgets(settings_.widgets, monitors_);
  auto placement = std::find_if(placements.begin(), placements.end(), [&](auto &p) { return p.id == w->id; });
  window.placed = placement != placements.end() && placement->visible;
  if (!window.placed) {
    ShowWindow(window.hwnd, SW_HIDE);
    status(L"화면 공간이 부족한 위젯은 설정에서 크기나 모니터를 변경하세요.");
    return;
  }
  auto mi =
      std::find_if(monitors_.begin(), monitors_.end(), [&](auto &m) { return m.id == placement->monitor; });
  window.currentMonitor = mi->id;
  window.dpi = mi->dpi;
  auto extentDip = extent(w->size, w->scale);
  window.displayX = placement->box.x;
  window.displayY = placement->box.y;
  HWND parent = w->topmost || settings_.edit ? nullptr : desktopHost();
  if (parent && !AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(window.hwnd),
                                              GetWindowDpiAwarenessContext(parent))) {
    parent = nullptr;
  }
  // Clear the native topmost state before returning a popup to the desktop.
  // Changing GWL_EXSTYLE alone does not update its position in the Z order.
  bool topmost = w->topmost || settings_.edit;
  if (!topmost && (GetWindowLongPtrW(window.hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST))
    SetWindowPos(window.hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  if (GetParent(window.hwnd) != parent) {
    // Reparenting between Explorer's DPI and a different monitor can leave
    // DWM displaying an empty layered surface on the next edit transition.
    // Reset the native layer before attaching it to its new coordinate space.
    if (renderer_.layered())
      SetWindowLongPtrW(window.hwnd, GWL_EXSTYLE,
                         GetWindowLongPtrW(window.hwnd, GWL_EXSTYLE) & ~WS_EX_LAYERED);
    auto style = GetWindowLongPtrW(window.hwnd, GWL_STYLE);
    if (parent)
      SetWindowLongPtrW(window.hwnd, GWL_STYLE, (style & ~WS_POPUP) | WS_CHILD);
    SetParent(window.hwnd, parent);
    if (!parent)
      SetWindowLongPtrW(window.hwnd, GWL_STYLE, (style & ~WS_CHILD) | WS_POPUP);
  }
  LONG_PTR ex =
      WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | (renderer_.layered() ? WS_EX_LAYERED : WS_EX_NOREDIRECTIONBITMAP);
  // Preserve native topmost state here; SetWindowPos owns changes to the band.
  ex |= GetWindowLongPtrW(window.hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST;
  if (w->clickThrough && !settings_.edit)
    ex |= WS_EX_TRANSPARENT;
  SetWindowLongPtrW(window.hwnd, GWL_EXSTYLE, ex);
  int left = MulDiv(int(window.displayX), mi->dpi, 96);
  int top = MulDiv(int(window.displayY), mi->dpi, 96);
  int right = MulDiv(int(window.displayX + extentDip.width), mi->dpi, 96);
  int bottom = MulDiv(int(window.displayY + extentDip.height), mi->dpi, 96);
  POINT origin{mi->work.left + left, mi->work.top + top};
  if (parent)
    ScreenToClient(parent, &origin);
  SetWindowPos(window.hwnd,
               parent           ? HWND_TOP
               : topmost        ? HWND_TOPMOST
                                : HWND_BOTTOM,
               origin.x, origin.y, right - left, bottom - top,
               SWP_NOACTIVATE | SWP_FRAMECHANGED);
}
void App::configure() {
  if (!providers_)
    return;
  std::vector<Widget> active;
  for (auto &w : settings_.widgets)
    if (std::any_of(windows_.begin(), windows_.end(),
                    [&](auto &window) { return window->id == w.id && window->placed; }))
      active.push_back(w);
  SYSTEM_POWER_STATUS power{};
  bool saving = GetSystemPowerStatus(&power) && power.SystemStatusFlag == 1;
  providers_->configure(active, settings_.hidden || suspended_ || sessionLocked_, saving,
                         settings_.weatherProvider, settings_.cpuTemperatureEnabled);
}
void App::draw(WidgetWindow &window) {
  auto *w = find(window.id);
  if (!w || !window.hwnd || !window.placed || window.positioning)
    return;
  if (!window.displayed && providers_)
    window.displayed = providers_->snapshot(*w);
  auto data = window.displayed;
#ifdef TEMPOS_TEST_HOST
  // Visual fixtures are compiled only into the test host; production cannot enable them.
  if (fixture_ && designCase_ >= 0) {
    auto sample = std::make_shared<Snapshot>(providers_ ? *providers_->snapshot(*w)
                                             : data     ? *data
                                                        : Snapshot{});
    sample->status = Status::Ready;
    sample->title = w->kind == Kind::System ? L"TEMPOS-WORKSTATION"
                    : w->kind == Kind::Cpu  ? L"AMD Ryzen 9 9950X 16-Core Processor"
                    : w->kind == Kind::Gpu  ? L"NVIDIA GeForce RTX 5070 Ti"
                    : w->kind == Kind::Disk ? L"C:\\"
                                            : L"Ethernet 2 · Realtek PCIe";
    sample->detail = L"Windows 11 Pro 24H2";
    if (w->kind == Kind::Cpu || w->kind == Kind::Gpu)
      sample->temperature = {w->kind == Kind::Cpu ? 45. : 52., TemperatureStatus::Ready, monotonic()};
    if (designCase_ == 30) sample->temperature = {};
    if (designCase_ == 31) {
      sample->value = 100;
      sample->temperature = {105, TemperatureStatus::Ready, monotonic()};
      sample->title = w->kind == Kind::Cpu ? L"AMD Ryzen 7 9700X 8-Core Processor" :
                                           L"NVIDIA GeForce RTX 4070 Ti SUPER";
    }
    if (designCase_ == 32) sample->temperature = {NAN, TemperatureStatus::Stale, 0};
    if (designCase_ >= 33 && designCase_ <= 36) {
      // Explicit design examples, not sensor/API integration fixtures.
      sample->value = designCase_ == 35 ? 79 : designCase_ == 33 ? 80 : 95;
      sample->temperature = {designCase_ == 35 ? 79. : designCase_ == 33 ? 80. : 90.,
                            designCase_ == 36 ? TemperatureStatus::Stale : TemperatureStatus::Ready,
                            monotonic()};
      sample->status = designCase_ == 36 ? Status::Stale : Status::Ready;
      sample->total = 1e12;
      sample->available = sample->secondary = sample->total * (100 - sample->value) / 100;
    }
    sample->extra = L"AMD Ryzen 9 9950X 16-Core Processor\nNVIDIA GeForce RTX 5070 Ti";
    if (w->kind == Kind::System)
      sample->value = 93840;
    double end = monotonic();
    sample->history = {};
    sample->history2 = {};
    for (int i = 0; i < 60; ++i) {
      double value = 42 + 19 * std::sin(i * .7);
      sample->history.add(
          {end - 60 + i, end - 59 + i, w->kind == Kind::Network ? value * 25000 : value, i != 29});
      sample->history2.add({end - 60 + i, end - 59 + i, value * 4000, i != 29});
    }
    if (w->kind == Kind::Weather) {
      auto weather = std::make_shared<WeatherData>(sample->weather ? *sample->weather : WeatherData{});
      weather->temperature = designCase_ == 3 ? -12 : 21;
      weather->low = designCase_ == 3 ? -16 : 16;
      weather->high = designCase_ == 3 ? -8 : 24;
      weather->sky = designCase_ == 1 ? 4 : designCase_ == 6 ? 0 : 1;
      weather->rain = designCase_ == 2 || designCase_ == 5 ? 1 : designCase_ == 3 ? 3 : 0;
      weather->night = designCase_ == 4 || designCase_ == 5;
      weather->observed = weather->fetched = nowUnix() - (designCase_ == 7 ? 8000 : 0);
      weather->condition = weather->rain == 3  ? L"눈"
                           : weather->rain     ? L"비"
                           : weather->sky == 4 ? L"흐림"
                           : weather->sky == 0 ? L"하늘 상태 정보 없음"
                                               : L"맑음";
      weather->state = kmaWeatherState(weather->rain, weather->sky);
      sample->weather = weather;
      if (designCase_ == 8) {
        sample->weather.reset();
        sample->message = L"인증키를 확인하세요";
        sample->status = Status::Error;
      }
    } else if (designCase_ == 9) {
      sample->status = Status::Loading;
      sample->message = L"불러오는 중";
      sample->calendar.reset();
    } else if (designCase_ == 10 && w->kind == Kind::Calendar) {
      using namespace std::chrono;
      auto calendar = std::make_shared<CalendarData>();
      calendar->connected = calendar->complete = true;
      for (int i = 0; i < 12; ++i) {
        Event event;
        event.id = L"overflow-" + std::to_wstring(i);
        event.title = L"긴 일정 제목 · 디자인 검토 및 다음 단계 세부 사항 확인";
        event.allDay = true;
        event.first = localDate(nowUnix(), w->timezone);
        event.last = year_month_day{sys_days{event.first} + days{i % 3 + 1}};
        calendar->events.push_back(event);
      }
      sample->calendar = calendar;
    } else if (designCase_ == 10) {
      sample->status = Status::Error;
      sample->message = L"장치를 찾을 수 없음 · 설정에서 다시 선택하세요";
    }
    if (w->kind == Kind::Disk && designCase_ >= 20 && designCase_ <= 23) {
      sample->disks.clear();
      int count = designCase_ == 20 ? 8 : designCase_ == 21 ? 1 : designCase_ == 22 ? 0 : 6;
      for (int i = 0; i < count; ++i)
        sample->disks.push_back({std::to_wstring(i), std::wstring(1, wchar_t(L'C' + i)) + L": 작업 자료 볼륨",
                                 4e12, 2e12, 1.8e12});
      if (count > 1)
        sample->disks[1].total = NAN;
      if (!count) {
        sample->status = Status::Unavailable;
        sample->message = L"표시할 로컬 볼륨 없음";
      }
    }
    data = sample;
    if (w->kind == Kind::Disk)
      window.displayed = sample;
  }
#endif
  if (renderer_.draw(window.hwnd, window.surface, *w, data.get(), settings_.theme, settings_.edit,
                     window.dpi)) {
    if (settingsBuilt_ && tab_ == 0 && selected_ == window.id && IsWindowVisible(main_))
      InvalidateRect(control(1212), nullptr, FALSE);
    window.renderedAt = monotonic();
    window.contentTime = nowUnix();
    ++redraws_;
    ++window.redraws;
    if (window.accessible)
      window.accessible->update(widgetSummary(*w, data.get()));
  }
}
void App::armTimer() {
  KillTimer(main_, 1);
  if (settings_.hidden || suspended_ || sessionLocked_)
    return;
  double wait = 86400, t = monotonic();
  auto unixTime = nowUnix();
  for (auto &window : windows_) {
    auto *w = find(window->id);
    if (!w || !window->placed)
      continue;
    if (w->kind == Kind::Clock) {
      int period = w->seconds ? 1 : 60;
      wait = std::min(wait, double(period - unixTime % period));
    } else if (w->kind == Kind::Calendar)
      wait = std::min(wait, double(60 - unixTime % 60));
    else if (providers_) {
      auto s = providers_->snapshot(*w);
      if (w->kind == Kind::Disk && w->size == Size::L && window->displayed &&
          window->displayed->disks.size() > 6)
        wait = std::min(wait, double(12 - unixTime % 12));
      if (s->sampledAt > (window->displayed ? window->displayed->sampledAt : 0))
        wait = std::min(wait, std::max(.02, window->renderedAt + w->interval - t));
    }
  }
  if (wait < 86400)
    SetTimer(main_, 1, UINT(std::clamp(wait * 1000., 20., 86400000.)), nullptr);
}
void App::tick() {
  if (settings_.hidden || suspended_ || sessionLocked_)
    return;
  double now = monotonic();
  for (auto &window : windows_) {
    auto *w = find(window->id);
    if (!w || !window->placed)
      continue;
    if (w->kind == Kind::Calendar && window->contentTime &&
        localDate(window->contentTime, w->timezone) != localDate(nowUnix(), w->timezone))
      providers_->invalidate(Kind::Calendar);
    int period = w->kind == Kind::Clock && w->seconds ? 1 : 60;
    bool repaint = (w->kind == Kind::Clock || w->kind == Kind::Calendar) &&
                   window->contentTime / period != nowUnix() / period;
    if (w->kind == Kind::Disk && w->size == Size::L && window->displayed &&
        window->displayed->disks.size() > 6 && window->contentTime / 12 != nowUnix() / 12)
      repaint = true;
    if (providers_ && w->kind != Kind::Clock) {
      auto data = providers_->snapshot(*w);
      bool fresh = data->sampledAt > (window->displayed ? window->displayed->sampledAt : 0);
      bool initial = !window->displayed || window->displayed->sampledAt == 0;
      if (fresh && (initial || now - window->renderedAt >= w->interval)) {
        window->displayed = data;
        repaint = true;
      }
    }
    if (repaint)
      draw(*window);
  }
  armTimer();
}
void App::openSettings(const std::wstring &id) {
  if (!settingsBuilt_) {
    buildSettings();
    updateList();
  }
  if (!id.empty()) {
    if (selected_ != id && !leaveEditor())
      return;
    selected_ = id;
    selectTab(0);
    updateList();
  }
  ShowWindow(main_, SW_SHOW);
  SetForegroundWindow(main_);
  if (IsIconic(main_))
    ShowWindow(main_, SW_RESTORE);
}
void App::tray(bool add) {
  tray_ = {sizeof tray_};
  tray_.hWnd = main_;
  tray_.uID = 1;
  tray_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  tray_.uCallbackMessage = WM_TRAY;
  tray_.hIcon = icon_;
  wcscpy_s(tray_.szTip, L"Tempos · 바탕화면 위젯");
  trayAdded_ = Shell_NotifyIconW(add ? NIM_ADD : NIM_MODIFY, &tray_) != FALSE;
  if (add && !trayAdded_)
    trayAdded_ = Shell_NotifyIconW(NIM_MODIFY, &tray_) != FALSE;
  tray_.uVersion = NOTIFYICON_VERSION_4;
  if (trayAdded_)
    Shell_NotifyIconW(NIM_SETVERSION, &tray_);
}
void App::menu(HWND owner, bool widget) {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, 2001, widget ? L"이 위젯 설정" : L"Tempos 설정");
  AppendMenuW(menu, MF_STRING | (settings_.edit ? MF_CHECKED : 0), 2002, L"정렬 모드");
  AppendMenuW(menu, MF_STRING, 2003, settings_.hidden ? L"모든 위젯 표시" : L"모든 위젯 숨기기");
  AppendMenuW(menu, MF_STRING, 2004, L"지금 새로고침");
  auto layouts = CreatePopupMenu();
  AppendMenuW(layouts, MF_STRING | MF_CHECKED, 3000, settings_.layoutName.c_str());
  for (size_t i = 0; i < settings_.otherLayouts.size(); ++i)
    AppendMenuW(layouts, MF_STRING, UINT_PTR(3001 + i), settings_.otherLayouts[i].layoutName.c_str());
  AppendMenuW(menu, MF_POPUP, UINT_PTR(layouts), L"배치");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, 2005, L"종료");
  POINT point{};
  GetCursorPos(&point);
  SetForegroundWindow(owner);
  int id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, owner, nullptr);
  DestroyMenu(menu);
  if (id)
    command(id, 0);
  PostMessageW(owner, WM_NULL, 0, 0);
}
void App::calendarMonth(Widget &current, int delta) {
  using namespace std::chrono;
  Widget w = current;
  if (delta == 0)
    w.source.clear();
  else {
    auto date = localDate(nowUnix(), w.timezone);
    int y = int(date.year()), m = int(unsigned(date.month()));
    if (!w.source.empty())
      swscanf_s(w.source.c_str(), L"%d-%d", &y, &m);
    year_month ym = year{y} / month{unsigned(m)};
    ym += months{delta};
    if (int(ym.year()) < 1900 || int(ym.year()) > 2200)
      return;
    w.source = std::to_wstring(int(ym.year())) + L"-" + std::to_wstring(unsigned(ym.month()));
  }
  auto next = settings_;
  next.widgets[size_t(&current - settings_.widgets.data())] = w;
  if (!commit(std::move(next)))
    return;
  configure();
  providers_->refresh();
  for (auto &window : windows_)
    if (window->id == w.id) {
      window->displayed.reset();
      draw(*window);
    }
}
void App::command(int id, int notification) {
  if (loading_)
    return;
  if (id >= 3000) {
    if (id > 3000 && leaveEditor())
      layoutCommand(1807, size_t(id - 3000));
    return;
  }
  if (id >= 1000 && id <= 1004 && notification == BN_CLICKED) {
    selectTab(id - 1000);
    return;
  }
  if (id == 1012 && notification == CBN_SELCHANGE) {
    selectTab(int(SendMessageW(control(id), CB_GETITEMDATA, choice(id), 0)));
    return;
  }
  if (id == 1802 && notification == LBN_SELCHANGE) {
    int index = int(SendMessageW(control(1802), LB_GETCURSEL, 0, 0));
    if (index >= 0 && size_t(index) <= settings_.otherLayouts.size()) {
      auto &layout = index ? settings_.otherLayouts[index - 1] : static_cast<const Layout &>(settings_);
      SetWindowTextW(control(1804), layout.layoutName.c_str());
      EnableWindow(control(1807), index > 0);
      EnableWindow(control(1808), index > 0);
    }
    return;
  }
  if (id >= 1805 && id <= 1808 && notification == BN_CLICKED) {
    layoutCommand(id, size_t(SendMessageW(control(1802), LB_GETCURSEL, 0, 0)));
    return;
  }
  if (notification == BN_SETFOCUS || notification == EN_SETFOCUS ||
      (notification == CBN_SETFOCUS &&
       (id == 1201 || id >= 1300 && id <= 1304 || id == 1313 || id == 1314 || id == 1403 || id == 1500)))
    focusField(control(id));
  if (id == 1325 && notification == BN_CLICKED) {
    loadControls();
    status(L"마지막으로 적용한 설정으로 되돌렸습니다.");
    return;
  }
  bool combo = id >= 1300 && id <= 1304 || id == 1313 || id == 1314;
  bool edit = id == 1305 || id == 1306 || id == 1315 || id == 1324;
  bool check = id >= 1307 && id <= 1312 || id >= 1600 && id <= 1604 || id >= 1610 && id <= 1614;
  if (combo || edit || check) {
    if ((combo && notification == CBN_SELCHANGE) || (edit && notification == EN_CHANGE) ||
        (check && notification == BN_CLICKED)) {
      if (id == 1310)
        SendMessageW(control(1303), CB_SETCURSEL, checked(1310) ? 0 : 1, 0);
      if (id == 1303 && find(selected_) && find(selected_)->kind == Kind::Clock)
        SendMessageW(control(1310), BM_SETCHECK, choice(1303) == 0 ? BST_CHECKED : BST_UNCHECKED, 0);
      editorChanged();
    }
    return;
  }
  if ((id == 1202 || id == 1203 || id == 1206 || id == 1507 || id == 2005 ||
       (id >= 1317 && id <= 1319)) &&
      !leaveEditor())
    return;
  if (id == 1206 && notification == BN_CLICKED) {
    duplicateWidget();
    return;
  }
  if (id >= 1702 && id <= 1713) {
    if (id == 1702 && updates_ && !installRequested_) {
      updateInstallError_.clear(); updates_->check();
    } else if (id == 1703 && updates_ && !installRequested_ && installedUpdateTarget(runningExecutable())) {
      updateInstallError_.clear();
      auto state = updates_->state();
      if (state.phase == UpdatePhase::Ready) {
        if (leaveEditor() && beginUpdateInstall(state, store_.root(), updateInstallError_)) {
          installRequested_ = true; PostQuitMessage(0);
        }
      } else updates_->download();
    } else if (id == 1704 && updates_) {
      installRequested_ = false; updates_->cancel();
    } else if (id == 1713) {
      auto next = settings_; next.checkUpdatesOnStart = checked(1713); commit(std::move(next));
    } else openAboutLink(id);
    updateControls(); return;
  }
#ifdef TEMPOS_TEST_HOST
  std::ofstream trace(store_.root() / L"commands.log", std::ios::app);
  trace << id << " " << notification << "\n";
  trace.close();
  if (fixture_ && id == 1914) {
    startupFailure_ = !startupFailure_;
    return;
  }
  if (id == 1913) {
    actions_.post([this](std::stop_token) {
      weather_.invalidate();
      providers_->invalidate(Kind::Weather);
      PostMessageW(main_, WM_ACTION, 0, 0);
    });
    return;
  }
  if (fixture_ && ((id >= 1900 && id <= 1912) || (id >= 1920 && id <= 1923) ||
                  (id >= 1930 && id <= 1936))) {
    designCase_ = id == 1912 ? -1 : id - 1900;
    for (auto &window : windows_)
      draw(*window);
    armTimer();
    return;
  }
#endif
  if (id == 1200 && notification == LBN_SELCHANGE) {
    int i = int(SendMessageW(control(1200), LB_GETCURSEL, 0, 0));
    if (i >= 0 && i < int(settings_.widgets.size())) {
      auto target = settings_.widgets[i].id;
      if (target == selected_)
        return;
      if (!leaveEditor()) {
        updateList();
        return;
      }
      selected_ = target;
      // Applying the previous widget's draft repopulates the list during this notification.
      SendMessageW(control(1200), LB_SETCURSEL, i, 0);
      loadControls();
    }
    return;
  }
  if (id == 1202) {
    int kind = choice(1201);
    if (kind >= 0 && kind < int(KindCount))
      add(Kind(kind));
  } else if (id == 1203)
    removeSelected();
  else if (id == 1316)
    applyControls();
  else if (id == 1320)
    findLocation();
  else if (id == 1204 || id == 2002) {
    settings_.edit = !settings_.edit;
    rebuild();
    status(settings_.edit ? L"정렬 모드 · 위젯을 드래그해서 이동하세요." : L"정렬 모드를 종료했어요.");
  } else if (id == 1205 || id == 2003) {
    auto next = settings_;
    next.hidden = !next.hidden;
    if (!commit(std::move(next)))
      return;
    rebuild();
  } else if (id >= 1317 && id <= 1319) {
    auto *w = find(selected_);
    if (w && w->kind == Kind::Calendar)
      calendarMonth(*w, id == 1317 ? -1 : id == 1318 ? 0 : 1);
  } else if (id == 1404) {
    auto next = settings_;
    int selected = choice(1403);
    if (selected < 0 || selected > 1)
      return;
    next.weatherProvider = WeatherProvider(selected);
    if (commit(std::move(next))) {
      rebuild();
      status(L"날씨 서비스를 모든 날씨 위젯에 적용했어요.");
    }
    weatherControls();
    updateViewport();
  } else if (id == 1401) {
    auto key = utf8(input(1400));
    if (key.empty() || key.size() > 1024) {
      status(L"기상청 API허브 인증키를 입력하세요.");
      return;
    }
    if (store_.saveSecret(L"weather-key", key)) {
      SetWindowTextW(control(1400), L"");
      actions_.post([this](std::stop_token) {
        weather_.invalidate();
        providers_->refresh();
        PostMessageW(main_, WM_ACTION, 0, 0);
      });
      status(L"날씨 인증키를 저장했어요.");
    } else
      status(L"인증키 저장 실패");
    SecureZeroMemory(key.data(), key.size());
  } else if (id == 1402) {
#ifdef TEMPOS_TEST_HOST
    status(L"목업 검사에서는 외부 안내 페이지를 열지 않아요.");
#else
    ShellExecuteW(main_, L"open",
                  settings_.weatherProvider == WeatherProvider::Kma ? L"https://apihub.kma.go.kr/"
                                                                    : L"https://open-meteo.com/en/terms",
                  nullptr, nullptr, SW_SHOWNORMAL);
#endif
  } else if (id == 1412) {
    auto client = utf8(input(1410));
    if (client.empty() || client.find(".apps.googleusercontent.com") == std::string::npos) {
      status(L"데스크톱 앱 OAuth 클라이언트 ID를 입력하세요.");
      return;
    }
    store_.saveSecret(L"calendar-client", client);
    auto secret = utf8(input(1411));
    store_.saveSecret(L"calendar-client-secret", secret);
    SetWindowTextW(control(1411), L"");
    EnableWindow(control(1412), FALSE);
    status(L"브라우저에서 조회 권한을 승인하세요. 최대 3분 동안 기다려요.");
    actions_.post([this](std::stop_token token) {
      calendar_.connect(token);
      PostMessageW(main_, WM_ACTION, 1, 0);
    });
  } else if (id == 1413) {
    calendar_.disconnect();
    providers_->invalidate(Kind::Calendar);
    rebuild();
    selectTab(1);
    status(L"일정 연결과 저장된 일정 자료를 삭제했어요.");
  } else if (id == 1415) {
    auto account = calendar_.account();
    std::vector<std::wstring> ids;
    for (size_t i = 0; i < account.calendars.size(); ++i)
      if (SendMessageW(control(1414), LB_GETSEL, i, 0) > 0)
        ids.push_back(account.calendars[i].id);
    if (ids.size() > 32) {
      status(L"동시에 표시할 캘린더는 최대 32개까지 선택하세요.");
      return;
    }
    calendar_.select(ids);
    providers_->invalidate(Kind::Calendar);
    rebuild();
    status(L"표시할 캘린더를 저장했어요.");
  } else if (id == 1501) {
    int theme = choice(1500);
    if (theme >= 0 && theme < 9) {
      auto next = settings_;
      next.theme = theme;
      if (!commit(std::move(next)))
        return;
      rebuild();
      status(L"전체 테마를 적용했어요.");
    }
  } else if (id == 1502) {
    wchar_t exe[32768]{};
    DWORD size = GetModuleFileNameW(nullptr, exe, DWORD(std::size(exe)));
    auto registration = startup_;
#ifdef TEMPOS_TEST_HOST
    if (startupFailure_)
      registration = StartupRegistration(L"", nullptr);
#endif
    LONG result = size && size < std::size(exe)
                      ? registration.set(checked(1502), startupCommand(exe, startupRoot_))
                      : ERROR_BAD_PATHNAME;
    SendMessageW(control(1502), BM_SETCHECK, startup_.enabled() ? BST_CHECKED : BST_UNCHECKED, 0);
    status(result == ERROR_SUCCESS ? L"시작 설정을 저장했어요."
                                   : L"시작 설정 저장 실패 · 기존 상태를 유지했어요.");
  } else if (id == 1503 || id == 1504) {
    wchar_t path[MAX_PATH] = L"tempos-layout.json";
    OPENFILENAMEW ofn{sizeof ofn};
    ofn.hwndOwner = main_;
    std::wstring title = (id == 1503 ? L"내보내기 · " : L"가져오기 · 교체 대상: ") + settings_.layoutName;
    if (id == 1504)
      title += L" (파일의 날씨 서비스는 모든 배치에 적용)";
    ofn.lpstrTitle = title.c_str();
    ofn.lpstrFilter = L"JSON 레이아웃\0*.json\0\0";
    ofn.lpstrFile = path;
#ifdef TEMPOS_TEST_HOST
    ofn.lpstrInitialDir = store_.root().c_str();
#endif
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"json";
    ofn.Flags = OFN_NOCHANGEDIR | (id == 1503 ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (id == 1503 ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn)) {
      try {
        if (id == 1503) {
          if (!atomicWrite(path, settingsJson(settings_, true).dump(2)))
            throw std::runtime_error("write");
          status(L"레이아웃을 내보냈어요.");
        } else {
          auto content = boundedRead(path);
          if (!content)
            throw std::runtime_error("read");
          auto imported = readSettings(Json::parse(*content));
          auto next = settings_;
          next.widgets = std::move(imported.widgets);
          next.theme = imported.theme;
          next.weatherProvider = imported.weatherProvider;
          next.hidden = false;
          next.edit = false;
          if (!commit(std::move(next)))
            return;
          rebuild();
          status(L"레이아웃을 가져왔어요.");
        }
      } catch (...) {
        status(L"파일을 처리할 수 없어요. 형식과 권한을 확인하세요.");
      }
    }
  } else if (id == 1510) {
    if (checked(1510)) {
#ifndef TEMPOS_TEST_HOST
      if (!sensorServiceRunning()) { startSensorSetup(false); return; }
#endif
    }
    auto next = settings_;
    next.cpuTemperatureEnabled = checked(1510);
    if (commit(std::move(next))) {
      configure();
      providers_->invalidate(Kind::Cpu);
    }
    sensorControls();
  } else if (id == 1512 || id == 1513)
    startSensorSetup(id == 1513);
  else if (id == 1505)
    diagnose();
  else if (id == 1506)
    ShellExecuteW(main_, L"open", store_.root().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  else if (id == 1507 || id == 2005) {
    closing_ = true;
    save();
    PostQuitMessage(0);
  } else if (id == 2001)
    openSettings();
  else if (id == 2004) {
    providers_->refresh();
    tick();
  }
}
LRESULT App::message(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
  if (msg == taskbarCreated_ && taskbarCreated_) {
    trayAdded_ = false;
    tray(true);
    rebuild();
    return 0;
  }
  switch (msg) {
  case WM_SETTINGCHANGE:
  case WM_THEMECHANGED:
    settingsTheme();
    rebuild();
    return 0;
  case WM_PAINT:
    if (settingsBuilt_) {
      PAINTSTRUCT ps{};
      auto dc = BeginPaint(hwnd, &ps);
      paintSettings(dc);
      EndPaint(hwnd, &ps);
      return 0;
    }
    break;
  case WM_ERASEBKGND:
    if (settingsBuilt_)
      return 1;
    break;
  case WM_PRINTCLIENT:
    if (settingsBuilt_) {
      paintSettings(reinterpret_cast<HDC>(w));
      return 0;
    }
    break;
  case WM_DRAWITEM:
    if (settingsBuilt_ && l) {
      drawSettingItem(*reinterpret_cast<DRAWITEMSTRUCT *>(l));
      return TRUE;
    }
    break;
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORLISTBOX:
  case WM_CTLCOLORBTN:
  case WM_CTLCOLORSTATIC: {
    if (!settingsBuilt_)
      break;
    int id = GetDlgCtrlID(reinterpret_cast<HWND>(l));
    bool input = msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX;
    bool canvas = id == 1011 || id == 1999;
    SetTextColor(reinterpret_cast<HDC>(w), textColor_);
    SetBkColor(reinterpret_cast<HDC>(w), input ? inputColor_ : canvas ? canvasColor_ : paneColor_);
    return reinterpret_cast<LRESULT>(input ? inputBrush_ : canvas ? canvasBrush_ : paneBrush_);
  }
  case WM_COMMAND:
    command(LOWORD(w), HIWORD(w));
    return 0;
#ifdef TEMPOS_TEST_HOST
  case WM_COPYDATA: {
    // Batch native input on this UI thread so real queued pointer messages cannot
    // interleave with a synthetic drag. Actual SendInput E2E remains separate.
    struct DragInput { HWND target; int fromX, fromY, toX, toY, move; };
    auto copy = reinterpret_cast<const COPYDATASTRUCT *>(l);
    if (!copy || copy->dwData != 0x544d5044 || copy->cbData != sizeof(DragInput) || !copy->lpData)
      return 0;
    auto input = *static_cast<const DragInput *>(copy->lpData);
    if (std::none_of(windows_.begin(), windows_.end(), [&](const auto &window) { return window->hwnd == input.target; }))
      return 0;
    for (int value : {input.fromX, input.fromY, input.toX, input.toY})
      if (value < -32768 || value > 32767) return 0;
    SendMessageW(input.target, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(input.fromX, input.fromY));
    LARGE_INTEGER frequency{}, start{}, moved{}, dropped{};
    QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start);
    if (input.move) SendMessageW(input.target, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(input.toX, input.toY));
    QueryPerformanceCounter(&moved);
    SendMessageW(input.target, WM_LBUTTONUP, 0, MAKELPARAM(input.fromX, input.fromY));
    QueryPerformanceCounter(&dropped);
    auto micros = [&](LONGLONG ticks) { return uint64_t(ticks * 1000000 / frequency.QuadPart); };
    return LRESULT((micros(dropped.QuadPart - moved.QuadPart) << 32) | (micros(moved.QuadPart - start.QuadPart) + 1));
  }
#endif
  case WM_TIMER:
    if (w == 1)
      tick();
    else if (w == 2 && sensorSetup_) {
      DWORD code = STILL_ACTIVE;
      if (GetExitCodeProcess(sensorSetup_, &code) && code != STILL_ACTIVE) {
        KillTimer(main_, 2);
        CloseHandle(sensorSetup_);
        sensorSetup_ = nullptr;
        completeSensorSetup(code);
      }
    }
    return 0;
  case WM_TEMPOS_DATA:
    tick();
    if (tab_ == 2) sensorControls();
    return 0;
  case WM_ACTION:
    EnableWindow(control(1412), TRUE);
    if (w == 1) {
      providers_->invalidate(Kind::Calendar);
      selectTab(tab_);
      status(calendar_.account().status);
    }
    return 0;
  case WM_LOCATION:
    if (auto result = location_.take())
      applyLocation(std::move(*result));
    return 0;
  case WM_UPDATE_HEALTH:
    if (!acknowledgeUpdate(store_.root(), store_.warning.empty() && windows_.size() == settings_.widgets.size() &&
        std::all_of(windows_.begin(), windows_.end(), [](const auto &w) { return IsWindow(w->hwnd); })))
      PostQuitMessage(1);
    return 0;
  case WM_UPDATE: {
    if (!updates_ || closing_) return 0;
    auto state = updates_->state();
    if (state.phase == UpdatePhase::Error || state.phase == UpdatePhase::Available) installRequested_ = false;
    updateControls();

#ifndef TEMPOS_TEST_HOST
    if (state.phase == UpdatePhase::Available && !updateNotified_ && trayAdded_) {
      updateNotified_ = true;
      auto info = tray_; info.uFlags = NIF_INFO; info.dwInfoFlags = NIIF_INFO;
      wcscpy_s(info.szInfoTitle, L"Tempos 새 버전");
      wcscpy_s(info.szInfo, L"설정의 정보 및 업데이트에서 확인하고 설치할 수 있습니다.");
      Shell_NotifyIconW(NIM_MODIFY, &info);
    }
#endif
    return 0;
  }
  case WM_TRAY:
    switch (LOWORD(l)) {
    case NIN_SELECT:
    case NIN_KEYSELECT:
    case WM_LBUTTONDBLCLK:
      openSettings();
      break;
    case WM_CONTEXTMENU:
      menu(main_);
      break;
    case NIN_BALLOONUSERCLICK:
      openSettings();
      selectTab(4);
      break;
    }
    return 0;
  case WM_DISPLAYCHANGE:
    rebuild();
    return 0;
  case WM_TIMECHANGE:
    providers_->refresh();
    tick();
    return 0;
  case WM_WTSSESSION_CHANGE:
    sessionLocked_ = w == WTS_SESSION_LOCK ? true : w == WTS_SESSION_UNLOCK ? false : sessionLocked_;
    configure();
    if (!sessionLocked_)
      tick();
    return 0;
  case WM_POWERBROADCAST:
    if (w == PBT_APMSUSPEND)
      suspended_ = true;
    else if (w == PBT_APMRESUMEAUTOMATIC || w == PBT_APMRESUMESUSPEND)
      suspended_ = false;
    configure();
    armTimer();
    return TRUE;
  case WM_DPICHANGED: {
    UINT nextDpi = HIWORD(w);
    if (settingsBuilt_ && settingsDpi_ != nextDpi) {
      auto oldFont = font_;
      font_ =
          CreateFontW(-MulDiv(14, nextDpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
      settingsDpi_ = nextDpi;
      settingsTheme();
      DeleteObject(oldFont);
    }
    auto *r = reinterpret_cast<RECT *>(l);
    MONITORINFO screen{sizeof screen};
    GetMonitorInfoW(MonitorFromRect(r, MONITOR_DEFAULTTONEAREST), &screen);
    int width = std::min(r->right - r->left, screen.rcWork.right - screen.rcWork.left),
        height = std::min(r->bottom - r->top, screen.rcWork.bottom - screen.rcWork.top);
    SetWindowPos(hwnd, nullptr, std::clamp(r->left, screen.rcWork.left, screen.rcWork.right - width),
                 std::clamp(r->top, screen.rcWork.top, screen.rcWork.bottom - height), width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    return 0;
  }
  case WM_SIZE:
    updateViewport();
    return 0;
  case WM_VSCROLL:
  case WM_HSCROLL: {
    int bar = msg == WM_VSCROLL ? SB_VERT : SB_HORZ;
    SCROLLINFO info{sizeof info, SIF_ALL};
    GetScrollInfo(hwnd, bar, &info);
    int position = info.nPos;
    switch (LOWORD(w)) {
    case SB_LINEUP:
      position -= 32;
      break;
    case SB_LINEDOWN:
      position += 32;
      break;
    case SB_PAGEUP:
      position -= int(info.nPage);
      break;
    case SB_PAGEDOWN:
      position += int(info.nPage);
      break;
    case SB_THUMBTRACK:
      position = info.nTrackPos;
      break;
    case SB_TOP:
      position = 0;
      break;
    case SB_BOTTOM:
      position = info.nMax;
      break;
    }
    scrollTo(position);
    return 0;
  }
  case WM_MOUSEWHEEL:
    scrollTo(GetScrollPos(hwnd, SB_VERT) - GET_WHEEL_DELTA_WPARAM(w) * 42 / 120);
    return 0;
  case WM_CLOSE:
    if (!leaveEditor())
      return 0;
    settings_.edit = false;
    rebuild();
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  case WM_QUERYENDSESSION:
    save();
    return TRUE;
  case WM_ENDSESSION:
    if (w)
      PostQuitMessage(0);
    return 0;
  case WM_DESTROY:
    if (closing_)
      PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, w, l);
}
#ifdef TEMPOS_TEST_HOST
// Copy the already presented layered surface when an unrelated topmost window covers
// the desktop. The fixed path belongs to the isolated TestHost data directory.
static bool captureWidgetClient(HWND hwnd, const RenderSurface &surface, const std::filesystem::path &root) {
  if (!surface.raster)
    return false;
  BITMAPINFO info{};
  auto &header = info.bmiHeader;
  header.biSize = sizeof header;
  header.biWidth = LONG(surface.width);
  header.biHeight = -LONG(surface.height);
  header.biPlanes = 1;
  header.biBitCount = 32;
  header.biCompression = BI_RGB;
  void *pixels{};
  HDC dc = CreateCompatibleDC(nullptr);
  if (!dc)
    return false;
  HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
  bool saved = false;
  if (bitmap) {
    auto old = SelectObject(dc, bitmap);
    RECT bounds{0, 0, LONG(surface.width), LONG(surface.height)};
    SetDCBrushColor(dc, RGB(237, 244, 250));
    FillRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SendMessageW(hwnd, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
    GdiFlush();
    DWORD bytes = surface.width * surface.height * 4;
    BITMAPFILEHEADER file{0x4d42, DWORD(sizeof file + sizeof header + bytes), 0, 0, sizeof file + sizeof header};
    std::ofstream out(root / L"widget-surface.bmp", std::ios::binary);
    out.write(reinterpret_cast<const char *>(&file), sizeof file);
    out.write(reinterpret_cast<const char *>(&header), sizeof header);
    out.write(static_cast<const char *>(pixels), bytes);
    out.close();
    saved = bool(out);
    SelectObject(dc, old);
    DeleteObject(bitmap);
  }
  DeleteDC(dc);
  return saved;
}
#endif
LRESULT App::widgetMessage(WidgetWindow &window, UINT msg, WPARAM wp, LPARAM lp) {
  auto *w = find(window.id);
  HWND hwnd = window.hwnd;
  switch (msg) {
#ifdef TEMPOS_TEST_HOST
  case WM_APP + 70:
    return captureWidgetClient(hwnd, window.surface, store_.root());
#endif
  case WM_PRINTCLIENT:
    if (window.surface.raster) {
      const auto &s = window.surface;
      BLENDFUNCTION blend{AC_SRC_OVER, 0, s.raster->opacity, AC_SRC_ALPHA};
      AlphaBlend(reinterpret_cast<HDC>(wp), 0, 0, int(s.width), int(s.height), s.raster->dc, 0, 0,
                 int(s.width), int(s.height), blend);
      return 0;
    }
    break;
  case WM_GETOBJECT:
    if (static_cast<LONG>(lp) == UiaRootObjectId && window.accessible)
      return UiaReturnRawElementProvider(hwnd, wp, lp, window.accessible);
    break;
  case WM_PAINT: {
    PAINTSTRUCT paint{};
    BeginPaint(hwnd, &paint);
    if (!settings_.hidden && !suspended_)
      draw(window);
    EndPaint(hwnd, &paint);
    return 0;
  }
  case WM_ERASEBKGND:
    return 1;
  case WM_NCHITTEST:
    if (w && w->clickThrough && !settings_.edit)
      return HTTRANSPARENT;
    return HTCLIENT;
  case WM_MOUSEACTIVATE:
    return MA_NOACTIVATE;
  case WM_CONTEXTMENU:
  case WM_LBUTTONDBLCLK:
  case WM_RBUTTONDOWN:
  case WM_RBUTTONUP:
  case WM_RBUTTONDBLCLK:
  case WM_MBUTTONDOWN:
  case WM_MBUTTONUP:
  case WM_MBUTTONDBLCLK:
  case WM_KEYDOWN:
    return 0;
  case WM_LBUTTONDOWN:
    if (w && settings_.edit && !w->locked) {
      window.dragging = true;
      window.dragStart = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ClientToScreen(hwnd, &window.dragStart);
      GetWindowRect(hwnd, &window.original);
      SetCapture(hwnd);
      return 0;
    }
    return 0;
  case WM_MOUSEMOVE:
    if (window.dragging) {
      POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ClientToScreen(hwnd, &p);
      SetWindowPos(hwnd, nullptr, window.original.left + p.x - window.dragStart.x,
                   window.original.top + p.y - window.dragStart.y, 0, 0,
                   SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return 0;
  case WM_LBUTTONUP:
    if (window.dragging && w) {
      window.dragging = false;
      ReleaseCapture();
      POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ClientToScreen(hwnd, &p);
      HMONITOR monitor = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
      MONITORINFO info{sizeof info};
      GetMonitorInfoW(monitor, &info);
      auto mi = std::find_if(monitors_.begin(), monitors_.end(),
                             [&](auto &m) { return EqualRect(&m.work, &info.rcWork); });
      if (mi != monitors_.end()) {
        Widget candidate = *w;
        candidate.monitor = mi->id;
        RECT r{};
        GetWindowRect(hwnd, &r);
        candidate.x = snap((r.left - mi->work.left) * 96.f / mi->dpi);
        candidate.y = snap((r.top - mi->work.top) * 96.f / mi->dpi);
        if (place(candidate)) {
          bool monitorChanged = candidate.monitor != w->monitor;
          if (monitorChanged || candidate.x != w->x || candidate.y != w->y) {
            auto next = settings_;
            next.widgets[size_t(w - settings_.widgets.data())] = candidate;
            if (commit(std::move(next)) && monitorChanged && selected_ == window.id)
              SendMessageW(control(1304), CB_SETCURSEL, mi - monitors_.begin(), 0);
          }
        } else {
          status(L"위젯을 놓을 빈 공간이 없어요. 기존 위치를 유지합니다.");
        }
      }
      position(window);
      draw(window);
    }
    return 0;
  case WM_CAPTURECHANGED:
    if (window.dragging) {
      window.dragging = false;
      position(window);
    }
    return 0;
  case WM_DPICHANGED:
    if (window.dragging || window.positioning)
      return 0;
    position(window);
    draw(window);
    return 0;
  case WM_NCDESTROY:
    if (window.accessible) {
      window.accessible->disconnect();
      window.accessible->Release();
      window.accessible = nullptr;
    }
    window.surface = {};
    window.hwnd = nullptr;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}
void App::loadRegions() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  auto file = std::filesystem::path(path).parent_path() / L"data" / L"regions.json";
  auto content = boundedRead(file, 4 * 1024 * 1024);
  if (!content)
    content = boundedRead(L"data/regions.json", 4 * 1024 * 1024);
  try {
    if (content)
      for (auto &r : Json::parse(*content)) {
        regions_.push_back({wide(r.at("name").get<std::string>()), r.at("nx").get<int>(),
                            r.at("ny").get<int>(), r.at("lat").get<double>(), r.at("lon").get<double>()});
      }
  } catch (...) {
    regions_.clear();
  }
  if (regions_.empty())
    regions_.push_back({L"서울특별시 종로구", 60, 127, 37.573, 126.979});
}
void App::diagnose() {
  PROCESS_MEMORY_COUNTERS_EX memory{};
  memory.cb = sizeof memory;
  GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
                       sizeof memory);
  DWORD handles = 0;
  GetProcessHandleCount(GetCurrentProcess(), &handles);
  Json j = Json::object();
  if (updates_) {
    auto update = updates_->state();
    j["update"] = {{"phase", int(update.phase)}, {"currentVersion", TEMPOS_VERSION_STRING},
        {"message", utf8(update.message)}, {"busy", update.busy()}, {"received", update.received},
        {"installRequested", installRequested_}, {"installError", utf8(updateInstallError_)},
        {"installedTarget", installedUpdateTarget(runningExecutable())}};
    if (update.release) j["update"]["latestVersion"] = update.release->version;
  }
  j["utc"] = rfc3339(nowUnix());
  j["widgets"] = settings_.widgets.size();
  j["subscriptions"] = providers_ ? providers_->subscriptions() : 0;
  j["privateBytes"] = memory.PrivateUsage;
  j["workingSet"] = memory.WorkingSetSize;
  j["handles"] = handles;
  j["gdiObjects"] = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
  j["userObjects"] = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
  j["redraws"] = redraws_;
  j["previewDraws"] = previewDraws_;
  j["softwareRendering"] = renderer_.software();
  j["hidden"] = settings_.hidden;
  j["fixture"] = fixture_;
  j["weatherProvider"] = settings_.weatherProvider == WeatherProvider::Kma ? "kma" : "open-meteo";
#ifdef TEMPOS_TEST_HOST
  j["apiMode"] = "mock";
  j["testDataDirectory"] = utf8(std::filesystem::absolute(store_.root()).wstring());
  j["locationMode"] = "mock";
  j["startupRegistryKey"] = utf8(startup_.key());
#else
  j["apiMode"] = "live";
  j["locationMode"] = "live";
#endif
  j["trayRegistered"] = trayAdded_;
  j["monitors"] = Json::array();
  for (auto &m : monitors_)
    j["monitors"].push_back({{"id", utf8(m.id)},
                             {"dpi", m.dpi},
                             {"left", m.work.left},
                             {"top", m.work.top},
                             {"right", m.work.right},
                             {"bottom", m.work.bottom},
                             {"primary", m.primary}});
  j["windows"] = Json::array();
  for (auto &window : windows_) {
    auto *w = find(window->id);
    RECT r{};
    GetWindowRect(window->hwnd, &r);
    j["windows"].push_back({{"kind", w ? int(w->kind) : -1},
                            {"id", utf8(window->id)},
                            {"handle", uint64_t(reinterpret_cast<uintptr_t>(window->hwnd))},
                            {"extendedStyle", uint64_t(GetWindowLongPtrW(window->hwnd, GWL_EXSTYLE))},
                            {"redraws", window->redraws},
                            {"placed", window->placed},
                            {"visible", bool(IsWindowVisible(window->hwnd))},
                            {"parent", uint64_t(reinterpret_cast<uintptr_t>(GetParent(window->hwnd)))},
                            {"x", r.left},
                            {"y", r.top},
                            {"width", r.right - r.left},
                            {"height", r.bottom - r.top},
                            {"dpi", window->dpi},
                            {"windowDpi", GetDpiForWindow(window->hwnd)}});
    if (w && w->kind == Kind::Weather) {
      auto data = providers_->snapshot(*w);
      j["windows"].back()["weatherProvider"] =
          data->weatherProvider == WeatherProvider::Kma ? "kma" : "open-meteo";
      j["windows"].back()["weatherStatus"] = int(data->status);
      j["windows"].back()["weatherTemperature"] = data->weather ? Json(data->weather->temperature) : Json();
      j["windows"].back()["weatherState"] = data->weather ? int(data->weather->state) : 0;
      auto &days = j["windows"].back()["weatherDays"] = Json::array();
      if (data->weather)
        for (const auto &day : data->weather->days)
          days.push_back({{"time", day.time}, {"low", day.low}, {"high", day.high},
                          {"probability", day.probability}, {"state", int(day.state)}});
    }
    if (w && (w->kind == Kind::Cpu || w->kind == Kind::Gpu)) {
      auto data = providers_->snapshot(*w);
      j["windows"].back()["temperatureC"] = validTemperature(data->temperature, monotonic(), w->interval)
                                               ? Json(data->temperature.celsius) : Json();
      j["windows"].back()["temperatureStatus"] = int(data->temperature.status);
      j["windows"].back()["temperatureSampledAt"] = data->temperature.sampledAt;
    }
    if (w && w->kind == Kind::Calendar) {
      auto data = providers_->snapshot(*w);
      j["windows"].back()["calendarStatus"] = int(data->status);
      j["windows"].back()["calendarEventCount"] = data->calendar ? data->calendar->events.size() : 0;
      j["windows"].back()["calendarConnected"] = data->calendar && data->calendar->connected;
    }
  }
  if (atomicWrite(store_.root() / L"diagnostics.json", j.dump(2)))
    status(L"diagnostics.json을 데이터 폴더에 저장했어요.");
  else
    status(L"진단 저장 실패");
}
void App::details(const Widget &w, std::optional<std::chrono::year_month_day> date) {
  auto data = providers_->snapshot(w);
  std::wstring body, title = std::wstring(kindName(w.kind));
  if (w.kind == Kind::Calendar) {
    using namespace std::chrono;
    auto day = date.value_or(localDate(nowUnix(), w.timezone));
    body = std::to_wstring(int(day.year())) + L"년 " + std::to_wstring(unsigned(day.month())) + L"월 " +
           std::to_wstring(unsigned(day.day())) + L"일\r\n\r\n";
    int count = 0;
    if (data->calendar)
      for (auto &e : data->calendar->events)
        if (eventOnDate(e, day, w.timezone)) {
          if (e.allDay)
            body += L"종일";
          else {
            auto t = localTime(e.start, w.timezone);
            hh_mm_ss time{t - floor<days>(t)};
            wchar_t label[24]{};
            swprintf_s(label, L"%02d:%02d", int(time.hours().count()), int(time.minutes().count()));
            body += label;
          }
          body += L"  " + e.title + L"\r\n\r\n";
          ++count;
        }
    if (!count)
      body += L"일정 없음";
  } else if (w.kind == Kind::Weather) {
    body = w.region + L"\r\n\r\n";
    if (data->weather) {
      auto &d = *data->weather;
      auto temperature = [&](double value) { return number(w.fahrenheit ? value * 1.8 + 32 : value); };
      auto unit = w.fahrenheit ? L"°F" : L"°C";
      body += d.condition + L"  " + temperature(d.temperature) + unit + L"\r\n습도 " + number(d.humidity) +
              L"%\r\n바람 " + number(d.wind, 1) + L" m/s\r\n최저 / 최고 " + temperature(d.low) + L"° / " +
              temperature(d.high) + unit + L"\r\n";
      if (d.observed)
        body += (data->weatherProvider == WeatherProvider::Kma ? L"관측 시각 (UTC) " : L"자료 시각 (UTC) ") +
                wide(rfc3339(d.observed)) + L"\r\n";
      if (d.skyForecast)
        body += L"하늘 상태: 초단기예보 기준\r\n";
    }
    body += L"\r\n" + weatherSource(data->weatherProvider) + L"\r\n";
    body += data->weatherProvider == WeatherProvider::Kma
                ? L"https://apihub.kma.go.kr/\r\n"
                : L"모델 기반 현재값·예보 (직접 관측값이 아닙니다)\r\nhttps://open-meteo.com/\r\nCC BY 4.0 · "
                  L"단위·날씨 코드 변환 표시\r\n";
    body += data->message;
  }
  UINT dpi = GetDpiForWindow(main_);
  auto hwnd = CreateWindowExW(0, L"Tempos.Details", title.c_str(), WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(480, dpi, 96), MulDiv(520, dpi, 96), main_,
                              nullptr, instance_, body.data());
  if (hwnd)
    SetForegroundWindow(hwnd);
}
} // namespace tempos
