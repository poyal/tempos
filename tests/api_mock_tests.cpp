#include "api_fixture.h"
#include "calendar.h"
using namespace tempos;
extern void check(bool, const char *);
void apiMockTests() {
  auto root = std::filesystem::temp_directory_path() / (L"tempos-api-mock-" + newId());
  Store store(root);
  HttpClient http(mockApi(root));
  check(http.request(L"api.open-meteo.com", L"/unexpected").error == ERROR_ACCESS_DENIED,
        "Unregistered mock API endpoint fails without live fallback");
  check(http.request(L"api.open-meteo.com", L"/v1/forecast?latitude=0").ok(),
        "Registered weather mock uses the normal HTTP boundary");
  store.saveSecret(L"calendar-client", "mock-client.apps.googleusercontent.com");
  CalendarService calendar(store, http, false, mockBrowser(root));
  calendar.connect({});
  check(calendar.account().connected && calendar.account().calendars.size() == 1,
        "Mock OAuth loopback callback and token exchange connect the real calendar service");
  Widget widget;
  widget.kind = Kind::Calendar;
  atomicWrite(root / L"api-scenario.json", R"({"calendar":"pages"})");
  auto data = calendar.read(widget, {});
  check(data.status == Status::Ready && data.calendar->events.size() == 2,
        "Mock calendar pagination passes through the real event parser");
  atomicWrite(root / L"api-scenario.json", R"({"calendar":"malformed"})");
  data = calendar.read(widget, {});
  check(data.status == Status::Stale && data.calendar->events.size() == 2,
        "Malformed mock response preserves the actual private cache");
  calendar.disconnect();
  check(store.secret(L"calendar-refresh").empty() && store.secret(L"calendar-events").empty(),
        "Mock API disconnect removes account secrets and event cache");
  atomicWrite(root / L"api-scenario.json", R"({"authorization":"denied"})");
  calendar.connect({});
  check(!calendar.account().connected, "Mock authorization denial does not connect an account");
}
