using FlaUI.Core.AutomationElements;
using System.Text.Json.Nodes;
using App = FlaUI.Core.Application;

internal static partial class Program
{
    static void UiReview(ref App? app, string exe)
    {
        for (int i = 0; i < 5; i++) NativeButton(1203);
        bool Visible(int id) => IsWindowVisible(GetDlgItem(Window.Properties.NativeWindowHandle, id));
        for (int kind = 0; kind < 9; kind++)
        {
            if (kind > 0) NativeButton(1203);
            NativeCombo(1201, kind); NativeButton(1202);
            Check(SendText(GetDlgItem(Window.Properties.NativeWindowHandle, 1305), 0xC, 0, "0") != 0,
                  "Native background transparency input"); NativeButton(1316);
            Check(Visible(1310) == (kind == 0), $"Kind {kind}: seconds only for clock");
            Check(Visible(1311) == new[] {2, 3, 4, 6}.Contains(kind), $"Kind {kind}: graph only for metrics");
            Check(Visible(1312) == (kind == 1) && Visible(1314) == (kind == 1) && Visible(1320) == (kind == 1), $"Kind {kind}: weather-only controls");
            Check(Visible(1313) == new[] {3, 6, 7}.Contains(kind), $"Kind {kind}: appropriate device controls");
            Check(Visible(1315) == (kind == 0 || kind == 8), $"Kind {kind}: timezone only for clock/calendar");
            CaptureSettings("settings-kind-" + kind);
            Tab(3);
            int[] optionKinds = [0, 1, 6, 7, 8];
            for (int i = 0; i < optionKinds.Length; i++)
                Check(Visible(1600 + i) == (kind == optionKinds[i]), $"Kind {kind}: relevant display option {i}");
            Check(Visible(1610) == (kind == 5) && Visible(1622) == (kind == 5), $"Kind {kind}: system-only fields");
            Tab(0);
            var before = Settings().DeepClone();
            var handle = WidgetHandle(State()["windows"]![0]!);
            ShowWindow(Window.Properties.NativeWindowHandle, 0);
            foreach (uint message in new uint[] {0x201, 0x202, 0x203, 0x204, 0x205, 0x206, 0x7B, 0x100})
                SendMessage(handle, message, 0, (nint)((30 << 16) | 30));
            Thread.Sleep(150);
            Check(!IsWindowVisible(Window.Properties.NativeWindowHandle) && OwnedWindow("Tempos.Details") == 0,
                  $"Kind {kind}: clicks do not open settings, details, or menu");
            Check(JsonNode.DeepEquals(before, Settings()), $"Kind {kind}: clicks do not mutate widget");
            Check(!Automation.FromHandle(handle).Patterns.Invoke.IsSupported, $"Kind {kind}: display-only accessibility");
            ShowWindow(Window.Properties.NativeWindowHandle, 5);

            if (kind == 1)
            {
                var original = Settings()["widgets"]![0]!["region"]!.GetValue<string>();
                NativeButton(1320);
                Check(Element(1999).Name.Contains("적용") && Settings()["widgets"]![0]!["region"]!.GetValue<string>() == original,
                      "Find location selects a region without silently saving");
                NativeButton(1316);
                var selectedRegion = Settings()["widgets"]![0]!["region"]!.GetValue<string>();
                Check(selectedRegion != original && selectedRegion.Contains("부산"), "Applying one-time location saves closest region");
                File.WriteAllText(Path.Combine(Data, "location-scenario.json"), "{\"error\":\"위치 권한이 필요해요\"}");
                NativeButton(1320); NativeButton(1316);
                Check(Settings()["widgets"]![0]!["region"]!.GetValue<string>() == selectedRegion, "Location denial retains selected region");
                File.WriteAllText(Path.Combine(Data, "location-scenario.json"), "{\"latitude\":35.68,\"longitude\":139.69}");
                NativeButton(1320);
                Check(Element(1999).Name.Contains("국내") && Settings()["widgets"]![0]!["region"]!.GetValue<string>() == selectedRegion,
                      "Out-of-area location does not replace region");
                Restart(ref app, exe);
                Check(Settings()["widgets"]![0]!["region"]!.GetValue<string>() == selectedRegion, "One-time location survives restart without tracking");
                Tab(0);
            }
            if (kind == 6)
            {
                Check(Element(1313).AsComboBox().SelectedItem?.Name == "전체 네트워크" && Settings()["widgets"]![0]!["source"]!.GetValue<string>() == "",
                      "New network widget defaults to aggregate traffic");
            }
            SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1911, 0);
            // Keep every capture unobscured by other applications on the desktop.
            Element(1309).AsCheckBox().IsChecked = true;
            for (int size = 0; size < (kind == 8 ? 5 : 4); size++)
            {
                NativeCombo(1300, size); NativeButton(1316);
                if (kind == 7)
                    Check(Visible(1313) == (size != 3), $"Disk size {size}: volume selector only for single-drive layouts");
                VisibleWidgets($"design-kind-{kind}-size-{size}", 1, true);
            }
            if (kind == 7)
            {
                var diskHandle = WidgetHandle(State()["windows"]![0]!);
                var summary = Automation.FromHandle(diskHandle).Name;
                Check(summary.Contains("3개 드라이브") && summary.Contains("C: 시스템") && summary.Contains("E: 작업"),
                      "Disk L exposes all drive rows to accessibility");
                for (int scenario = 20; scenario <= 23; scenario++)
                {
                    SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1900 + scenario, 0);
                    VisibleWidgets("design-disk-scenario-" + scenario, 1, true);
                    if (scenario == 20)
                    {
                        var beforeRedraw = State()["windows"]![0]!["redraws"]!.GetValue<long>();
                        Thread.Sleep(12500);
                        Check(State()["windows"]![0]!["redraws"]!.GetValue<long>() > beforeRedraw,
                              "More than six disks automatically advance without a click");
                        VisibleWidgets("design-disk-next-page", 1, true);
                    }
                }
                SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1911, 0);
            }
            if (kind == 1)
            {
                NativeCombo(1300, 3); NativeButton(1316);
                WeatherScenario(new() { ["dailyCount"] = 3 }); WaitWeather("open-meteo", 1, 21);
                WaitDailyCount(2);
                Check(WeatherWindows().All(w => w["weatherDays"]!.AsArray().Count == 2),
                      "Partial HTTP daily forecast retains missing future days");
                VisibleWidgets("design-weather-daily-partial", 1, true);
                WeatherScenario(new() { ["dailyCount"] = 0 }); WaitWeather("open-meteo", 1, 21);
                WaitDailyCount(0);
                Check(WeatherWindows().All(w => w["weatherDays"]!.AsArray().Count == 0),
                      "Absent daily forecast does not invent values");
                VisibleWidgets("design-weather-daily-missing", 1, true);
                WeatherScenario(new()); WaitWeather("open-meteo", 1, 21);
                WaitDailyCount(4);
                NativeCombo(1300, 2); NativeButton(1316);
                for (int scenario = 0; scenario <= 8; scenario++)
                {
                    SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1900 + scenario, 0);
                    VisibleWidgets($"design-weather-scene-{scenario}", 1, true);
                }
                Element(1312).AsCheckBox().IsChecked = false; NativeButton(1316);
                VisibleWidgets("design-weather-fixed", 1, true);
                SendText(GetDlgItem(Window.Properties.NativeWindowHandle, 1305), 0xC, 0, "100");
                Element(1312).AsCheckBox().IsChecked = true; NativeButton(1316);
                SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1902, 0);
                VisibleWidgets("design-weather-transparent", 1);
                var transparent = State()["windows"]![0]!;
                ShowWindow(Window.Properties.NativeWindowHandle, 0); Thread.Sleep(200);
                CaptureWidget("design-weather-transparent", transparent);
                ShowWindow(Window.Properties.NativeWindowHandle, 5);
            }
        }
        Quit(app!); app = null;
    }
}
