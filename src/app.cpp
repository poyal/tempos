#include "app.h"
#include <commctrl.h>
#include <windowsx.h>
#include <wtsapi32.h>
#include <psapi.h>
#include <commdlg.h>
#include <fstream>
namespace tempos {
constexpr UINT WM_TRAY = WM_APP + 2, WM_ACTION = WM_APP + 3;
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
    int step = MulDiv(16, GetDpiForWindow(hwnd), 96);
    for (int x = 0; x < r.right; x += step) {
      MoveToEx(dc, x, 0, nullptr);
      LineTo(dc, x, r.bottom);
    }
    for (int y = 0; y < r.bottom; y += step) {
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
static void scrollSettings(HWND hwnd, int bar, int position) {
  SCROLLINFO info{sizeof info, SIF_ALL};
  GetScrollInfo(hwnd, bar, &info);
  position = std::clamp(position, 0, std::max(0, info.nMax - int(info.nPage) + 1));
  int delta = info.nPos - position;
  SetScrollPos(hwnd, bar, position, TRUE);
  if (delta)
    ScrollWindowEx(hwnd, bar == SB_HORZ ? delta : 0, bar == SB_VERT ? delta : 0, nullptr, nullptr, nullptr,
                   nullptr, SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
}
void App::updateViewport() {
  if (!main_)
    return;
  RECT client{};
  GetClientRect(main_, &client);
  UINT dpi = GetDpiForWindow(main_);
  for (int bar : {SB_HORZ, SB_VERT}) {
    int old = GetScrollPos(main_, bar);
    SCROLLINFO info{sizeof info, SIF_RANGE | SIF_PAGE};
    info.nMax = MulDiv(bar == SB_HORZ ? 850 : 730, dpi, 96) - 1;
    info.nPage = UINT(bar == SB_HORZ ? client.right : client.bottom);
    SetScrollInfo(main_, bar, &info, TRUE);
    int current = GetScrollPos(main_, bar);
    if (old != current)
      ScrollWindowEx(main_, bar == SB_HORZ ? old - current : 0, bar == SB_VERT ? old - current : 0, nullptr,
                     nullptr, nullptr, nullptr, SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
  }
}
App::App(HINSTANCE instance, std::filesystem::path root, bool fixture, bool visible)
    : instance_(instance), store_(root), settings_(store_.load()), weather_(store_, http_, fixture),
      calendar_(store_, http_, fixture), fixture_(fixture), visible_(visible) {
  if (!renderer_.ready())
    throw std::runtime_error("renderer");
  monitors_ = monitors();
}
App::~App() {
  closing_ = true;
  http_.cancel();
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
  icon_ = LoadIconW(instance_, MAKEINTRESOURCEW(101));
  WNDCLASSEXW wc{sizeof wc};
  wc.hInstance = instance_;
  wc.hIcon = icon_;
  wc.hIconSm = icon_;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
  wc.lpfnWndProc = windowProc;
  wc.lpszClassName = L"Tempos.Management";
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
  main_ = CreateWindowExW(0, L"Tempos.Management", L"Tempos · 위젯 설정",
                          WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME |
                              WS_VSCROLL | WS_HSCROLL,
                          CW_USEDEFAULT, CW_USEDEFAULT, 880, 790, nullptr, nullptr, instance_, this);
  if (!main_)
    return 1;
  UINT dpi = GetDpiForWindow(main_);
  RECT size{0, 0, MulDiv(850, dpi, 96), MulDiv(730, dpi, 96)};
  AdjustWindowRectExForDpi(&size, DWORD(GetWindowLongPtrW(main_, GWL_STYLE)), FALSE, 0, dpi);
  MONITORINFO screen{sizeof screen};
  GetMonitorInfoW(MonitorFromWindow(main_, MONITOR_DEFAULTTONEAREST), &screen);
  SetWindowPos(
      main_, nullptr, 0, 0, std::min(size.right - size.left, screen.rcWork.right - screen.rcWork.left),
      std::min(size.bottom - size.top, screen.rcWork.bottom - screen.rcWork.top), SWP_NOMOVE | SWP_NOZORDER);
  font_ = CreateFontW(-MulDiv(14, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  providers_ = std::make_unique<ProviderService>(
      main_,
      [this](const Widget &w, std::stop_token stop) {
        return w.kind == Kind::Weather ? weather_.read(w, stop) : calendar_.read(w, stop);
      },
      fixture_);
  WTSRegisterSessionNotification(main_, NOTIFY_FOR_THIS_SESSION);
  tray(true);
  rebuild();
  if (visible_ || !store_.warning.empty() || !trayAdded_)
    openSettings();
  if (!store_.warning.empty())
    status(store_.warning);
  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
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
void App::buildSettings() {
  settingsBuilt_ = true;
  loadRegions();
  UINT dpi = GetDpiForWindow(main_);
  settingsDpi_ = dpi;
  auto px = [&](int x) { return MulDiv(x, dpi, 96); };
  auto create = [&](const wchar_t *cls, const std::wstring &text, DWORD style, int x, int y, int w, int h,
                    int id, int tab) {
    HWND ctrl = CreateWindowExW(cls == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text.c_str(),
                                WS_CHILD | WS_VISIBLE | style, px(x), px(y), px(w), px(h), main_,
                                reinterpret_cast<HMENU>(INT_PTR(id)), instance_, nullptr);
    SendMessageW(ctrl, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    controls_.push_back({ctrl, tab});
    return ctrl;
  };
  auto label = [&](const wchar_t *t, int x, int y, int width, int tab) {
    create(L"STATIC", t, 0, x, y, width, 23, 0, tab);
  };
  auto button = [&](const wchar_t *t, int x, int y, int width, int id, int tab) {
    return create(L"BUTTON", t, WS_TABSTOP | BS_PUSHBUTTON, x, y, width, 31, id, tab);
  };
  auto combo = [&](int x, int y, int width, int id, int tab) {
    return create(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, x, y, width, 220, id, tab);
  };
  auto edit = [&](int x, int y, int width, int id, int tab, bool password = false) {
    return create(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | (password ? ES_PASSWORD : 0), x, y, width, 27,
                  id, tab);
  };
  auto check = [&](const wchar_t *t, int x, int y, int width, int id, int tab) {
    return create(L"BUTTON", t, WS_TABSTOP | BS_AUTOCHECKBOX, x, y, width, 27, id, tab);
  };
  auto addItem = [](HWND hwnd, const wchar_t *t) {
    SendMessageW(hwnd, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t));
  };
  auto tabs = create(WC_TABCONTROLW, L"", WS_TABSTOP, 16, 12, 818, 34, 1000, -1);
  for (auto t : {L"위젯", L"연동", L"앱", L"표시 옵션"}) {
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<wchar_t *>(t);
    TabCtrl_InsertItem(tabs, TabCtrl_GetItemCount(tabs), &item);
  }
  label(L"바탕화면 위젯", 22, 63, 205, 0);
  create(L"LISTBOX", L"", WS_TABSTOP | WS_BORDER | WS_VSCROLL | LBS_NOTIFY, 22, 91, 207, 400, 1200, 0);
  auto kinds = combo(22, 507, 207, 1201, 0);
  for (int i = 0; i < int(KindCount); ++i)
    addItem(kinds, kindName(Kind(i)).data());
  SendMessageW(kinds, CB_SETCURSEL, 0, 0);
  button(L"추가", 22, 546, 98, 1202, 0);
  button(L"제거", 130, 546, 99, 1203, 0);
  button(L"정렬 모드", 22, 591, 207, 1204, 0);
  button(L"모두 숨기기 / 표시", 22, 632, 207, 1205, 0);
  label(L"크기 · 비율 고정", 253, 64, 230, 0);
  combo(253, 90, 258, 1300, 0);
  label(L"확대", 534, 64, 250, 0);
  auto scale = combo(534, 90, 270, 1301, 0);
  for (auto t : {L"100%", L"150%", L"200%"})
    addItem(scale, t);
  label(L"테마", 253, 132, 258, 0);
  auto theme = combo(253, 158, 258, 1302, 0);
  addItem(theme, L"전체 테마 따르기");
  for (auto &p : palettes())
    addItem(theme, p.name);
  label(L"새로고침", 534, 132, 270, 0);
  combo(534, 158, 270, 1303, 0);
  label(L"모니터", 253, 200, 551, 0);
  combo(253, 226, 551, 1304, 0);
  label(L"배경 투명도 (0–100%)", 253, 268, 258, 0);
  edit(253, 294, 258, 1305, 0);
  label(L"전체 투명도 (0–100%)", 534, 268, 270, 0);
  edit(534, 294, 270, 1306, 0);
  check(L"클릭 통과", 253, 336, 165, 1307, 0);
  check(L"위치 잠금", 430, 336, 165, 1308, 0);
  check(L"항상 위", 621, 336, 175, 1309, 0);
  check(L"초 표시 (시계)", 253, 373, 187, 1310, 0);
  check(L"그래프", 453, 373, 140, 1311, 0);
  check(L"날씨 배경 자동", 621, 373, 183, 1312, 0);
  label(L"장치 / 볼륨", 253, 418, 551, 0);
  combo(253, 444, 551, 1313, 0);
  label(L"날씨 지역", 253, 484, 551, 0);
  combo(253, 510, 551, 1314, 0);
  for (auto &r : regions_)
    SendMessageW(control(1314), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(r.name.c_str()));
  label(L"시간대 (비우면 Windows 설정 · 예: Asia/Seoul)", 253, 550, 551, 0);
  edit(253, 576, 551, 1315, 0);
  button(L"적용", 662, 628, 142, 1316, 0);
  button(L"이전 달", 253, 628, 113, 1317, 0);
  button(L"오늘", 375, 628, 100, 1318, 0);
  button(L"다음 달", 484, 628, 113, 1319, 0);
  label(L"기상청 API허브 인증키", 27, 67, 760, 1);
  edit(27, 100, 775, 1400, 1, true);
  button(L"날씨 인증키 저장", 27, 143, 224, 1401, 1);
  button(L"API허브 열기", 264, 143, 224, 1402, 1);
  label(L"개인 키를 PC에 암호화해 저장합니다. 공공데이터포털 ServiceKey와 다릅니다.", 27, 190, 777, 1);
  label(L"Google 캘린더 · 일정 조회 전용", 27, 245, 777, 1);
  label(L"데스크톱 앱 OAuth 클라이언트 ID", 27, 281, 777, 1);
  edit(27, 308, 775, 1410, 1);
  label(L"클라이언트 보안 비밀 (발급 정보에 있을 때 입력)", 27, 346, 777, 1);
  edit(27, 373, 775, 1411, 1, true);
  button(L"브라우저에서 연결", 27, 416, 224, 1412, 1);
  button(L"연결 해제", 264, 416, 224, 1413, 1);
  label(L"표시할 캘린더 (여러 개 선택 가능)", 27, 466, 777, 1);
  create(L"LISTBOX", L"", WS_TABSTOP | WS_BORDER | WS_VSCROLL | LBS_EXTENDEDSEL, 27, 495, 580, 151, 1414, 1);
  button(L"캘린더 선택 저장", 620, 495, 182, 1415, 1);
  create(L"STATIC", calendar_.account().status, 0, 27, 658, 777, 25, 1416, 1);
  label(L"전체 테마", 27, 71, 777, 2);
  auto global = combo(27, 103, 450, 1500, 2);
  for (auto &p : palettes())
    addItem(global, p.name);
  SendMessageW(global, CB_SETCURSEL, settings_.theme, 0);
  button(L"전체 테마 적용", 496, 102, 304, 1501, 2);
  check(L"Windows 로그인 시 시작", 27, 168, 550, 1502, 2);
  HKEY runKey{};
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ,
                    &runKey) == ERROR_SUCCESS) {
    if (RegQueryValueExW(runKey, L"Tempos", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
      SendMessageW(control(1502), BM_SETCHECK, BST_CHECKED, 0);
    RegCloseKey(runKey);
  }
  button(L"레이아웃 내보내기", 27, 225, 363, 1503, 2);
  button(L"레이아웃 가져오기", 408, 225, 392, 1504, 2);
  button(L"진단 정보 저장", 27, 279, 363, 1505, 2);
  button(L"데이터 폴더 열기", 408, 279, 392, 1506, 2);
  label(L"위젯은 트레이 아이콘에서 관리할 수 있습니다.", 27, 347, 777, 2);
  label(L"트레이 아이콘을 ^ 영역에 둘지는 Windows 작업표시줄 설정에서 지정합니다.", 27, 379, 777, 2);
  label(L"정렬 모드에서는 드래그 이동과 투명한 위젯 복구가 가능합니다.", 27, 429, 777, 2);
  label(L"일정과 인증정보는 레이아웃 내보내기에 포함되지 않습니다.", 27, 462, 777, 2);
  button(L"Tempos 종료", 27, 615, 773, 1507, 2);
  label(L"위젯 탭에서 선택한 위젯의 추가 표시 옵션입니다.", 27, 71, 777, 3);
  check(L"시계 · 오전 / 오후 12시간 표시", 27, 120, 777, 1600, 3);
  check(L"날씨 · 화씨 (°F)", 27, 162, 777, 1601, 3);
  check(L"네트워크 · 바이트 단위 (KiB/s, MiB/s)", 27, 204, 777, 1602, 3);
  check(L"디스크 · 이진 단위 (GiB, TiB)", 27, 246, 777, 1603, 3);
  check(L"캘린더 · 월요일부터 한 주 시작", 27, 288, 777, 1604, 3);
  label(L"시스템 정보 표시 항목", 27, 351, 777, 3);
  check(L"PC 이름", 27, 393, 350, 1610, 3);
  check(L"Windows 버전", 405, 393, 395, 1611, 3);
  check(L"CPU 이름", 27, 435, 350, 1612, 3);
  check(L"GPU 이름", 405, 435, 395, 1613, 3);
  check(L"가동 시간", 27, 477, 350, 1614, 3);
  button(L"표시 옵션 적용", 27, 615, 773, 1620, 3);
  create(L"STATIC", L"", 0, 22, 695, 802, 26, 1999, -1);
  selectTab(0);
  updateViewport();
}
void App::selectTab(int tab) {
  tab_ = tab;
  for (auto &[hwnd, page] : controls_)
    ShowWindow(hwnd, page < 0 || page == tab ? SW_SHOW : SW_HIDE);
  if (tab == 1) {
    SetWindowTextW(control(1416), calendar_.account().status.c_str());
    auto account = calendar_.account();
    SendMessageW(control(1414), LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < account.calendars.size(); ++i) {
      auto &c = account.calendars[i];
      SendMessageW(control(1414), LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(c.name.c_str()));
      SendMessageW(
          control(1414), LB_SETSEL,
          std::find(account.selected.begin(), account.selected.end(), c.id) != account.selected.end(), i);
    }
  }
  if (tab == 3) {
    auto *w = find(selected_);
    for (int id : {1600, 1601, 1602, 1603, 1604, 1610, 1611, 1612, 1613, 1614, 1620})
      EnableWindow(control(id), w != nullptr);
    if (w) {
      auto check = [&](int id, bool value) {
        SendMessageW(control(id), BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
      };
      check(1600, w->twelveHour);
      check(1601, w->fahrenheit);
      check(1602, w->networkBytes);
      check(1603, w->binaryDisk);
      check(1604, w->weekStart == 1);
      for (int i = 0; i < 5; ++i)
        check(1610 + i, w->systemFields[i]);
    }
  }
}
void App::updateList() {
  if (!settingsBuilt_)
    return;
  auto list = control(1200);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  for (size_t i = 0; i < settings_.widgets.size(); ++i) {
    auto &w = settings_.widgets[i];
    auto name = std::wstring(kindName(w.kind)) + L" · " + std::wstring(sizeName(w.size));
    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    if (w.id == selected_)
      SendMessageW(list, LB_SETCURSEL, i, 0);
  }
  if (!find(selected_) && !settings_.widgets.empty()) {
    selected_ = settings_.widgets.front().id;
    SendMessageW(list, LB_SETCURSEL, 0, 0);
  }
  loadControls();
}
void App::loadControls() {
  loading_ = true;
  auto *w = find(selected_);
  for (int id = 1300; id <= 1319; ++id)
    EnableWindow(control(id), w != nullptr);
  if (!w) {
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
    if (w->monitor == m.id || w->monitor.empty() && m.primary)
      set(1304, int(i));
  }
  SetWindowTextW(control(1305), std::to_wstring(w->backgroundTransparency).c_str());
  SetWindowTextW(control(1306), std::to_wstring(w->transparency).c_str());
  auto check = [&](int id, bool yes) {
    SendMessageW(control(id), BM_SETCHECK, yes ? BST_CHECKED : BST_UNCHECKED, 0);
  };
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
  item(1313, w->kind == Kind::Network ? L"장치를 선택하세요" : L"기본 장치");
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
  for (auto &other : settings_.widgets)
    if (other.id != w.id && (other.monitor == mi->id || other.monitor.empty() && mi->primary)) {
      auto s = extent(other.size, other.scale);
      occupied.push_back({other.x, other.y, s.width, s.height});
    }
  Box proposed{snap(w.x), snap(w.y), size.width, size.height};
  bool valid =
      proposed.x >= 16 && proposed.y >= 16 && proposed.x + proposed.w <= area.width - 16 &&
      proposed.y + proposed.h <= area.height - 16 &&
      !std::any_of(occupied.begin(), occupied.end(), [&](auto &box) { return overlaps(box, proposed); });
  if (!keep || !valid) {
    auto position = freePosition(area, size, occupied);
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
void App::applyControls() {
  auto *current = find(selected_);
  if (!current)
    return;
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
    w.backgroundTransparency = integer(1305);
    w.transparency = integer(1306);
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
    if (w.kind == Kind::Gpu || w.kind == Kind::Network || w.kind == Kind::Disk) {
      int i = choice(1313);
      w.source = i > 0 && i <= int(devices_.size()) ? devices_[i - 1].id : L"";
    }
    if (w.kind == Kind::Weather) {
      int i = choice(1314);
      if (i >= 0 && i < int(regions_.size())) {
        auto &r = regions_[i];
        w.region = r.name;
        w.nx = r.nx;
        w.ny = r.ny;
        w.latitude = r.lat;
        w.longitude = r.lon;
      }
    }
    w.timezone = input(1315);
    if (!w.timezone.empty())
      static_cast<void>(std::chrono::locate_zone(utf8(w.timezone)));
    if (!validateWidget(w))
      throw std::runtime_error("values");
    if (!place(w))
      throw std::runtime_error("space");
    *current = std::move(w);
    save();
    rebuild();
    status(L"설정을 적용했어요.");
  } catch (...) {
    status(L"입력값을 확인하세요. 투명도는 0–100%, 크기는 화면 안의 빈 공간에 맞아야 해요.");
  }
}
void App::save() {
  if (!store_.save(settings_))
    status(L"설정 저장 실패 · 데이터 폴더 권한을 확인하세요.");
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
  selected_ = w.id;
  settings_.widgets.push_back(w);
  save();
  rebuild();
  status(L"위젯을 추가했어요.");
}
void App::removeSelected() {
  settings_.widgets.erase(std::remove_if(settings_.widgets.begin(), settings_.widgets.end(),
                                         [&](auto &w) { return w.id == selected_; }),
                          settings_.widgets.end());
  selected_.clear();
  save();
  rebuild();
  status(L"위젯을 제거했어요.");
}
void App::rebuild() {
  monitors_ = monitors();
  for (auto hwnd : grids_)
    DestroyWindow(hwnd);
  grids_.clear();
  if (settings_.edit && !settings_.hidden)
    for (auto &m : monitors_) {
      auto hwnd =
          CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                          L"Tempos.Grid", L"", WS_POPUP, m.work.left, m.work.top, m.work.right - m.work.left,
                          m.work.bottom - m.work.top, nullptr, nullptr, instance_, nullptr);
      SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 95, LWA_COLORKEY | LWA_ALPHA);
      SetWindowPos(hwnd, HWND_TOP, m.work.left, m.work.top, m.work.right - m.work.left,
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
    if (!settings_.hidden && window.placed)
      draw(window);
  }
  configure();
  updateList();
  armTimer();
}
void App::position(WidgetWindow &window) {
  auto *w = find(window.id);
  if (!w || !window.hwnd || monitors_.empty())
    return;
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
  float factor = mi->dpi / 96.f;
  window.displayX = placement->box.x;
  window.displayY = placement->box.y;
  HWND parent = w->topmost || settings_.edit ? nullptr : desktopHost();
  if (parent && !AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(window.hwnd),
                                              GetWindowDpiAwarenessContext(parent))) {
    parent = nullptr;
  }
  if (GetParent(window.hwnd) != parent) {
    if (parent)
      SetWindowLongPtrW(window.hwnd, GWL_STYLE, WS_CHILD);
    SetParent(window.hwnd, parent);
    if (!parent)
      SetWindowLongPtrW(window.hwnd, GWL_STYLE, WS_POPUP);
  }
  LONG_PTR ex =
      WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | (renderer_.layered() ? WS_EX_LAYERED : WS_EX_NOREDIRECTIONBITMAP);
  if (w->clickThrough && !settings_.edit)
    ex |= WS_EX_TRANSPARENT;
  SetWindowLongPtrW(window.hwnd, GWL_EXSTYLE, ex);
  POINT origin{mi->work.left + LONG(window.displayX * factor), mi->work.top + LONG(window.displayY * factor)};
  if (parent)
    ScreenToClient(parent, &origin);
  SetWindowPos(window.hwnd,
               parent           ? HWND_TOP
               : w->topmost     ? HWND_TOPMOST
               : settings_.edit ? HWND_TOP
                                : HWND_BOTTOM,
               origin.x, origin.y, LONG(extentDip.width * factor), LONG(extentDip.height * factor),
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
  providers_->configure(active, settings_.hidden || suspended_ || sessionLocked_, saving);
}
void App::draw(WidgetWindow &window) {
  auto *w = find(window.id);
  if (!w || !window.hwnd || !window.placed)
    return;
  if (!window.displayed && providers_)
    window.displayed = providers_->snapshot(*w);
  auto data = window.displayed;
  if (renderer_.draw(window.hwnd, window.surface, *w, data.get(), settings_.theme, settings_.edit,
                     window.dpi)) {
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
    selected_ = id;
    selectTab(0);
    TabCtrl_SetCurSel(control(1000), 0);
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
void App::calendarMonth(Widget &w, int delta) {
  using namespace std::chrono;
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
  configure();
  providers_->refresh();
  for (auto &window : windows_)
    if (window->id == w.id) {
      window->displayed.reset();
      draw(*window);
    }
  save();
}
void App::command(int id, int notification) {
#ifdef TEMPOS_TEST_HOST
  std::ofstream trace(store_.root() / L"commands.log", std::ios::app);
  trace << id << " " << notification << "\n";
  trace.close();
#endif
  if (id == 1200 && notification == LBN_SELCHANGE) {
    int i = int(SendMessageW(control(1200), LB_GETCURSEL, 0, 0));
    if (i >= 0 && i < int(settings_.widgets.size())) {
      selected_ = settings_.widgets[i].id;
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
  else if (id == 1204 || id == 2002) {
    settings_.edit = !settings_.edit;
    rebuild();
    status(settings_.edit ? L"정렬 모드 · 위젯을 드래그해서 이동하세요." : L"정렬 모드를 종료했어요.");
  } else if (id == 1205 || id == 2003) {
    settings_.hidden = !settings_.hidden;
    save();
    rebuild();
  } else if (id >= 1317 && id <= 1319) {
    auto *w = find(selected_);
    if (w && w->kind == Kind::Calendar)
      calendarMonth(*w, id == 1317 ? -1 : id == 1318 ? 0 : 1);
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
  } else if (id == 1402)
    ShellExecuteW(main_, L"open", L"https://apihub.kma.go.kr/", nullptr, nullptr, SW_SHOWNORMAL);
  else if (id == 1412) {
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
      settings_.theme = theme;
      save();
      rebuild();
      status(L"전체 테마를 적용했어요.");
    }
  } else if (id == 1502) {
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr,
                        0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
      LONG result;
      if (checked(1502)) {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring value = L"\"" + std::wstring(exe) + L"\"";
        result = RegSetValueExW(key, L"Tempos", 0, REG_SZ, reinterpret_cast<const BYTE *>(value.c_str()),
                                DWORD((value.size() + 1) * sizeof(wchar_t)));
      } else {
        result = RegDeleteValueW(key, L"Tempos");
        if (result == ERROR_FILE_NOT_FOUND)
          result = ERROR_SUCCESS;
      }
      RegCloseKey(key);
      status(result == ERROR_SUCCESS ? L"시작 설정을 저장했어요." : L"시작 설정 저장 실패");
    }
  } else if (id == 1503 || id == 1504) {
    wchar_t path[MAX_PATH] = L"tempos-layout.json";
    OPENFILENAMEW ofn{sizeof ofn};
    ofn.hwndOwner = main_;
    ofn.lpstrFilter = L"JSON 레이아웃\0*.json\0\0";
    ofn.lpstrFile = path;
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
          settings_ = std::move(imported);
          save();
          rebuild();
          status(L"레이아웃을 가져왔어요.");
        }
      } catch (...) {
        status(L"파일을 처리할 수 없어요. 형식과 권한을 확인하세요.");
      }
    }
  } else if (id == 1505)
    diagnose();
  else if (id == 1506)
    ShellExecuteW(main_, L"open", store_.root().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  else if (id == 1620) {
    if (auto *w = find(selected_)) {
      w->twelveHour = checked(1600);
      w->fahrenheit = checked(1601);
      w->networkBytes = checked(1602);
      w->binaryDisk = checked(1603);
      w->weekStart = checked(1604) ? 1 : 0;
      for (int i = 0; i < 5; ++i)
        w->systemFields[i] = checked(1610 + i);
      save();
      rebuild();
      status(L"표시 옵션을 적용했어요.");
    }
  } else if (id == 1507 || id == 2005) {
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
  case WM_CTLCOLORSTATIC:
    SetBkMode(reinterpret_cast<HDC>(w), TRANSPARENT);
    return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
  case WM_COMMAND:
    command(LOWORD(w), HIWORD(w));
    return 0;
  case WM_NOTIFY:
    if (reinterpret_cast<NMHDR *>(l)->idFrom == 1000 && reinterpret_cast<NMHDR *>(l)->code == TCN_SELCHANGE) {
      selectTab(TabCtrl_GetCurSel(control(1000)));
      return 0;
    }
    break;
  case WM_TIMER:
    if (w == 1)
      tick();
    return 0;
  case WM_TEMPOS_DATA:
    tick();
    return 0;
  case WM_ACTION:
    EnableWindow(control(1412), TRUE);
    if (w == 1) {
      providers_->invalidate(Kind::Calendar);
      selectTab(tab_);
      status(calendar_.account().status);
    }
    return 0;
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
    }
    return 0;
  case WM_DISPLAYCHANGE:
    rebuild();
    return 0;
  case WM_SETTINGCHANGE:
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
      for (auto &[child, page] : controls_) {
        RECT bounds{};
        GetWindowRect(child, &bounds);
        MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT *>(&bounds), 2);
        SetWindowPos(child, nullptr, MulDiv(bounds.left, nextDpi, settingsDpi_),
                     MulDiv(bounds.top, nextDpi, settingsDpi_),
                     MulDiv(bounds.right - bounds.left, nextDpi, settingsDpi_),
                     MulDiv(bounds.bottom - bounds.top, nextDpi, settingsDpi_),
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
      }
      settingsDpi_ = nextDpi;
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
    scrollSettings(hwnd, bar, position);
    return 0;
  }
  case WM_MOUSEWHEEL:
    scrollSettings(hwnd, SB_VERT, GetScrollPos(hwnd, SB_VERT) - GET_WHEEL_DELTA_WPARAM(w) * 42 / 120);
    return 0;
  case WM_CLOSE:
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
LRESULT App::widgetMessage(WidgetWindow &window, UINT msg, WPARAM wp, LPARAM lp) {
  auto *w = find(window.id);
  HWND hwnd = window.hwnd;
  switch (msg) {
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
    selected_ = window.id;
    menu(hwnd, true);
    return 0;
  case WM_TEMPOS_SETTINGS:
  case WM_LBUTTONDBLCLK:
    openSettings(window.id);
    return 0;
  case WM_KEYDOWN:
    if (wp == VK_RETURN)
      openSettings(window.id);
    else if (w && w->kind == Kind::Calendar && (wp == VK_LEFT || wp == VK_RIGHT))
      calendarMonth(*w, wp == VK_LEFT ? -1 : 1);
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
    if (w && w->kind == Kind::Calendar && (w->size == Size::L || w->size == Size::XL)) {
      float factor = window.dpi / 96.f * w->scale / 100;
      float x = GET_X_LPARAM(lp) / factor, y = GET_Y_LPARAM(lp) / factor;
      auto e = extent(w->size);
      if (y < 61) {
        if (x < 65)
          calendarMonth(*w, -1);
        else if (x > e.width - 65)
          calendarMonth(*w, 1);
        else if (w->size == Size::XL && x > e.width - 130)
          calendarMonth(*w, 0);
      } else {
        using namespace std::chrono;
        auto date = localDate(nowUnix(), w->timezone);
        int yearValue = int(date.year()), monthValue = int(unsigned(date.month()));
        if (!w->source.empty())
          swscanf_s(w->source.c_str(), L"%d-%d", &yearValue, &monthValue);
        auto grid = calendarGrid(yearValue, unsigned(monthValue), w->weekStart);
        float pad = w->size == Size::XL ? 24.f : 20.f, top = w->size == Size::XL ? 110.f : 83.f;
        float rowHeight = w->size == Size::XL ? (e.height - top - 24) / grid.weeks : 140.f / grid.weeks;
        int col = int((x - pad) / ((e.width - pad * 2) / 7)), row = int((y - top) / rowHeight);
        if (x >= pad && y >= top && col >= 0 && col < 7 && row >= 0 && row < grid.weeks)
          details(*w, year_month_day{grid.first + days{row * 7 + col}});
      }
    } else if (w && !settings_.edit && (w->kind == Kind::Calendar || w->kind == Kind::Weather))
      details(*w);
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
        if (place(candidate))
          *w = candidate;
      }
      save();
      position(window);
      draw(window);
      loadControls();
    }
    return 0;
  case WM_CAPTURECHANGED:
    if (window.dragging) {
      window.dragging = false;
      position(window);
    }
    return 0;
  case WM_DPICHANGED:
    if (window.dragging)
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
  j["utc"] = rfc3339(nowUnix());
  j["widgets"] = settings_.widgets.size();
  j["subscriptions"] = providers_ ? providers_->subscriptions() : 0;
  j["privateBytes"] = memory.PrivateUsage;
  j["workingSet"] = memory.WorkingSetSize;
  j["handles"] = handles;
  j["gdiObjects"] = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
  j["userObjects"] = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
  j["redraws"] = redraws_;
  j["softwareRendering"] = renderer_.software();
  j["hidden"] = settings_.hidden;
  j["fixture"] = fixture_;
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
      body += d.condition + L"  " + number(d.temperature) + L"°C\r\n습도 " + number(d.humidity) +
              L"%\r\n바람 " + number(d.wind, 1) + L" m/s\r\n최저 / 최고 " + number(d.low) + L"° / " +
              number(d.high) + L"°\r\n";
      if (d.observed)
        body += L"관측 시각 (UTC) " + wide(rfc3339(d.observed)) + L"\r\n";
      if (d.skyForecast)
        body += L"하늘 상태: 초단기예보 기준\r\n";
    }
    body += L"\r\n자료: 기상청\r\nhttps://apihub.kma.go.kr/\r\n" + data->message;
  }
  UINT dpi = GetDpiForWindow(main_);
  auto hwnd = CreateWindowExW(0, L"Tempos.Details", title.c_str(), WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(480, dpi, 96), MulDiv(520, dpi, 96), main_,
                              nullptr, instance_, body.data());
  if (hwnd)
    SetForegroundWindow(hwnd);
}
} // namespace tempos
