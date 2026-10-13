using FlaUI.Core.AutomationElements;
using Microsoft.Win32;
using System.Text.Json.Nodes;
using App = FlaUI.Core.Application;

internal static partial class Program
{
    // Document real controls using an isolated profile, sample collectors and mock APIs.
    // Publish only these client/widget crops, never the runner's desktop captures.
    static void ReadmeScreenshots(ref App? app, string exe)
    {
        Check(!Live && !LiveApis, "README captures use sample collectors and mock APIs");
        // Leave room below the native status line at 150% DPI. Resize the real
        // window, rather than padding/correcting pixels in published screenshots.
        if (!GetWindowRect(Window.Properties.NativeWindowHandle, out var initialBounds))
            throw new Exception("README settings bounds unavailable");
        SetWindowPos(Window.Properties.NativeWindowHandle, 0, 0, 0,
            initialBounds.Width - initialBounds.X, initialBounds.Height - initialBounds.Y + 60, 0x16);
        Thread.Sleep(200);
        CaptureSettings("usage-first-launch");
        for (int i = 0; i < 5; i++) NativeButton(1203);
        NativeCombo(1201, 0); NativeButton(1202);
        SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1911, 0);
        NativeCombo(1300, 2); Element(1305).AsTextBox().Text = "0";
        Element(1309).AsCheckBox().IsChecked = true; NativeButton(1316);
        Check(Settings()["widgets"]!.AsArray().Count == 1, "README add and apply a clock");
        CaptureSettings("usage-add-apply");
        NativeButton(1204); CaptureSettings("usage-arrange");
        VisibleWidgets("design-usage-arrange-widget", 1, true);
        NativeButton(1204);
        Tab(3); Element(1600).AsCheckBox().IsChecked = true; NativeButton(1620);
        Check(Settings()["widgets"]![0]!["twelveHour"]!.GetValue<bool>(), "README display option saved");
        CaptureSettings("usage-clock-options");
        Tab(0); NativeButton(1203); NativeCombo(1201, 5); NativeButton(1202);
        Tab(3); CaptureSettings("usage-system-options");
        Tab(0); NativeButton(1203); NativeCombo(1201, 6); NativeButton(1202);
        CaptureSettings("usage-network");
        NativeButton(1203); NativeCombo(1201, 1); NativeButton(1202);
        CaptureSettings("usage-weather-region");
        NativeButton(1320); NativeButton(1316);
        Check(Settings()["widgets"]![0]!["region"]!.GetValue<string>().Contains("부산"), "README one-time mock location applied");
        CaptureSettings("usage-location-applied");
        Tab(1); CaptureSettings("usage-weather-open-meteo");
        NativeCombo(1403, 0); NativeButton(1404);
        Check(Settings()["weatherProvider"]!.GetValue<string>() == "kma", "README KMA provider selected without a private key");
        CaptureSettings("usage-weather-kma");
        NativeCombo(1403, 1); NativeButton(1404);
        Tab(0); NativeButton(1203); NativeCombo(1201, 8); NativeButton(1202);
        NativeCombo(1300, 3); NativeButton(1316); CaptureSettings("usage-calendar-month");
        Tab(1); Element(1410).AsTextBox().Text = "mock-client.apps.googleusercontent.com";
        NativeButton(1412);
        var deadline = DateTime.UtcNow.AddSeconds(12);
        while ((!Element(1412).IsEnabled || !Element(1416).Name.Contains("연결됨")) && DateTime.UtcNow < deadline) Thread.Sleep(100);
        Check(Element(1416).Name.Contains("연결됨") && Element(1414).AsListBox().Items.Length > 0,
              "README mock OAuth populates calendar selection");
        SendMessage(Element(1414).Properties.NativeWindowHandle, 0x185, 1, 0);
        NativeButton(1415); CaptureSettings("usage-calendar-connected");
        // Keep the actual user's Windows Run registration untouched.
        var registryPath = State()["startupRegistryKey"]!.GetValue<string>();
        Check(registryPath.StartsWith(@"Software\Tempos\Tests\"), "README startup uses isolated registry");
        try
        {
            Tab(2); Element(1502).AsCheckBox().IsChecked = true;
            CaptureSettings("usage-app-startup");
            Element(1502).AsCheckBox().IsChecked = false;
            var layout = Path.Combine(Data, "tempos-layout.json");
            FileDialog(1503, layout);
            Check(File.Exists(layout), "README native layout export succeeds");
            CaptureSettings("usage-backup-exported");
            Tab(0); NativeButton(1203);
            Check(Settings()["widgets"]!.AsArray().Count == 0, "README remove before restore");
            Tab(2); FileDialog(1504, layout);
            Check(Settings()["widgets"]!.AsArray().Count == 1, "README layout restore recovers calendar");
            CaptureSettings("usage-backup-restored");
            NativeButton(1505); CaptureSettings("usage-diagnostics");
            var saved = Settings().DeepClone(); Restart(ref app, exe);
            Check(JsonNode.DeepEquals(saved, Settings()), "README restored layout survives restart");
            Tab(1); NativeButton(1413);
            Quit(app!); app = null;
        }
        finally { Registry.CurrentUser.DeleteSubKeyTree(registryPath, false); }
    }
}
