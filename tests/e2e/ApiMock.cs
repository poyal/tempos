using FlaUI.Core.AutomationElements;
using System.Text.Json.Nodes;
using App = FlaUI.Core.Application;

internal static partial class Program
{
    static void ApiMock(ref App? app, string exe)
    {
        Check(!LiveApis && !State()["fixture"]!.GetValue<bool>(), "Mock API E2E retains real Windows system collectors");
        WaitWeather("open-meteo", 1, 21);
        NativeCombo(1201, 8); NativeButton(1202); NativeCombo(1300, 2); NativeButton(1316);
        void Scenario(JsonObject scenario) => File.WriteAllText(Path.Combine(Data, "api-scenario.json"), scenario.ToJsonString());
        void Connect()
        {
            Tab(1); Element(1410).AsTextBox().Text = "mock-client.apps.googleusercontent.com";
            NativeButton(1412);
            var deadline = DateTime.UtcNow.AddSeconds(10);
            while (!Element(1412).IsEnabled && DateTime.UtcNow < deadline) Thread.Sleep(50);
            Check(Element(1412).IsEnabled, "Mock authorization completes without an external browser");
        }
        void Calendar(int status, int count)
        {
            var deadline = DateTime.UtcNow.AddSeconds(15);
            while (DateTime.UtcNow < deadline)
            {
                var view = State()["windows"]!.AsArray().Single(w => w!["kind"]!.GetValue<int>() == 8)!;
                if (view["calendarStatus"]!.GetValue<int>() == status && view["calendarEventCount"]!.GetValue<int>() == count)
                { Check(true, $"Mock calendar reaches UI: status {status}, events {count}"); return; }
                Thread.Sleep(100);
            }
            throw new Exception($"Calendar did not reach {status}/{count}");
        }
        Scenario(new() { ["authorization"] = "denied" }); Connect();
        Check(Element(1416).Name.Contains("실패"), "Denied mock authorization gives a connection error");
        Check(!File.Exists(Path.Combine(Data, "calendar-refresh.bin")), "Denied authorization saves no refresh token");
        Scenario(new() { ["calendar"] = "pages" }); Connect();
        Check(Element(1416).Name.Contains("연결됨") && Element(1414).AsListBox().Items.Length == 1,
              "Mock OAuth exchange and calendar-list parser populate the connection UI");
        Calendar(1, 2);
        // Applying the selected widget presents the newly fetched data immediately;
        // normal calendar presentation otherwise follows its refresh interval.
        Tab(0); SelectWidget(5); NativeButton(1316);
        var calendarView = State()["windows"]!.AsArray().Single(w => w!["kind"]!.GetValue<int>() == 8)!;
        Check(Automation.FromHandle(WidgetHandle(calendarView)).Name.Contains("목업 API 일정"), "Parsed mock event reaches widget accessibility output");
        foreach (string mode in new[] { "timeout", "unauthorized", "forbidden", "quota", "server-error", "malformed" })
        {
            Scenario(new() { ["calendar"] = mode }); Restart(ref app, exe); Calendar(2, 2);
            Check(true, "Mock calendar preserves cached events after " + mode);
        }
        Scenario(new() { ["calendar"] = "empty" }); Restart(ref app, exe); Calendar(1, 0);
        Scenario(new() { ["token"] = "unauthorized" }); Restart(ref app, exe);
        Tab(1);
        var tokenDeadline = DateTime.UtcNow.AddSeconds(10);
        while (File.Exists(Path.Combine(Data, "calendar-refresh.bin")) && DateTime.UtcNow < tokenDeadline) Thread.Sleep(100);
        Check(!File.Exists(Path.Combine(Data, "calendar-refresh.bin")), "Mock expired refresh token is removed");
        Scenario(new()); Connect(); Calendar(1, 1);
        NativeButton(1413); Calendar(1, 0);
        Check(!File.Exists(Path.Combine(Data, "calendar-refresh.bin")) && !File.Exists(Path.Combine(Data, "calendar-events.bin")),
              "Disconnect clears mock account credentials and cached events");
        var calls = File.ReadAllLines(Path.Combine(Data, "api-mock-requests.jsonl")).Select(line => JsonNode.Parse(line)!).ToArray();
        Check(calls.All(c => c["transport"]!.GetValue<string>() == "mock" && c["matched"]!.GetValue<bool>()), "Every API request matched the mock contract");
        Check(calls.Any(c => c["endpoint"]!.GetValue<string>() == "oauth-exchange") &&
              calls.Any(c => c["endpoint"]!.GetValue<string>() == "oauth-refresh"), "Both OAuth exchange and refresh use mock APIs");
        Check(!File.ReadAllText(Path.Combine(Data, "settings.json")).Contains("mock-refresh"), "Mock credentials do not leak into settings JSON");
        Quit(app!); app = null;
    }
}
