using FlaUI.Core.AutomationElements;
using Microsoft.Win32;
using System.Text.Json.Nodes;
using App = FlaUI.Core.Application;

internal static partial class Program
{
    static JsonNode[] WeatherWindows() => State()["windows"]!.AsArray().Where(x => x!["kind"]!.GetValue<int>() == 1).Select(x => x!).ToArray();
    static void WaitWeather(string provider, int status = 1, double? temperature = null)
    {
        var deadline = DateTime.UtcNow.AddSeconds(15);
        while (DateTime.UtcNow < deadline)
        {
            var windows = WeatherWindows();
            if (windows.Length > 0 && windows.All(w => w["weatherProvider"]!.GetValue<string>() == provider &&
                w["weatherStatus"]!.GetValue<int>() == status &&
                (!temperature.HasValue || w["weatherTemperature"]?.GetValue<double>() == temperature)))
            { Check(true, $"All weather widgets: {provider}, status {status}, temperature {temperature}"); return; }
            Thread.Sleep(100);
        }
        throw new Exception($"Weather did not reach {provider}/{status}/{temperature}");
    }
    static void WeatherScenario(JsonObject value)
    {
        File.WriteAllText(Path.Combine(Data, "weather-scenario.json"), value.ToJsonString());
        SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1913, 0);
    }
    static void SelectWeather(int index)
    {
        Tab(1); NativeCombo(1403, index); NativeButton(1404);
    }
    static void WeatherOptions(ref App? app, string exe)
    {
        NativeButton(1316);
        Check(Settings()["weatherProvider"]!.GetValue<string>() == "open-meteo", "New profile uses Open-Meteo");
        WaitWeather("open-meteo", 1, 21);
        Tab(1);
        Check(!Element(1400).IsEnabled && Element(1405).Name.Contains("비상업적"), "Open-Meteo needs no key and explains free-use condition");
        Screenshot("weather-connections-open-meteo", Window.BoundingRectangle);
        SelectWeather(0); WaitWeather("kma", 1, 18);
        Check(Element(1400).IsEnabled, "KMA credential input enabled");
        Element(1400).AsTextBox().Text = "e2e-isolated-key"; NativeButton(1401);
        var secretPath = Path.Combine(Data, "weather-key.bin");
        Check(File.Exists(secretPath), "KMA key stored as protected file");
        var secret = File.ReadAllBytes(secretPath);
        Screenshot("weather-connections-kma", Window.BoundingRectangle);
        SelectWeather(1); WaitWeather("open-meteo", 1, 21);
        Check(File.ReadAllBytes(secretPath).SequenceEqual(secret), "Switching provider preserves encrypted KMA credential");
        Tab(0); NativeCombo(1201, 1); NativeButton(1202); WaitWeather("open-meteo", 1, 21);
        Check(WeatherWindows().Length == 2 && State()["subscriptions"]!.GetValue<int>() == 4, "Two weather widgets share one subscription");
        var before = File.ReadAllLines(Path.Combine(Data, "weather-requests.jsonl")).Length;
        NativeCombo(1302, 4); NativeButton(1316); Thread.Sleep(500);
        Check(File.ReadAllLines(Path.Combine(Data, "weather-requests.jsonl")).Length == before, "Style-only changes do not request weather");
        SelectWeather(0); WaitWeather("kma", 1, 18);
        var saved = Settings().DeepClone();
        Restart(ref app, exe); WaitWeather("kma", 1, 18);
        Check(JsonNode.DeepEquals(saved, Settings()), "Global provider and both weather widgets survive normal restart");
        SelectWeather(1); WaitWeather("open-meteo", 1, 21);
        saved = Settings().DeepClone();
        int oldPid = app!.ProcessId;
        using (var process = System.Diagnostics.Process.GetProcessById(oldPid)) process.Kill();
        Check(WaitExit(app, 10000), "Saved-profile process really terminated");
        app.Dispose(); app = Start(exe);
        Check(app.ProcessId != oldPid && JsonNode.DeepEquals(saved, Settings()), "Committed settings survive forced process termination");
        WaitWeather("open-meteo", 1, 21);
        Tab(2); FileDialog(1503, Path.Combine(Data, "weather-layout.json"));
        Check(JsonNode.Parse(File.ReadAllText(Path.Combine(Data, "weather-layout.json")))!["weatherProvider"]!.GetValue<string>() == "open-meteo", "Layout export includes provider");
        SelectWeather(0); Tab(2); FileDialog(1504, Path.Combine(Data, "weather-layout.json"));
        WaitWeather("open-meteo", 1, 21);
        SelectWeather(0);
        using (var locked = new FileStream(Path.Combine(Data, "settings.json"), FileMode.Open, FileAccess.Read, FileShare.Read))
        {
            NativeCombo(1403, 1); NativeButton(1404);
            Check(Settings()["weatherProvider"]!.GetValue<string>() == "kma" && Element(1403).AsComboBox().SelectedItem?.Name == "기상청",
                  "Save failure preserves provider and restores selector");
        }
        WaitWeather("kma", 1, 18);
        SelectWeather(1); WaitWeather("open-meteo", 1, 21);
        WeatherScenario(new() { ["mode"] = "offline" }); WaitWeather("open-meteo", 2, 21);
        WeatherScenario(new() { ["mode"] = "quota" }); WaitWeather("open-meteo", 2, 21);
        WeatherScenario(new() { ["mode"] = "malformed" }); WaitWeather("open-meteo", 2, 21);
        foreach (int code in new[] {45, 95, 71, 999})
        {
            WeatherScenario(new() { ["code"] = code }); WaitWeather("open-meteo", 1, 21);
            Check(WeatherWindows().All(w => w["weatherState"]!.GetValue<int>() == (code == 45 ? 9 : code == 95 ? 10 : code == 71 ? 6 : 0)), "WMO state reaches widget: " + code);
            Tab(0); SelectWidget(1); NativeButton(1316);
            var view = WeatherWindows().First();
            ShowWindow(Window.Properties.NativeWindowHandle, 0); Thread.Sleep(200);
            Screenshot("weather-code-" + code, new Rectangle(view["x"]!.GetValue<int>(), view["y"]!.GetValue<int>(), view["width"]!.GetValue<int>(), view["height"]!.GetValue<int>()));
            ShowWindow(Window.Properties.NativeWindowHandle, 5);
        }
        var calls = File.ReadAllLines(Path.Combine(Data, "weather-requests.jsonl")).Length;
        WeatherScenario(new() { ["delayMs"] = 1800, ["temperature"] = 33 });
        for (int i = 0; i < 100 && File.ReadAllLines(Path.Combine(Data, "weather-requests.jsonl")).Length == calls; i++) Thread.Sleep(20);
        Check(File.ReadAllLines(Path.Combine(Data, "weather-requests.jsonl")).Length > calls, "Delayed old-provider request started");
        File.WriteAllText(Path.Combine(Data, "weather-scenario.json"), "{}");
        SelectWeather(0); WaitWeather("kma", 1, 18); Thread.Sleep(2000); WaitWeather("kma", 1, 18);
        SelectWeather(1);
        WeatherScenario(new()); WaitWeather("open-meteo", 1, 21);
        Tab(0); SelectWidget(5); NativeButton(1203); SelectWidget(1);
        Element(1305).AsTextBox().Text = "0";
        foreach (int size in new[] {0, 1, 2, 3})
        {
            NativeCombo(1300, size); NativeButton(1316); Thread.Sleep(200);
            var item = WeatherWindows().First();
            ShowWindow(Window.Properties.NativeWindowHandle, 0); Thread.Sleep(200);
            Screenshot("weather-size-" + size, new Rectangle(item["x"]!.GetValue<int>(), item["y"]!.GetValue<int>(), item["width"]!.GetValue<int>(), item["height"]!.GetValue<int>()));
            ShowWindow(Window.Properties.NativeWindowHandle, 5);
            Check(Automation.FromHandle(WidgetHandle(item)).Name.Contains("Open-Meteo"), "Weather accessibility attribution at size " + size);
        }
        Quit(app!); app = null;
        var old = Settings(); old.Remove("weatherProvider");
        File.WriteAllText(Path.Combine(Data, "settings.json"), old.ToJsonString());
        app = Start(exe); WaitWeather("kma", 1, 18);
        Tab(1); Check(Element(1403).AsComboBox().SelectedItem?.Name == "기상청", "Legacy settings without provider retain KMA");
        Quit(app); app = null;
    }
    static void StartupOptions(ref App? app, string exe)
    {
        NativeButton(1316);
        var snapshot = Settings().DeepClone();
        var registryPath = State()["startupRegistryKey"]!.GetValue<string>();
        Check(registryPath.StartsWith(@"Software\Tempos\Tests\"), "Startup test uses isolated registry path");
        try
        {
            Tab(2); Element(1502).AsCheckBox().IsChecked = true;
            using var registry = Registry.CurrentUser.OpenSubKey(registryPath);
            var command = (string?)registry?.GetValue("Tempos");
            Check(command != null && command.StartsWith('"' + Path.GetFullPath(exe) + '"') && command.Contains("--data-dir") && command.Contains(Data),
                  "Startup command preserves executable and isolated Unicode/spaced data path");
            Restart(ref app, exe); Tab(2);
            Check(Element(1502).AsCheckBox().IsChecked == true, "Startup checkbox survives complete restart");
            Quit(app!); app = null;
            string arguments = command![(command!.IndexOf('"', 1) + 1)..].Trim();
            app = Start(exe, arguments);
            Check(JsonNode.DeepEquals(snapshot, Settings()), "Registered command reopens same profile without --settings");
            Tab(2); Element(1502).AsCheckBox().IsChecked = false;
            Restart(ref app, exe); Tab(2);
            using var after = Registry.CurrentUser.OpenSubKey(registryPath);
            Check(Element(1502).AsCheckBox().IsChecked == false && after?.GetValue("Tempos") == null, "Disabled startup remains disabled after restart");
            SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1914, 0);
            Element(1502).AsCheckBox().IsChecked = true;
            Check(Element(1502).AsCheckBox().IsChecked == false && Element(1999).Name.Contains("실패"),
                  "Startup registration failure restores checkbox and displays error");
            SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1914, 0);
            Check(JsonNode.DeepEquals(snapshot, Settings()), "Startup toggle does not change profile");
            Quit(app!); app = null;
        }
        finally { Registry.CurrentUser.DeleteSubKeyTree(registryPath, false); }
        Skip("Actual Windows logon", "Registered command tested; current user session was not logged out.");
    }
}
