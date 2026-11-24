#include "app.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <set>
#include <windowsx.h>

namespace tempos {
namespace {
constexpr const wchar_t *pages[]{L"위젯", L"연동", L"앱", L"배치", L"정보 및 업데이트"};
constexpr int navigationOrder[]{0, 3, 1, 2, 4};
enum ControlType { Label, Section, Button, Check, Combo, Edit, Password, List, MultiList, Preview };
struct Field {
  int id;
  ControlType type;
  const wchar_t *text;
};
constexpr Field fields[]{{1013, Preview, L""},
                         {1014, Preview, L""},
                         {1010, Label, L"Tempos"},
                         {1011, Label, L""},
                         {1012, Combo, L""},
                         {1200, List, L"내 위젯"},
                         {1201, Combo, L""},
                         {1202, Button, L"추가"},
                         {1203, Button, L"제거"},
                         {1204, Button, L"정렬 모드"},
                         {1205, Button, L"모두 숨기기"},
                         {1206, Button, L"복제"},
                         {1210, Section, L"내 위젯"},
                         {1211, Section, L"미리보기"},
                         {1212, Preview, L"위젯 미리보기"},
                         {1220, Section, L"모양"},
                         {1221, Section, L"표시할 정보"},
                         {1222, Section, L"위치와 동작"},
                         {1223, Label, L"목록에서 사용할 이름"},
                         {1224, Label, L"크기"},
                         {1225, Label, L"확대"},
                         {1226, Label, L"테마"},
                         {1227, Label, L"갱신 주기"},
                         {1228, Label, L"배경 투명도 (%)"},
                         {1229, Label, L"전체 투명도 (%)"},
                         {1230, Label, L"모니터"},
                         {1231, Label, L""},
                         {1232, Label, L"이 크기에서는 그래프가 표시되지 않습니다."},
                         {1233, Label, L"표시할 위젯을 추가하세요."},
                         {1234, Label, L"CPU·GPU 이름은 M과 L 크기에 표시됩니다."},
                         {1300, Combo, L""},
                         {1301, Combo, L""},
                         {1302, Combo, L""},
                         {1303, Combo, L""},
                         {1304, Combo, L""},
                         {1305, Edit, L""},
                         {1306, Edit, L""},
                         {1307, Check, L"클릭 통과"},
                         {1308, Check, L"위치 잠금"},
                         {1309, Check, L"항상 위"},
                         {1310, Check, L"초 표시"},
                         {1311, Check, L"그래프"},
                         {1312, Check, L"날씨에 따른 배경"},
                         {1313, Combo, L""},
                         {1314, Combo, L""},
                         {1315, Edit, L""},
                         {1316, Button, L"적용"},
                         {1317, Button, L"이전 달"},
                         {1318, Button, L"오늘"},
                         {1319, Button, L"다음 달"},
                         {1320, Button, L"현재 위치 찾기"},
                         {1321, Label, L"장치"},
                         {1322, Label, L"날씨 지역"},
                         {1323, Label, L"시간대  ·  비우면 Windows 설정"},
                         {1324, Edit, L""},
                         {1325, Button, L"되돌리기"},
                         {1600, Check, L"12시간 표시 (오전 / 오후)"},
                         {1601, Check, L"화씨 (°F)"},
                         {1602, Check, L"바이트 단위 (KiB/s, MiB/s)"},
                         {1603, Check, L"이진 단위 (GiB, TiB)"},
                         {1604, Check, L"월요일부터 한 주 시작"},
                         {1622, Label, L"표시 항목"},
                         {1610, Check, L"PC 이름"},
                         {1611, Check, L"Windows 버전"},
                         {1612, Check, L"CPU 이름"},
                         {1613, Check, L"GPU 이름"},
                         {1614, Check, L"가동 시간"},
                         {1406, Section, L"날씨 서비스"},
                         {1407, Label, L"모든 날씨 위젯에 사용할 서비스"},
                         {1408, Label, L"기상청 API허브 인증키"},
                         {1403, Combo, L""},
                         {1404, Button, L"날씨 서비스 적용"},
                         {1405, Label, L""},
                         {1400, Password, L""},
                         {1401, Button, L"날씨 인증키 저장"},
                         {1402, Button, L"날씨 서비스 안내"},
                         {1420, Section, L"Google 캘린더"},
                         {1421, Label, L"데스크톱 앱 OAuth 클라이언트 ID"},
                         {1422, Label, L"클라이언트 보안 비밀 (선택)"},
                         {1410, Edit, L""},
                         {1411, Password, L""},
                         {1412, Button, L"브라우저에서 연결"},
                         {1413, Button, L"연결 해제"},
                         {1423, Label, L"표시할 캘린더"},
                         {1414, MultiList, L""},
                         {1415, Button, L"캘린더 선택 저장"},
                         {1416, Label, L""},
                         {1520, Section, L"시작 및 테마"},
                         {1521, Label, L"현재 배치의 기본 테마"},
                         {1500, Combo, L""},
                         {1501, Button, L"전체 테마 적용"},
                         {1502, Check, L"Windows 로그인 시 시작"},
                         {1522, Section, L"CPU 온도"},
                         {1510, Check, L"CPU 온도 표시"},
                         {1511, Label, L""},
                         {1512, Button, L"센서 도우미 설치 / 복구"},
                         {1513, Button, L"센서 도우미 제거"},
                         {1523, Section, L"진단 및 관리"},
                         {1505, Button, L"진단 정보 저장"},
                         {1506, Button, L"데이터 폴더 열기"},
                         {1507, Button, L"Tempos 종료"},
                         {1503, Button, L"레이아웃 내보내기"},
                         {1504, Button, L"레이아웃 가져오기"},
                         {1801, Section, L"저장된 배치"},
                         {1802, List, L"저장된 배치"},
                         {1803, Label, L"배치 이름"},
                         {1804, Edit, L""},
                         {1805, Button, L"현재 배치 복사"},
                         {1806, Button, L"이름 변경"},
                         {1807, Button, L"이 배치 사용"},
                         {1808, Button, L"삭제"},
                         {1809, Section, L"파일로 옮기기"},
                         {1810, Label, L""},
                         {1811, Label, L""},
                         {1720, Section, L"Tempos"},
                         {1700, Label, L""},
                         {1721, Label, L"제작자  Poyal  ·  poyal.work@gmail.com"},
                         {1708, Button, L"사용 안내"},
                         {1706, Button, L"GitHub"},
                         {1707, Button, L"문의·오류 신고"},
                         {1712, Button, L"이메일"},
                         {1722, Section, L"업데이트"},
                         {1713, Check, L"앱을 시작할 때 새 버전 확인"},
                         {1701, Label, L""},
                         {1709, Label, L""},
                         {1702, Button, L"업데이트 확인"},
                         {1703, Button, L"업데이트 설치"},
                         {1704, Button, L"취소"},
                         {1705, Button, L"변경 내역 보기"},
                         {1710, Label, L""},
                         {1714, Label, L""},
                         {1999, Label, L""}};
void comboItem(HWND ctrl, const wchar_t *value) {
  SendMessageW(ctrl, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
}
void rounded(HDC dc, RECT r, COLORREF fill, COLORREF stroke, int radius) {
  auto pen = CreatePen(PS_SOLID, 1, stroke);
  auto brush = CreateSolidBrush(fill);
  auto oldPen = SelectObject(dc, pen), oldBrush = SelectObject(dc, brush);
  RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
  SelectObject(dc, oldBrush);
  SelectObject(dc, oldPen);
  DeleteObject(brush);
  DeleteObject(pen);
}
} // namespace
void App::buildSettings() {
  loadRegions();
  settingsDpi_ = GetDpiForWindow(main_);
  auto create = [&](int id, ControlType type, const wchar_t *text) {
    const wchar_t *cls = L"STATIC";
    DWORD style = WS_CHILD | WS_CLIPSIBLINGS, ex = 0;
    switch (type) {
    case Section:
    case Label:
      style |= SS_NOPREFIX;
      break;
    case Preview:
      style |= SS_OWNERDRAW;
      break;
    case Button:
      cls = L"BUTTON";
      style |= WS_TABSTOP | BS_OWNERDRAW;
      break;
    case Check:
      cls = L"BUTTON";
      style |= WS_TABSTOP | BS_AUTOCHECKBOX;
      break;
    case Combo:
      cls = L"COMBOBOX";
      style |= WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL;
      break;
    case List:
    case MultiList:
      cls = L"LISTBOX";
      style |= WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT;
      if (type == MultiList)
        style |= LBS_EXTENDEDSEL;
      ex = WS_EX_CLIENTEDGE;
      break;
    case Edit:
    case Password:
      cls = L"EDIT";
      style |= WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL;
      if (type == Password)
        style |= ES_PASSWORD;
      break;
    }
    HWND hwnd = CreateWindowExW(ex, cls, text, style, 0, 0, 1, 1, main_, reinterpret_cast<HMENU>(INT_PTR(id)),
                                instance_, nullptr);
    controls_.push_back({hwnd, int(type)});
    if (style & WS_TABSTOP)
      SetWindowSubclass(
          hwnd,
          [](HWND child, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR ref) -> LRESULT {
            auto app = reinterpret_cast<App *>(ref);
            if (msg == WM_SETFOCUS)
              app->focusField(child);
            return DefSubclassProc(child, msg, w, l);
          },
          2, reinterpret_cast<DWORD_PTR>(this));
    if (type == Combo)
      SetWindowSubclass(hwnd, comboProc, 1, reinterpret_cast<DWORD_PTR>(this));
  };
  for (int page : navigationOrder)
    create(1000 + page, Button, pages[page]);
  for (auto &field : fields)
    create(field.id, field.type, field.text);
  for (int page : navigationOrder) {
    auto cb = control(1012);
    int item = int(SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(pages[page])));
    SendMessageW(cb, CB_SETITEMDATA, item, page);
  }
  for (int k = 0; k < int(KindCount); ++k)
    comboItem(control(1201), kindName(Kind(k)).data());
  SendMessageW(control(1201), CB_SETCURSEL, 0, 0);
  for (auto value : {L"100%", L"150%", L"200%"})
    comboItem(control(1301), value);
  comboItem(control(1302), L"배치 테마 따르기");
  for (auto &p : palettes()) {
    comboItem(control(1302), p.name);
    comboItem(control(1500), p.name);
  }
  for (auto &r : regions_)
    comboItem(control(1314), r.name.c_str());
  for (auto value : {L"기상청", L"Open-Meteo"})
    comboItem(control(1403), value);
  SendMessageW(control(1324), EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"예: 서울 시계"));
  for (int id : {1324, 1804})
    SendMessageW(control(id), EM_SETLIMITTEXT, 64, 0);
  SetWindowTextW(control(1700), (L"현재 버전 " + wide(TEMPOS_VERSION_STRING) + L"  ·  Windows x64").c_str());
  SendMessageW(control(1502), BM_SETCHECK, startup_.enabled() ? BST_CHECKED : BST_UNCHECKED, 0);
  settingsBuilt_ = true;
  settingsTheme();
  weatherControls();
  sensorControls();
  updateControls();
  selectTab(0);
}
void App::settingsTheme() {
  if (!settingsBuilt_)
    return;
  HIGHCONTRASTW hc{sizeof hc};
  highContrast_ =
      SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof hc, &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
  DWORD light = 1, len = sizeof light;
  RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
               L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &len);
