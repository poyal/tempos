#pragma once
// TestHost only: mock the external boundary, retain HTTP parsers and OAuth callback validation.
#include <winsock2.h>
#include "weather_fixture.h"
namespace tempos {
inline void mockApiLog(const std::filesystem::path &root, std::string endpoint, bool matched,
                       std::string mode = "normal") {
  static std::mutex mutex;
  std::lock_guard lock(mutex);
  std::ofstream(root / L"api-mock-requests.jsonl", std::ios::app)
      << Json({{"endpoint", endpoint}, {"matched", matched}, {"mode", mode}, {"transport", "mock"}}).dump() << '\n';
}
inline Json mockApiOptions(const std::filesystem::path &root) {
  if (auto text = boundedRead(root / L"api-scenario.json"))
    return Json::parse(*text);
  return Json::object();
}
inline HttpClient::Transport mockApi(const std::filesystem::path &root) {
  return [root, weather = weatherFixture(root)](const std::wstring &host, const std::wstring &path,
             const std::wstring &method, const std::string &body, const std::wstring &headers,
             std::stop_token stop) -> Response {
    const bool weatherRoute = method == L"GET" &&
        ((host == L"api.open-meteo.com" && path.starts_with(L"/v1/forecast?")) ||
         (host == L"apihub.kma.go.kr" &&
          (path.starts_with(L"/api/typ02/openApi/VilageFcstInfoService_2.0/getUltraSrtNcst?") ||
           path.starts_with(L"/api/typ02/openApi/VilageFcstInfoService_2.0/getUltraSrtFcst?") ||
           path.starts_with(L"/api/typ02/openApi/VilageFcstInfoService_2.0/getVilageFcst?"))));
    const bool token = host == L"oauth2.googleapis.com" && path == L"/token" && method == L"POST";
    const bool list = host == L"www.googleapis.com" && method == L"GET" &&
                     path.starts_with(L"/calendar/v3/users/me/calendarList?");
    const bool events = host == L"www.googleapis.com" && method == L"GET" &&
                       path.starts_with(L"/calendar/v3/calendars/mock-calendar/events?");
    const bool matched = weatherRoute || token || list || events;
    auto endpoint = utf8(host + path.substr(0, path.find(L'?'))); // Never log credentials or coordinates.
    if (weatherRoute) {
      mockApiLog(root, endpoint, true);
      return weather(host, path, method, body, headers, stop);
    }
    Json options;
    try { options = mockApiOptions(root); }
    catch (...) { mockApiLog(root, endpoint, false, "invalid-scenario"); return {0, ERROR_INVALID_DATA, {}, 0}; }
    auto mode = options.value(token ? "token" : "calendar", std::string("normal"));
    mockApiLog(root, endpoint, matched, mode);
    if (!matched) return {0, ERROR_ACCESS_DENIED, {}, 0};
    if (stop.stop_requested()) return {0, ERROR_CANCELLED, {}, 0};
    if (mode == "timeout") return {0, ERROR_WINHTTP_TIMEOUT, {}, 0};
    if (mode == "unauthorized") return {401, 0, R"({"error":"invalid_grant"})", 0};
    if (mode == "forbidden") return {403, 0, "{}", 0};
    if (mode == "quota") return {429, 0, "{}", 3600};
    if (mode == "server-error") return {503, 0, "{}", 60};
    if (mode == "malformed") return {200, 0, "not-json", 0};
    if (token) {
      bool exchange = body.find("grant_type=authorization_code") != std::string::npos;
      bool refresh = body.find("grant_type=refresh_token") != std::string::npos;
      if (!exchange && !refresh) return {400, 0, "{}", 0};
      mockApiLog(root, exchange ? "oauth-exchange" : "oauth-refresh", true);
      return {200, 0, R"({"access_token":"mock-access","refresh_token":"mock-refresh","expires_in":3600})", 0};
    }
    if (list)
      return {200, 0, R"({"items":[{"id":"mock-calendar","summary":"목업 캘린더","backgroundColor":"#78b9ff"}]})", 0};
    Json response = {{"items", Json::array()}};
    if (mode != "empty") {
      bool next = path.find(L"pageToken=next") != std::wstring::npos;
      auto date = localDate(nowUnix(), L"Asia/Seoul");
      auto start = rfc3339(localMidnight(date, L"Asia/Seoul") + 9 * 3600).substr(0, 10);
      auto end = rfc3339(localMidnight(std::chrono::year_month_day{std::chrono::sys_days{date} + std::chrono::days{1}}, L"Asia/Seoul") + 9 * 3600).substr(0, 10);
      response["items"].push_back({{"id", next ? "mock-second" : "mock-first"},
          {"summary", next ? "목업 다음 페이지" : "목업 API 일정"},
          {"start", {{"date", start}}}, {"end", {{"date", end}}}});
      if (mode == "pages" && !next) response["nextPageToken"] = "next";
    }
    return {200, 0, response.dump(), 0};
  };
}
inline auto mockBrowser(const std::filesystem::path &root) {
  return [root](const std::string &url) {
    // Only the app's own loopback listener receives this synthetic authorization response.
    auto field = [&](std::string_view name) {
      auto start = url.find(std::string(name) + "=");
      if (start == std::string::npos) throw std::runtime_error("missing OAuth field");
      start += name.size() + 1;
      return url.substr(start, url.find('&', start) - start);
    };
    auto redirect = field("redirect_uri");
    const std::string prefix = "http%3A%2F%2F127.0.0.1%3A";
    if (!redirect.starts_with(prefix) || !redirect.ends_with("%2Fcallback"))
      throw std::runtime_error("mock OAuth only accepts app loopback");
    mockApiLog(root, "oauth-browser-callback", true);
    if (mockApiOptions(root).value("authorization", std::string("normal")) == "denied")
      throw std::runtime_error("mock consent denied");
    auto port = std::stoi(redirect.substr(prefix.size()));
    if (port < 1 || port > 65535) throw std::runtime_error("invalid mock callback port");
    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct Close { SOCKET value; ~Close() { if (value != INVALID_SOCKET) closesocket(value); } } close{client};
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<u_short>(port));
    if (client == INVALID_SOCKET || connect(client, reinterpret_cast<sockaddr *>(&address), sizeof address) == SOCKET_ERROR)
      throw std::runtime_error("mock callback connection");
    auto request = "GET /callback?code=mock-code&state=" + field("state") +
                   " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    if (send(client, request.c_str(), int(request.size()), 0) != int(request.size()))
      throw std::runtime_error("mock callback send");
  };
}
} // namespace tempos
