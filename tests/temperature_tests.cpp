#include "temperature.h"
#include "storage.h"
#include "design.h"
#include <sddl.h>
using namespace tempos;
void check(bool, const char *);
static void temperaturePipeTest() {
  auto name = L"\\\\.\\pipe\\Tempos.Test.Sensors." + std::to_wstring(GetCurrentProcessId());
  HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | WRITE_DAC,
      PIPE_TYPE_BYTE | PIPE_REJECT_REMOTE_CLIENTS, 1, 4096, 256, 0, nullptr);
  check(pipe != INVALID_HANDLE_VALUE, "Create isolated native temperature test pipe");
  if (pipe == INVALID_HANDLE_VALUE) return;
  PSECURITY_DESCRIPTOR acl{};
  bool secured = ConvertStringSecurityDescriptorToSecurityDescriptorW(
      L"D:(A;;0x120183;;;WD)", SDDL_REVISION_1, &acl, nullptr) &&
      SetKernelObjectSecurity(pipe, DACL_SECURITY_INFORMATION, acl);
  if (acl) LocalFree(acl);
  check(secured, "Test pipe permits data access without creating pipe instances");
  HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  auto transfer = [&](auto operation) {
    OVERLAPPED pending{};
    pending.hEvent = event;
    ResetEvent(event);
    if (operation(pending)) return true;
    auto error = GetLastError();
    if (error == ERROR_PIPE_CONNECTED) return true;
    if (error != ERROR_IO_PENDING) return false;
    DWORD done{};
    if (WaitForSingleObject(event, 3000) != WAIT_OBJECT_0) {
      CancelIoEx(pipe, &pending);
      GetOverlappedResult(pipe, &pending, &done, TRUE);
      return false;
    }
    return GetOverlappedResult(pipe, &pending, &done, FALSE) != FALSE;
  };
  {
    CpuTemperatureClient client(name);
    client.configure(true, 1);
    bool connected = transfer([&](OVERLAPPED &p) { return ConnectNamedPipe(pipe, &p); });
    char request[257]{};
    DWORD bytes{};
    bool read = connected && transfer([&](OVERLAPPED &p) { return ReadFile(pipe, request, 256, &bytes, &p); });
    check(read && std::string(request).find("\"version\":1") != std::string::npos,
          "Native client connects to restricted service ACL and sends versioned request");
    std::string reply = "{\"version\":1,\"status\":\"ready\",\"celsius\":47.5,\"ageMs\":0}\n";
    bool sent = read && transfer([&](OVERLAPPED &p) {
      return WriteFile(pipe, reply.data(), DWORD(reply.size()), &bytes, &p);
    });
    for (int i = 0; sent && i < 100 && client.reading().status != TemperatureStatus::Ready; ++i) Sleep(10);
    check(sent && client.reading().celsius == 47.5,
          "Native background client parses a real local pipe response");
    client.configure(false, 1);
    check(client.reading().status == TemperatureStatus::Disabled,
          "Disabling CPU sensors clears the last pipe sample");
  }
  CloseHandle(event);
  CloseHandle(pipe);
}
void temperatureTests() {
  using design::Metric;
  for (auto metric : {Metric::Temperature, Metric::Load, Metric::Capacity}) {
    const double red = metric == Metric::Load ? 95 : 90;
    check(design::metricColor(metric, 79.9) == design::metricWhite &&
              design::metricColor(metric, 80) == design::metricOrange &&
              design::metricColor(metric, red - .1) == design::metricOrange &&
              design::metricColor(metric, red) == design::metricRed,
          "Metric emphasis changes at exact thresholds for each metric type");
    check(design::metricColor(metric, red, false) == design::metricWhite &&
              design::metricColor(metric, NAN) == design::metricWhite &&
              design::metricColor(metric, INFINITY) == design::metricWhite &&
              design::metricColor(metric, -1) == design::metricWhite &&
              design::metricColor(metric, 151) == design::metricWhite,
          "Stale, missing and invalid readings never retain warning colors");
  }
  check(design::metricColor(Metric::Load, 100) == design::metricRed &&
            design::metricColor(Metric::Load, 101) == design::metricWhite &&
            design::metricColor(Metric::Temperature, 105) == design::metricRed,
        "Temperature range is independent of percentage range");
  auto reading = parseCpuTemperature(R"({"version":1,"status":"ready","celsius":48.5,"ageMs":500})", 100);
  check(reading.celsius == 48.5 && reading.sampledAt == 99.5 && validTemperature(reading, 100, 2),
        "CPU temperature preserves value and sensor sample age");
  check(!validTemperature(reading, 105, 2) && validTemperature(reading, 105, 5),
        "Temperature expiry respects collection interval");
  check(!validTemperature(reading, 98, 2), "Future sensor timestamps are rejected");
  for (auto input : {
           R"({"version":2,"status":"ready","celsius":45,"ageMs":0})",
           R"({"version":1,"status":"ready","celsius":null,"ageMs":0})",
           R"({"version":1,"status":"ready","celsius":0,"ageMs":0})",
           R"({"version":1,"status":"ready","celsius":151,"ageMs":0})",
           R"({"version":1,"status":"ready","celsius":45,"ageMs":-1})",
           R"({"version":1,"status":"ready","celsius":45,"ageMs":61000})",
           R"({"version":1,"status":"unavailable","celsius":45,"ageMs":0})",
           "not json"}) {
    auto result = parseCpuTemperature(input, 100);
    check(result.status == TemperatureStatus::Unavailable && !validTemperature(result, 100, 2),
          "Malformed or unavailable sensor reply cannot become a temperature");
  }
  check(parseCpuTemperature(std::string(4097, 'x'), 100).status == TemperatureStatus::Unavailable,
        "Oversized sensor replies are rejected");
  check(parseCpuTemperature(R"({"version":1,"status":"unsupported"})", 100).status ==
            TemperatureStatus::Unsupported, "Unsupported sensors remain distinct from transport failure");
  Settings settings;
  settings.cpuTemperatureEnabled = true;
  auto saved = settingsJson(settings), exported = settingsJson(settings, true);
  check(readSettings(saved).cpuTemperatureEnabled && !exported.contains("cpuTemperatureEnabled"),
        "CPU temperature opt-in persists locally but is excluded from layout export");
  saved.erase("cpuTemperatureEnabled");
  check(!readSettings(saved).cpuTemperatureEnabled, "Existing profiles do not silently enable CPU sensors");
  CpuTemperatureClient client;
  check(client.reading().status == TemperatureStatus::Disabled,
        "Disabled CPU temperature client does not need a service");
  temperaturePipeTest();
}