#ifdef TEMPOS_TEST_HOST
  if (fixture_) {
    auto mode = boundedRead(store_.root() / L"settings-theme-example.txt", 32);
    if (mode == "light" || mode == "dark") {
      highContrast_ = false;
      light = mode == "light";
    }
    if (mode == "contrast")
      highContrast_ = true;
  }
#endif
  dark_ = !highContrast_ && !light;
  auto tone = [&](COLORREF lightColor, COLORREF darkColor, int system) {
    return highContrast_ ? GetSysColor(system) : dark_ ? darkColor : lightColor;
  };
  canvasColor_ = tone(RGB(243, 245, 248), RGB(27, 30, 35), COLOR_WINDOW);
  paneColor_ = tone(RGB(255, 255, 255), RGB(38, 42, 48), COLOR_WINDOW);
  textColor_ = tone(RGB(26, 37, 50), RGB(237, 240, 245), COLOR_WINDOWTEXT);
  accentColor_ = tone(RGB(25, 86, 158), RGB(151, 198, 255), COLOR_HIGHLIGHT);
  borderColor_ = tone(RGB(215, 221, 229), RGB(69, 75, 85), COLOR_WINDOWFRAME);
  inputColor_ = tone(RGB(250, 251, 253), RGB(47, 52, 59), COLOR_WINDOW);
  for (auto brush : {canvasBrush_, paneBrush_, inputBrush_})
    if (brush)
      DeleteObject(brush);
  canvasBrush_ = CreateSolidBrush(canvasColor_);
  paneBrush_ = CreateSolidBrush(paneColor_);
  inputBrush_ = CreateSolidBrush(inputColor_);
  auto makeFont = [&](int size, int weight, const wchar_t *family = L"Segoe UI") {
    return CreateFontW(-MulDiv(size, settingsDpi_, 96), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                       family);
  };
  auto oldTitle = titleFont_, oldSection = sectionFont_, oldIcon = navIconFont_;
  titleFont_ = makeFont(26, FW_SEMIBOLD);
  sectionFont_ = makeFont(16, FW_SEMIBOLD);
  navIconFont_ = makeFont(16, FW_NORMAL, L"Segoe MDL2 Assets");
  for (auto [child, type] : controls_) {
    int id = GetDlgCtrlID(child);
    SetWindowTheme(child, dark_ ? L"DarkMode_Explorer" : nullptr, nullptr);
    SendMessageW(child, WM_SETFONT,
                 reinterpret_cast<WPARAM>(id == 1011                      ? titleFont_
                                          : id == 1010 || type == Section ? sectionFont_
                                                                          : font_),
                 FALSE);
  }
  if (oldTitle)
    DeleteObject(oldTitle);
  if (oldSection)
    DeleteObject(oldSection);
  if (oldIcon)
    DeleteObject(oldIcon);
  BOOL dark = dark_;
  DwmSetWindowAttribute(main_, 20, &dark, sizeof dark);
  RedrawWindow(main_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}
