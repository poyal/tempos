#include "http.h"
#include <atomic>
namespace tempos {
std::string urlEncode(std::string_view value) {
  constexpr char h[] = "0123456789ABCDEF";
  std::string r;
  for (unsigned char c : value) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~')
      r += char(c);
    else {
      r += '%';
      r += h[c >> 4];
      r += h[c & 15];
    }
  }
  return r;
}
HttpClient::HttpClient(Transport transport) : transport_(std::move(transport)) {
  session_ = WinHttpOpen(L"Tempos/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                         WINHTTP_NO_PROXY_BYPASS, 0);
  if (session_)
    WinHttpSetTimeouts(session_, 5000, 5000, 5000, 5000);
}
HttpClient::~HttpClient() {
  cancel();
  if (session_)
    WinHttpCloseHandle(session_);
}
void HttpClient::cancel() {
  std::lock_guard g(mutex_);
  for (auto &request : active_) {
    if (!request->closed)
      WinHttpCloseHandle(request->handle);
    request->closed = true;
  }
  active_.clear();
}
Response HttpClient::request(const std::wstring &host, const std::wstring &path, const std::wstring &method,
                             const std::string &body, const std::wstring &headers, std::stop_token stop) {
  Response r;
  if (!session_ || stop.stop_requested()) {
    r.error = ERROR_CANCELLED;
    return r;
  }
  if (host != L"apihub.kma.go.kr" && host != L"oauth2.googleapis.com" && host != L"www.googleapis.com") {
    r.error = ERROR_ACCESS_DENIED;
    return r;
  }
  if (method != L"GET" && !(host == L"oauth2.googleapis.com" && path == L"/token" && method == L"POST")) {
    r.error = ERROR_ACCESS_DENIED;
    return r;
  }
  if (transport_)
    return transport_(host, path, method, body, headers, stop);
  auto connection = WinHttpConnect(session_, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!connection) {
    r.error = GetLastError();
    return r;
  }
  auto request = WinHttpOpenRequest(connection, method.c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                    WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!request) {
    r.error = GetLastError();
    WinHttpCloseHandle(connection);
    return r;
  }
  auto lifetime = std::make_shared<ActiveRequest>();
  lifetime->handle = request;
  {
    std::lock_guard g(mutex_);
    active_.push_back(lifetime);
  }
  auto close = [&] {
    std::lock_guard g(mutex_);
    auto it = std::find(active_.begin(), active_.end(), lifetime);
    if (!lifetime->closed) {
      WinHttpCloseHandle(request);
      lifetime->closed = true;
    }
    if (it != active_.end()) {
      active_.erase(it);
    }
  };
  std::stop_callback cancelled(stop, close);
  std::atomic<bool> complete = false;
  std::jthread deadline([&](std::stop_token token) {
    std::mutex m;
    std::condition_variable_any cv;
    std::unique_lock l(m);
    cv.wait_for(l, token, 15s, [&] { return complete.load(); });
    if (!token.stop_requested() && !complete.load())
      close();
  });
  DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof redirect);
  DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
  WinHttpSetOption(request, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof decompression);
  if (!WinHttpSendRequest(request, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                          DWORD(headers.size()),
                          body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char *>(body.data()),
                          DWORD(body.size()), DWORD(body.size()), 0) ||
      !WinHttpReceiveResponse(request, nullptr))
    r.error = GetLastError();
  else {
    DWORD n = sizeof r.status;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &r.status, &n, WINHTTP_NO_HEADER_INDEX);
    wchar_t retry[64]{};
    n = sizeof retry;
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_RETRY_AFTER, WINHTTP_HEADER_NAME_BY_INDEX, retry, &n,
                            WINHTTP_NO_HEADER_INDEX))
      r.retryAfter = std::clamp(_wtoi(retry), 0, 86400);
    char buffer[8192];
    while (!stop.stop_requested()) {
      DWORD bytes = 0;
      if (!WinHttpReadData(request, buffer, sizeof buffer, &bytes)) {
        r.error = GetLastError();
        break;
      }
      if (!bytes)
        break;
      if (r.body.size() + bytes > 1024 * 1024) {
        r.error = ERROR_BUFFER_OVERFLOW;
        r.body.clear();
        break;
      }
      r.body.append(buffer, bytes);
    }
  }
  complete = true;
  deadline.request_stop();
  close();
  WinHttpCloseHandle(connection);
  if (stop.stop_requested())
    r.error = ERROR_CANCELLED;
  return r;
}
WorkQueue::WorkQueue(size_t count) {
  for (size_t i = 0; i < count; ++i)
    workers_.emplace_back([this](std::stop_token token) {
      CoInitializeEx(nullptr, COINIT_MULTITHREADED);
      while (!token.stop_requested()) {
        std::function<void(std::stop_token)> job;
        {
          std::unique_lock lock(mutex_);
          cv_.wait(lock, token, [&] { return stopped_ || !jobs_.empty(); });
          if (stopped_ || token.stop_requested())
            break;
          job = std::move(jobs_.front());
          jobs_.pop_front();
        }
        try {
          job(token);
        } catch (...) {
        }
      }
      CoUninitialize();
    });
}
WorkQueue::~WorkQueue() {
  stop();
}
void WorkQueue::stop() {
  {
    std::lock_guard g(mutex_);
    stopped_ = true;
    jobs_.clear();
  }
  for (auto &w : workers_)
    w.request_stop();
  cv_.notify_all();
  workers_.clear();
}
bool WorkQueue::post(std::function<void(std::stop_token)> job) {
  std::lock_guard g(mutex_);
  if (stopped_ || jobs_.size() >= 128)
    return false;
  jobs_.push_back(std::move(job));
  cv_.notify_one();
  return true;
}
} // namespace tempos
