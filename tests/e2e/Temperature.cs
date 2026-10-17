using System.Text.Json.Nodes;
using FlaUI.Core.AutomationElements;
using App = FlaUI.Core.Application;

internal static partial class Program
{
    static void TemperatureLayout(ref App? app, string exe)
    {
        JsonNode Cpu() => State()["windows"]!.AsArray().First(w => w!["kind"]!.GetValue<int>() == 2)!;
        void WaitTemperature(int expected, double? value)
        {
            var deadline = DateTime.UtcNow.AddSeconds(12);
            while (DateTime.UtcNow < deadline)
            {
                var data = Cpu();
                if (data["temperatureStatus"]!.GetValue<int>() == expected &&
                    (value.HasValue ? data["temperatureC"]?.GetValue<double>() == value : data["temperatureC"] is null))
                { Check(true, $"Sensor boundary reaches UI state {expected}, value {value}"); return; }
                Thread.Sleep(200);
            }
            throw new Exception("CPU temperature did not reach expected state " + expected);
        }
        NativeButton(1316); // Materialize the default profile before reading settings.json.
        Tab(2);
        Check(!Settings()["cpuTemperatureEnabled"]!.GetValue<bool>(), "CPU sensor opt-in defaults off");
        WaitTemperature(0, null);
        foreach (int exitCode in new[] {1223, 3010, 1})
        {
            File.WriteAllText(Path.Combine(Data, "sensor-setup-scenario.json"), new JsonObject {["exitCode"] = exitCode}.ToJsonString());
            NativeButton(1512);
            Check(!Settings()["cpuTemperatureEnabled"]!.GetValue<bool>(), "Setup cancel/reboot/failure never enables sensors: " + exitCode);
        }
        File.WriteAllText(Path.Combine(Data, "sensor-setup-scenario.json"), """{"exitCode":0}""");
        NativeButton(1512);
        Check(Settings()["cpuTemperatureEnabled"]!.GetValue<bool>(), "Successful mock setup enables sensors");
        WaitTemperature(2, 45);
        Restart(ref app, exe);
        Check(Settings()["cpuTemperatureEnabled"]!.GetValue<bool>(), "Temperature opt-in survives restart");
        WaitTemperature(2, 45);
        foreach (var scenario in new[] {
            ("""{"version":1,"status":"unsupported"}""", 3),
            ("""{"version":1,"status":"ready","celsius":47,"ageMs":20000}""", 5),
            ("malformed sensor response", 4)})
        {
            File.WriteAllText(Path.Combine(Data, "sensor-scenario.json"), scenario.Item1);
            WaitTemperature(scenario.Item2, null);
            Check(Automation.FromHandle(WidgetHandle(Cpu())).Name.Contains('%'), "Temperature failure preserves usage display");
        }
        File.WriteAllText(Path.Combine(Data, "sensor-scenario.json"), """{"version":1,"status":"ready","celsius":47,"ageMs":0}""");
        WaitTemperature(2, 47);
        Tab(2);
        var layout = Path.Combine(Data, "thermal-layout.json");
        FileDialog(1503, layout);
        Check(!JsonNode.Parse(File.ReadAllText(layout))!.AsObject().ContainsKey("cpuTemperatureEnabled"),
            "Layout export omits machine sensor opt-in");
        FileDialog(1504, layout);
        Check(Settings()["cpuTemperatureEnabled"]!.GetValue<bool>(), "Layout import preserves local sensor opt-in");
        NativeButton(1513);
        Check(!Settings()["cpuTemperatureEnabled"]!.GetValue<bool>(), "Mock removal disables CPU temperature");
        WaitTemperature(0, null);
        CaptureSettings("temperature-settings");

        Tab(0);
        for (int i = 0; i < 5; i++) NativeButton(1203);
        foreach (int kind in new[] {1, 2, 3, 4})
        {
            if (Settings()["widgets"]!.AsArray().Count != 0) NativeButton(1203);
            NativeCombo(1201, kind); NativeButton(1202);
            Element(1305).AsTextBox().Text = "0";
            Element(1309).AsCheckBox().IsChecked = true;
            SendMessage(Window.Properties.NativeWindowHandle, 0x111, kind is 2 or 3 ? 1931 : 1911, 0);
            foreach (int size in new[] {0, 1, 2, 3})
            {
                NativeCombo(1300, size);
                for (int scale = 0; scale < 3; scale++)
                {
                    NativeCombo(1301, scale); NativeButton(1316);
                    VisibleWidgets($"layout-example-{kind}-{size}-{scale}", 1, true);
                }
            }
            NativeCombo(1301, 0); NativeCombo(1300, 2); NativeButton(1316);
            if (kind is 2 or 3)
            {
                foreach (int scenario in new[] {30, 32})
                {
                    SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1900 + scenario, 0);
                    VisibleWidgets($"layout-example-{kind}-temperature-{scenario}", 1, true);
                }
                SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1931, 0);
            }
            if (kind != 1)
            {
                Element(1311).AsCheckBox().IsChecked = false; NativeButton(1316);
                VisibleWidgets($"layout-example-{kind}-no-graph", 1, true);
            }
            else
            {
                Tab(3); Element(1601).AsCheckBox().IsChecked = true; NativeButton(1620); Tab(0);
                SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1903, 0);
                VisibleWidgets("layout-example-weather-fahrenheit-negative", 1, true);
                SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1907, 0);
                VisibleWidgets("layout-example-weather-stale", 1, true);
            }
        }
        foreach (int kind in new[] {2, 3, 4, 7})
        {
            NativeButton(1203); NativeCombo(1201, kind); NativeButton(1202);
            NativeCombo(1300, 2); NativeCombo(1301, 0);
            Element(1305).AsTextBox().Text = "0";
            Element(1309).AsCheckBox().IsChecked = true;
            NativeButton(1316);
            foreach (int scenario in new[] {35, 33, 34, 36})
            {
                SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1900 + scenario, 0);
                string name = $"layout-example-color-{kind}-{scenario}";
                VisibleWidgets(name, 1, true);
                using var bitmap = new Bitmap(Path.Combine(Data, name + ".png"));
                var expected = scenario == 33 ? Color.FromArgb(255, 207, 138) :
                               scenario == 34 ? Color.FromArgb(255, 160, 160) : Color.White;
                void ColorRegion(RectangleF dip, string label, bool translucentBar = false)
                {
                    float scale = bitmap.Width / 336f;
                    var target = expected;
                    if (translucentBar)
                    {
                        // The bar is 90% opaque over its existing track. Sample an unfilled
                        // part of that track, rather than expecting an opaque text color.
                        var track = bitmap.GetPixel((int)(314 * scale), (int)(98.5f * scale));
                        target = Color.FromArgb((int)Math.Round(expected.R * .9 + track.R * .1),
                                                (int)Math.Round(expected.G * .9 + track.G * .1),
                                                (int)Math.Round(expected.B * .9 + track.B * .1));
                    }
                    int matches = 0;
                    for (int y = (int)(dip.Top * scale); y < (int)(dip.Bottom * scale); y++)
                        for (int x = (int)(dip.Left * scale); x < (int)(dip.Right * scale); x++)
                        {
                            var pixel = bitmap.GetPixel(x, y);
                            if (Math.Abs(pixel.R - target.R) < 12 && Math.Abs(pixel.G - target.G) < 12 &&
                                Math.Abs(pixel.B - target.B) < 12) matches++;
                        }
                    Check(matches >= 3, name + ": actual " + label + " pixels match " + expected.Name);
                }
                ColorRegion(kind == 7 ? new(204, 117, 112, 23) :
                            kind == 4 ? new(20, 49, 146, 55) : new(20, 73, 106, 55), "usage");
                if (kind is 2 or 3) ColorRegion(new(128, 103, 46, 21), "temperature / unavailable dash");
                if (kind == 7) ColorRegion(new(204, 96, 112, 5), "capacity bar", true);
            }
        }
        Quit(app!); app = null;
    }
}