LRESULT CALLBACK App::comboProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
  auto app = reinterpret_cast<App *>(ref);
  if (msg == WM_NCDESTROY)
    RemoveWindowSubclass(hwnd, comboProc, 1);
  if (msg != WM_PAINT && msg != WM_PRINTCLIENT || !app->settingsBuilt_ || app->highContrast_)
    return DefSubclassProc(hwnd, msg, wp, lp);
  PAINTSTRUCT ps{};
  auto dc = msg == WM_PAINT ? BeginPaint(hwnd, &ps) : reinterpret_cast<HDC>(wp);
  RECT r{};
  GetClientRect(hwnd, &r);
  SaveDC(dc);
  FillRect(dc, &r, app->paneBrush_);
  rounded(dc, r, app->inputColor_, GetFocus() == hwnd ? app->accentColor_ : app->borderColor_, 7);
  SelectObject(dc, app->font_);
  SetBkMode(dc, TRANSPARENT);
  auto color = IsWindowEnabled(hwnd) ? app->textColor_ : GetSysColor(COLOR_GRAYTEXT);
  SetTextColor(dc, color);
  int inset = MulDiv(9, app->settingsDpi_, 96), mid = (r.top + r.bottom) / 2;
  auto pen = CreatePen(PS_SOLID, std::max(1, MulDiv(1, app->settingsDpi_, 96)), color);
  auto old = SelectObject(dc, pen);
  int cx = r.right - inset - 4;
  MoveToEx(dc, cx - 4, mid - 2, nullptr);
  LineTo(dc, cx, mid + 2);
  LineTo(dc, cx + 5, mid - 3);
  SelectObject(dc, old);
  DeleteObject(pen);
  r.left += inset;
  r.right = cx - inset;
  auto value = app->input(GetDlgCtrlID(hwnd));
  DrawTextW(dc, value.c_str(), int(value.size()), &r,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
  RestoreDC(dc, -1);
  if (msg == WM_PAINT)
    EndPaint(hwnd, &ps);
  return 0;
}
void App::selectTab(int page) {
  if (!settingsBuilt_ || page < 0 || page > 4)
    return;
  if (page != tab_ && tab_ == 0 && !leaveEditor()) {
    updateViewport();
    return;
  }
  tab_ = page;
  SetScrollPos(main_, SB_VERT, 0, TRUE);
  SetWindowTextW(control(1011), pages[page]);
  if (page == 1) {
    weatherControls();
    auto account = calendar_.account();
    SetWindowTextW(control(1416), account.status.c_str());
    SendMessageW(control(1414), LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < account.calendars.size(); ++i) {
      auto &c = account.calendars[i];
      SendMessageW(control(1414), LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(c.name.c_str()));
      SendMessageW(
          control(1414), LB_SETSEL,
          std::find(account.selected.begin(), account.selected.end(), c.id) != account.selected.end(), i);
    }
  }
  if (page == 2)
    sensorControls();
  if (page == 3)
    refreshLayouts();
  if (page == 4)
    updateControls();
  updateViewport();
}
void App::layoutWidgetFields() {
  updateViewport();
}
void App::updateViewport() {
  if (!settingsBuilt_ || layoutBusy_)
    return;
  layoutBusy_ = true;
  RECT client{};
  GetClientRect(main_, &client);
  const int dpi = int(settingsDpi_), cw = MulDiv(client.right, 96, dpi), ch = MulDiv(client.bottom, 96, dpi);
  auto px = [&](int v) { return MulDiv(v, dpi, 96); };
  bool rail = cw >= 700;
  int left = rail ? 180 : 14, right = cw - (rail ? 24 : 14);
  int top = rail ? 0 : 48, bottom = std::max(top + 32, ch - 58);
  int offset = GetScrollPos(main_, SB_VERT);
  std::set<int> visible;
  panels_.clear();
  tabStops_.clear();
  auto put = [&](int id, int x, int y, int width, int height, bool fixed = false) {
    HWND hwnd = control(id);
    visible.insert(id);
    if (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_TABSTOP)
      tabStops_.push_back(hwnd);
    int py = px(y) - (fixed ? 0 : offset);
    bool combo = id == 1012 || id == 1201 || (id >= 1300 && id <= 1304) || id == 1313 || id == 1314 ||
                 id == 1403 || id == 1500;
    int w = px(std::max(1, width)), h = px(height);
    SetWindowPos(hwnd, nullptr, px(x), py, w, combo ? px(248) : h, SWP_NOACTIVATE | SWP_NOZORDER);
    if (!fixed && (py < px(top) || py + h > px(bottom))) {
      int start = std::clamp(px(top) - py, 0, h), end = std::clamp(px(bottom) - py, 0, h);
      SetWindowRgn(hwnd, CreateRectRgn(0, start, w, end), TRUE);
    } else
      SetWindowRgn(hwnd, nullptr, FALSE);
    if (!(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VISIBLE))
      ShowWindow(hwnd, SW_SHOWNA);
  };
  put(1014, 0, bottom, cw, ch - bottom, true);
  if (!rail)
    put(1013, 0, 0, cw, top, true);
  if (rail) {
    put(1010, 22, 23, 130, 30, true);
    for (int row = 0; row < 5; ++row)
      put(1000 + navigationOrder[row], 10, 75 + row * 43, 146, 36, true);
  } else {
    put(1012, 14, 10, std::max(100, cw - 28), 30, true);
    auto i = std::find(std::begin(navigationOrder), std::end(navigationOrder), tab_);
    SendMessageW(control(1012), CB_SETCURSEL, i - std::begin(navigationOrder), 0);
  }
  int y = top + 16, x = left + 16, width = std::max(80, right - left - 32), panelStart = 0;
  put(1011, left + 2, y, right - left, 41);
  y += 54;
  auto start = [&](int title) {
    panelStart = y;
    y += 14;
    put(title, x, y, width, 27);
    y += 36;
  };
  auto finish = [&] {
    y += 7;
    panels_.push_back({px(left), px(panelStart) - offset, px(right), px(y) - offset});
    y += 12;
  };
  auto inlineError = [&](int id) {
    if (id == errorField_) {
      put(1231, x, y, width, 36);
      y += 40;
    }
  };
  auto row = [&](int id, int height = 30) {
    put(id, x, y, width, height);
    y += height + 7;
    inlineError(id);
  };
  auto field = [&](int label, int id) {
    put(label, x, y, width, 21);
    y += 23;
    row(id);
  };
  auto pair = [&](int la, int a, int lb, int b) {
    if (width < 370) {
      field(la, a);
      field(lb, b);
      return;
    }
    int half = (width - 14) / 2;
    put(la, x, y, half, 21);
    put(lb, x + half + 14, y, width - half - 14, 21);
    y += 23;
    put(a, x, y, half, 30);
    put(b, x + half + 14, y, width - half - 14, 30);
    y += 38;
    inlineError(a);
    inlineError(b);
  };
  auto actions = [&](std::initializer_list<int> ids) {
    int count = int(ids.size()), perLine = std::min(count, std::max(1, (width + 8) / 112));
    int w = std::min(190, (width - (perLine - 1) * 8) / perLine), col = 0;
    for (int id : ids) {
      put(id, x + col * (w + 8), y, w, 32);
      if (++col == perLine) {
        col = 0;
        y += 40;
      }
    }
    if (col)
      y += 40;
  };
  auto *widget = find(selected_);
  if (tab_ == 0) {
    SetWindowTextW(control(1204), settings_.edit ? L"정렬 종료" : L"정렬 모드");
    SetWindowTextW(control(1205), settings_.hidden ? L"모두 표시" : L"모두 숨기기");
    start(1210);
    int listTop = y;
    bool split = widget && width >= 480;
    int listWidth = split ? (width - 18) / 2 : width;
    if (split)
      put(1210, x, listTop - 36, listWidth, 27);
    put(1200, x, y, listWidth, 76);
    y += 84;
    put(1201, x, y, listWidth - 74, 30);
    put(1202, x + listWidth - 66, y, 66, 30);
    y += 38;
    if (widget) {
      put(1206, x, y, (listWidth - 8) / 2, 30);
      put(1203, x + (listWidth - 8) / 2 + 8, y, (listWidth - 8) / 2, 30);
      y += 38;
      int previewX = split ? x + listWidth + 18 : x, previewY = split ? listTop - 34 : y;
      int previewWidth = split ? width - listWidth - 18 : width;
      put(1211, previewX, previewY, previewWidth, 26);
      Size size = choice(1300) >= 0 && choice(1300) < int(sizeChoices_.size()) ? sizeChoices_[choice(1300)]
                                                                               : widget->size;
      int scale = choice(1301) == 1 ? 150 : choice(1301) == 2 ? 200 : 100;
      auto e = extent(size, scale);
      float factor = std::min({1.f, previewWidth / e.width, 145.f / e.height});
      int w = std::max(1, int(e.width * factor)), h = std::max(1, int(e.height * factor));
      put(1212, previewX, previewY + 32, w, h);
      y = std::max(y, previewY + 32 + h + 8);
    } else
      row(1233);
    finish();
    if (widget) {
      start(1220);
      field(1223, 1324);
      pair(1224, 1300, 1225, 1301);
      pair(1226, 1302, 1227, 1303);
      pair(1228, 1305, 1229, 1306);
      finish();
      auto kind = widget->kind;
      auto size = choice(1300) >= 0 && choice(1300) < int(sizeChoices_.size()) ? sizeChoices_[choice(1300)]
                                                                               : widget->size;
      start(1221);
      if (kind == Kind::Clock) {
        row(1310);
        row(1600);
        field(1323, 1315);
      }
      if (kind == Kind::Weather) {
        actions({1312, 1601});
        field(1322, 1314);
        actions({1320});
      }
      if (kind == Kind::Network || kind == Kind::Gpu || (kind == Kind::Disk && size != Size::L)) {
        SetWindowTextW(control(1321), kind == Kind::Gpu    ? L"GPU 장치"
                                      : kind == Kind::Disk ? L"볼륨"
                                                           : L"네트워크 집계 대상");
        field(1321, 1313);
      }
      if (kind == Kind::Cpu || kind == Kind::Gpu || kind == Kind::Memory || kind == Kind::Network) {
        bool graph = size != Size::S && !(size == Size::Slim && kind == Kind::Network);
        EnableWindow(control(1311), graph);
        row(1311);
        if (!graph)
          row(1232);
      }
      if (kind == Kind::Network)
        row(1602);
      if (kind == Kind::Disk)
        row(1603);
      if (kind == Kind::System) {
        row(1622, 22);
        actions({1610, 1611, 1614});
        bool hardware = size == Size::M || size == Size::L;
        EnableWindow(control(1612), hardware);
        EnableWindow(control(1613), hardware);
        actions({1612, 1613});
        if (!hardware)
          row(1234);
      }
      if (kind == Kind::Calendar) {
        row(1604);
        field(1323, 1315);
        if (size == Size::L || size == Size::XL)
          actions({1317, 1318, 1319});
      }
      finish();
      start(1222);
      field(1230, 1304);
      actions({1307, 1308, 1309});
      actions({1204, 1205});
      finish();
    }
  } else if (tab_ == 3) {
    start(1801);
    row(1811);
    row(1802, 104);
    field(1803, 1804);
    actions({1805, 1806, 1807, 1808});
    finish();
    start(1809);
    row(1810, 46);
    actions({1503, 1504});
    finish();
  } else if (tab_ == 1) {
    start(1406);
    field(1407, 1403);
    row(1405, 40);
    actions({1404, 1402});
    if (choice(1403) == 0) {
      field(1408, 1400);
      actions({1401});
    }
    finish();
    start(1420);
    row(1416);
    field(1421, 1410);
    field(1422, 1411);
    actions({1412, 1413});
    row(1423, 22);
    row(1414, 75);
    actions({1415});
    finish();
  } else if (tab_ == 2) {
    start(1520);
    field(1521, 1500);
    actions({1501});
    row(1502);
    finish();
    start(1522);
    row(1510);
    row(1511);
    actions({1512, 1513});
    finish();
    start(1523);
    actions({1505, 1506, 1507});
    finish();
  } else {
    start(1720);
    row(1700);
    row(1721);
    actions({1708, 1706, 1707, 1712});
    finish();
    start(1722);
    row(1713);
    row(1701, 44);
    row(1709);
    actions({1702, 1703, 1704, 1705});
    row(1710, 80);
    if (GetWindowTextLengthW(control(1714)))
      row(1714, 50);
    finish();
  }
  int footerX = rail ? left : 14;
  int footerWidth = right - footerX, buttons = widget && tab_ == 0 ? std::min(196, footerWidth / 2) : 0;
  put(1999, footerX, bottom + 7, footerWidth - buttons - 8, 45, true);
  if (buttons) {
    int w = (buttons - 8) / 2;
    put(1325, right - buttons, bottom + 10, w, 34, true);
    put(1316, right - w, bottom + 10, w, 34, true);
    EnableWindow(control(1325), dirty_);
  }
  for (auto [child, type] : controls_)
    if (!visible.contains(GetDlgCtrlID(child)) && (GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE))
      ShowWindow(child, SW_HIDE);
  for (int fixedId : {1013, 1014, 1012, 1999, 1325, 1316})
    if (visible.contains(fixedId))
      SetWindowPos(control(fixedId), HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
  SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE};
  si.nMax = px(y - top + 8) - 1;
  si.nPage = UINT(px(bottom - top));
  SetScrollInfo(main_, SB_VERT, &si, TRUE);
  RECT actual{};
  GetClientRect(main_, &actual);
  layoutBusy_ = false;
  if (offset != GetScrollPos(main_, SB_VERT) || actual.right != client.right) {
    updateViewport();
    return;
  }
  InvalidateRect(main_, nullptr, FALSE);
}
void App::scrollTo(int pos) {
  int old = GetScrollPos(main_, SB_VERT);
  SetScrollPos(main_, SB_VERT, pos, TRUE);
  if (old != GetScrollPos(main_, SB_VERT))
    updateViewport();
}
void App::focusField(HWND field) {
  if (!field || layoutBusy_ || GetDlgCtrlID(field) < 1200 || GetDlgCtrlID(field) == 1316 ||
      GetDlgCtrlID(field) == 1325)
    return;
  RECT r{}, client{};
  GetWindowRect(field, &r);
  MapWindowPoints(nullptr, main_, reinterpret_cast<POINT *>(&r), 2);
  GetClientRect(main_, &client);
  int top = MulDiv(client.right, 96, settingsDpi_) < 700 ? MulDiv(54, settingsDpi_, 96) : 0;
  int bottom = client.bottom - MulDiv(64, settingsDpi_, 96);
  if (r.top < top || r.bottom > bottom)
    scrollTo(GetScrollPos(main_, SB_VERT) + (r.top < top ? r.top - top : r.bottom - bottom));
}
void App::paintSettings(HDC dc) {
  RECT client{};
  GetClientRect(main_, &client);
  FillRect(dc, &client, canvasBrush_);
  int footer = std::max(0L, client.bottom - MulDiv(58, settingsDpi_, 96));
  SaveDC(dc);
  int top = MulDiv(client.right, 96, settingsDpi_) < 700 ? MulDiv(48, settingsDpi_, 96) : 0;
  IntersectClipRect(dc, 0, top, client.right, footer);
  for (auto panel : panels_)
    rounded(dc, panel, paneColor_, borderColor_, MulDiv(14, settingsDpi_, 96));
  RestoreDC(dc, -1);
  if (!top) {
    RECT side{0, 0, MulDiv(162, settingsDpi_, 96), client.bottom};
    FillRect(dc, &side, paneBrush_);
  }
}
void App::drawSettingItem(const DRAWITEMSTRUCT &d) {
  int id = int(d.CtlID);
  if (id == 1013 || id == 1014) {
    FillRect(d.hDC, &d.rcItem, canvasBrush_);
    return;
  }
  if (id == 1212) {
    FillRect(d.hDC, &d.rcItem, paneBrush_);
    int field = 0;
    std::wstring message;
    auto draft = editorValue(field, message);
    auto saved = find(selected_);
    if (!saved)
      return;
    auto &widget = draft ? *draft : *saved;
    auto data = providers_->snapshot(widget);
    if (renderer_.draw(d.hwndItem, previewSurface_, widget, data.get(), settings_.theme, false, settingsDpi_,
                       d.hDC))
      ++previewDraws_;
    return;
  }
  bool nav = id >= 1000 && id <= 1004, active = nav && id == 1000 + tab_;
  bool disabled = d.itemState & ODS_DISABLED;
  bool primary = !disabled && (id == 1316 || id == 1807 || active);
  COLORREF fill = primary ? accentColor_ : nav ? paneColor_ : inputColor_;
  if (d.itemState & ODS_SELECTED)
    fill = borderColor_;
  FillRect(d.hDC, &d.rcItem, nav || (id != 1316 && id != 1325) ? paneBrush_ : canvasBrush_);
  rounded(d.hDC, d.rcItem, fill,
          primary ? fill
          : nav   ? paneColor_
                  : borderColor_,
          MulDiv(9, settingsDpi_, 96));
  SetBkMode(d.hDC, TRANSPARENT);
  SetTextColor(d.hDC, disabled  ? GetSysColor(COLOR_GRAYTEXT)
                      : primary ? (highContrast_ ? GetSysColor(COLOR_HIGHLIGHTTEXT)
                                   : dark_       ? RGB(14, 29, 46)
                                                 : RGB(255, 255, 255))
                                : textColor_);
  RECT r = d.rcItem;
  if (nav) {
    // Widgets, connections, app, layouts, about in native control ID order.
    static constexpr wchar_t icons[]{0xe8a5, 0xe71b, 0xe713, 0xe8a9, 0xe946};
    RECT icon = r;
    icon.left += MulDiv(10, settingsDpi_, 96);
    icon.right = icon.left + MulDiv(20, settingsDpi_, 96);
    auto previous = SelectObject(d.hDC, navIconFont_);
    DrawTextW(d.hDC, &icons[id - 1000], 1, &icon, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    SelectObject(d.hDC, previous);
    r.left += MulDiv(39, settingsDpi_, 96);
  }
  auto label = input(id);
  DrawTextW(d.hDC, label.c_str(), int(label.size()), &r,
            DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | (nav ? DT_LEFT : DT_CENTER));
  if ((d.itemState & ODS_FOCUS) && !(d.itemState & ODS_NOFOCUSRECT)) {
    r = d.rcItem;
    InflateRect(&r, -3, -3);
    DrawFocusRect(d.hDC, &r);
  }
}
void App::editorChanged() {
  if (loading_ || !settingsBuilt_ || !find(selected_))
    return;
  int field = 0;
  std::wstring error;
  auto widget = editorValue(field, error);
  dirty_ = !widget || widgetJson(*widget) != widgetJson(*find(selected_));
  errorField_ = 0;
  status(dirty_ ? L"아직 적용하지 않은 변경이 있습니다." : L"");
  updateViewport();
  InvalidateRect(control(1212), nullptr, FALSE);
}
bool App::leaveEditor() {
  if (!dirty_)
    return true;
  TASKDIALOG_BUTTON buttons[]{{101, L"적용"}, {102, L"버리기"}, {IDCANCEL, L"계속 편집"}};
  TASKDIALOGCONFIG config{sizeof config};
  config.hwndParent = main_;
  config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
  config.pszWindowTitle = L"Tempos";
  config.pszMainInstruction = L"위젯 설정 변경을 적용할까요?";
  config.pButtons = buttons;
  config.cButtons = 3;
  config.nDefaultButton = IDCANCEL;
  int result = IDCANCEL;
  if (FAILED(TaskDialogIndirect(&config, &result, nullptr, nullptr)))
    return false;
  if (result == 101)
    return applyControls();
  if (result == 102) {
    loadControls();
    status(L"");
    return true;
  }
  return false;
}
void App::duplicateWidget() {
  auto saved = find(selected_);
  if (!saved)
    return;
  if (settings_.widgets.size() >= 64) {
    status(L"한 배치에 최대 64개까지 추가할 수 있습니다.");
    return;
  }
  Widget copy = *saved;
  copy.id = newId();
  copy.alias =
      (copy.alias.empty() ? std::wstring(kindName(copy.kind)) : copy.alias).substr(0, 59) + L" 복사본";
  bool placed = place(copy, false);
  for (const auto &monitor : monitors_) {
    if (placed)
      break;
    copy.monitor = monitor.id;
    placed = place(copy, false);
  }
  if (!placed) {
    status(L"같은 크기의 복제본을 놓을 빈 공간이 없습니다.");
    return;
  }
  auto next = settings_;
  next.widgets.push_back(copy);
  if (commit(std::move(next))) {
    selected_ = copy.id;
    rebuild();
    status(L"위젯을 복제했습니다.");
  }
}
void App::refreshLayouts() {
  if (!settingsBuilt_)
    return;
  HWND list = control(1802);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  auto add = [&](const Layout &layout, bool active) {
    auto name = layout.layoutName + L"  ·  " + std::to_wstring(layout.widgets.size()) + L"개" +
                (active ? L"  ·  사용 중" : L"");
    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
  };
  add(settings_, true);
  for (const auto &layout : settings_.otherLayouts)
    add(layout, false);
  SendMessageW(list, LB_SETCURSEL, 0, 0);
  SetWindowTextW(control(1804), settings_.layoutName.c_str());
  SetWindowTextW(control(1811), (L"사용 중인 배치   " + settings_.layoutName).c_str());
  SetWindowTextW(control(1810), (L"대상 배치   " + settings_.layoutName +
                                 L"\r\n가져오면 이 배치와 모든 배치의 날씨 서비스가 파일의 값으로 바뀝니다.")
                                    .c_str());
  EnableWindow(control(1807), FALSE);
  EnableWindow(control(1808), FALSE);
}
void App::layoutCommand(int id, size_t index) {
  if (index > settings_.otherLayouts.size())
    return;
  auto next = settings_;
  auto &target = index ? next.otherLayouts[index - 1] : static_cast<Layout &>(next);
  if (id == 1805 || id == 1806) {
    auto name = input(1804);
    auto first = name.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
      status(L"배치 이름을 입력하세요.");
      return;
    }
    name = name.substr(first, name.find_last_not_of(L" \t\r\n") - first + 1);
    auto used = [&](const Layout &layout) {
      return layout.layoutName == name && (id == 1805 || layout.layoutId != target.layoutId);
    };
    if (used(next) || std::any_of(next.otherLayouts.begin(), next.otherLayouts.end(), used)) {
      status(L"다른 배치 이름을 입력하세요.");
      return;
    }
    if (id == 1806)
      target.layoutName = name;
    else {
      Layout copy = next;
      copy.layoutId = newId();
      copy.layoutName = name;
      for (auto &widget : copy.widgets)
        widget.id = newId();
      next.otherLayouts.push_back(static_cast<const Layout &>(next));
      static_cast<Layout &>(next) = std::move(copy);
    }
  } else if (id == 1807 && index) {
    std::swap(static_cast<Layout &>(next), next.otherLayouts[index - 1]);
  } else if (id == 1808 && index) {
    if (MessageBoxW(main_, (target.layoutName + L" 배치를 삭제할까요?").c_str(), L"배치 삭제",
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
      return;
    next.otherLayouts.erase(next.otherLayouts.begin() + index - 1);
  } else
    return;
  bool switched = next.layoutId != settings_.layoutId;
  if (!commit(std::move(next)))
    return;
  if (switched) {
    location_.cancel();
    locationTarget_.clear();
    selected_.clear();
    dirty_ = false;
    rebuild();
  }
  refreshLayouts();
  status(L"사용 중인 배치 · " + settings_.layoutName);
}
} // namespace tempos
