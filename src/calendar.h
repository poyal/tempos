#pragma once
#include "http.h"
#include "storage.h"
namespace tempos {
struct CalendarAccount {
  std::wstring status;
  bool connected = false;
  std::vector<Device> calendars;
  std::vector<std::wstring> selected;
};
Event parseEvent(const Json &, const std::wstring &calendar, uint32_t color);
std::string pkceChallenge(const std::string &verifier);
class CalendarService {
public:
  CalendarService(Store &, HttpClient &, bool fixture = false);
  Snapshot read(const Widget &, std::stop_token);
  void connect(std::stop_token);
  void disconnect();
  void select(const std::vector<std::wstring> &);
  CalendarAccount account() const;

private:
  std::string token(std::stop_token);
  Response api(const std::wstring &, std::stop_token);
  void saveAccount();
  Store &store_;
  HttpClient &http_;
  bool fixture_;
  mutable std::mutex mutex_;
  std::mutex requestMutex_;
  CalendarAccount account_;
  std::string access_;
  int64_t expiry_{};
  uint64_t generation_{};
  int64_t retryAt_{};
};
} // namespace tempos
