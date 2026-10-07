#pragma once
#include "model.h"
#include <winhttp.h>
#include <condition_variable>
#include <thread>
#include <stop_token>
#include <map>
namespace tempos {
struct Response {
  DWORD status{}, error{};
  std::string body;
  int retryAfter{};
  bool ok() const { return !error && status >= 200 && status < 300; }
};
std::string urlEncode(std::string_view value);
class HttpClient {
public:
  using Transport = std::function<Response(const std::wstring &, const std::wstring &, const std::wstring &,
                                           const std::string &, const std::wstring &, std::stop_token)>;
  explicit HttpClient(Transport transport = {});
  ~HttpClient();
  Response request(const std::wstring &host, const std::wstring &path, const std::wstring &method = L"GET",
                   const std::string &body = {}, const std::wstring &headers = {}, std::stop_token stop = {});
  void cancel();

private:
  HINTERNET session_{};
  Transport transport_;
  std::mutex mutex_;
  struct ActiveRequest {
    HINTERNET handle{};
    bool closed = false;
  };
  std::vector<std::shared_ptr<ActiveRequest>> active_;
};
class WorkQueue {
public:
  explicit WorkQueue(size_t count = 2);
  ~WorkQueue();
  bool post(std::function<void(std::stop_token)> job);
  void stop();

private:
  std::mutex mutex_;
  std::condition_variable_any cv_;
  std::deque<std::function<void(std::stop_token)>> jobs_;
  std::vector<std::jthread> workers_;
  bool stopped_ = false;
};
} // namespace tempos
