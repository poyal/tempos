using FlaUI.Core.AutomationElements;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;
using App = FlaUI.Core.Application;

internal static partial class Program
{
    [StructLayout(LayoutKind.Explicit, Size = 40)]
    struct DesktopMouseInput
    {
        [FieldOffset(0)] public uint Type;
        [FieldOffset(8)] public int X;
        [FieldOffset(12)] public int Y;
        [FieldOffset(16)] public uint Data;
        [FieldOffset(20)] public uint Flags;
        [FieldOffset(24)] public uint Time;
        [FieldOffset(32)] public nuint Extra;
    }
    [DllImport("user32.dll", SetLastError = true)] static extern uint SendInput(uint count, ref DesktopMouseInput input, int size);
    [DllImport("user32.dll", SetLastError = true)] static extern bool GetCursorPos(out Point point);
    [DllImport("user32.dll", SetLastError = true)] static extern nint OpenInputDesktop(uint flags, bool inherit, uint access);
    [DllImport("user32.dll")] static extern bool CloseDesktop(nint desktop);

    static bool DesktopMouse(Point point, uint flags)
    {
        var screen = SystemInformation.VirtualScreen;
        var input = new DesktopMouseInput { X = (int)((point.X - screen.X) * 65535L / (screen.Width - 1)),
            Y = (int)((point.Y - screen.Y) * 65535L / (screen.Height - 1)), Flags = flags | 0xC001 };
        return SendInput(1, ref input, Marshal.SizeOf<DesktopMouseInput>()) == 1;
    }

    static void WindowsReal(ref App? app, string exe, bool locationAllowed)
    {
        Check(Path.GetFileName(exe).Equals("Tempos.exe", StringComparison.OrdinalIgnoreCase), "Real Windows suite uses production Tempos.exe");
        Check(!State()["fixture"]!.GetValue<bool>(), "Production providers are active without fixture data");
        var inputDesktop = OpenInputDesktop(0, false, 1);
        var environment = new { os = Environment.OSVersion.VersionString, architecture = RuntimeInformation.OSArchitecture.ToString(),
            runnerSession = Process.GetCurrentProcess().SessionId, appSession = Process.GetProcessById(app!.ProcessId).SessionId,
            interactive = Environment.UserInteractive, inputDesktopAccessible = inputDesktop != 0,
            monitorDpi = State()["monitors"]!.AsArray().Select(m => m!["dpi"]!.GetValue<int>()).ToArray() };
        if (inputDesktop != 0) CloseDesktop(inputDesktop);
        File.WriteAllText(Path.Combine(Data, "windows-environment.json"), JsonSerializer.Serialize(environment, new JsonSerializerOptions { WriteIndented = true }));
        Check(environment.runnerSession == environment.appSession && environment.interactive, "App runs in the interactive Windows user session");

        for (int kind = 5; kind < 9; kind++) { NativeCombo(1201, kind); NativeButton(1202); }
        for (int kind = 0; kind < 9; kind++)
        {
            SelectWidget(kind); NativeCombo(1300, 0); NativeCombo(1301, 0);
            Element(1309).AsCheckBox().IsChecked = true; Element(1305).AsTextBox().Text = "0"; NativeButton(1316);
        }
        Thread.Sleep(4000);
        string Summary(int kind) => Automation.FromHandle(WidgetHandle(State()["windows"]![kind]!)).Name;
        foreach (int kind in new[] {2, 3, 4})
        {
            string summary = Summary(kind);
            var match = Regex.Match(summary, @"(?<value>\d+(?:\.\d+)?)%");
            Check(match.Success && double.Parse(match.Groups["value"].Value, System.Globalization.CultureInfo.InvariantCulture) is >= 0 and <= 100,
                "Actual system counter returns a valid percentage: kind " + kind);
        }
        SelectWidget(6); NativeCombo(1300, 3); NativeButton(1316); Thread.Sleep(3000);
        Check(Settings()["widgets"]![6]!["source"]!.GetValue<string>() == "", "Actual network uses aggregate interface counters");
        Check(Regex.IsMatch(Summary(6), @"\d+(?:\.\d+)? (?:Kbps|Mbps|Gbps|bps)"), "Actual aggregate receive/send rates are available");
        SelectWidget(7); NativeCombo(1300, 3); NativeButton(1316); Thread.Sleep(1500);
        string disks = Summary(7);
        var localDrives = DriveInfo.GetDrives().Where(d => d.DriveType is DriveType.Fixed or DriveType.Removable && d.IsReady).ToArray();
        Check(localDrives.Length > 0 && localDrives.All(d => disks.Contains(d.Name, StringComparison.OrdinalIgnoreCase)),
            "Large disk widget includes every ready local drive reported by Windows");
        Check(Regex.IsMatch(disks, @"\d+(?:\.\d+)? (?:GB|TB|GiB|TiB)"), "Actual disk capacity is available");
        Check(!IsWindowVisible(GetDlgItem(Window.Properties.NativeWindowHandle, 1313)), "Large disk hides the individual volume selector");

        var until = DateTime.UtcNow.AddSeconds(40);
        JsonNode weather;
        do { weather = State()["windows"]![1]!; if (weather["weatherStatus"]!.GetValue<int>() == 1) break; Thread.Sleep(500); } while (DateTime.UtcNow < until);
        Check(weather["weatherProvider"]!.GetValue<string>() == "open-meteo", "Real weather uses the default Open-Meteo service");
        Check(weather["weatherStatus"]!.GetValue<int>() == 1 && weather["weatherTemperature"] is not null,
            "Live Open-Meteo response reaches the actual weather widget");
        SelectWidget(8); NativeCombo(1300, 3); NativeButton(1316);
        Check(Summary(8).Contains("캘린더"), "Local calendar is accessible without a Google account");

        // Publish only non-identifying widget crops: no PC name, drive labels,
        // interface identifiers, actual location, account data or desktop wallpaper.
        foreach (int kind in new[] {0, 1, 2, 3, 4, 6, 8})
        {
            SelectWidget(kind); NativeCombo(1300, kind == 8 ? 3 : 2); NativeButton(1316); Thread.Sleep(200);
            var widget = State()["windows"]![kind]!;
            SetWindowPos(WidgetHandle(widget), new nint(-1), 0, 0, 0, 0, 0x13);
            ShowWindow(Window.Properties.NativeWindowHandle, 0);
            try { CaptureWidget("windows-live-kind-" + kind, widget); }
            finally { ShowWindow(Window.Properties.NativeWindowHandle, 5); }
            if (kind == 4)
            {
                using var capture = new Bitmap(Path.Combine(Data, "windows-live-kind-4.png"));
                double scale = capture.Width / 336.0;
                int whitePixels = 0;
                // Only the graph plot, excluding its baseline, title and legend.
                for (int y = (int)(58 * scale); y < (int)(110 * scale); y++)
                    for (int x = (int)(192 * scale); x < (int)(316 * scale); x++)
                    {
                        var pixel = capture.GetPixel(x, y);
                        if (pixel.R > 220 && pixel.G > 220 && pixel.B > 220) whitePixels++;
                    }
                Check(whitePixels > 20, "Actual memory history renders a visible graph line");
            }
        }
        SelectWidget(0); NativeCombo(1300, 2); NativeButton(1316);
        var clock = State()["windows"]![0]!;
        var origin = new Point(clock["x"]!.GetValue<int>() + 80, clock["y"]!.GetValue<int>() + 70);
        bool hadCursor = GetCursorPos(out var oldCursor);
        bool buttonDown = false;
        try
        {
            NativeButton(1204); ShowWindow(Window.Properties.NativeWindowHandle, 0); Thread.Sleep(200);
            bool moveInjected = hadCursor && DesktopMouse(origin, 0);
            int inputError = moveInjected ? 0 : Marshal.GetLastWin32Error();
            bool moved = false;
            var inputDeadline = DateTime.UtcNow.AddSeconds(1);
            while (moveInjected && DateTime.UtcNow < inputDeadline)
            {
                if (GetCursorPos(out var cursor) && Math.Abs(cursor.X - origin.X) <= 2 && Math.Abs(cursor.Y - origin.Y) <= 2)
                { moved = true; break; }
                Thread.Sleep(25);
            }
            File.WriteAllText(Path.Combine(Data, "input-outcome.json"), JsonSerializer.Serialize(new {
                cursorReadable = hadCursor, moveInjected, win32Error = inputError, cursorMoveObserved = moved,
                pointerCoordinatesRecorded = false }));
            if (!moved)
                Skip("OS SendInput drag", "Windows input desktop did not accept an observable cursor move; native-message tests are separate.");
            else
            {
                Check(WindowFromPoint(origin) == WidgetHandle(clock), "OS input targets the owned clock widget");
                var before = Settings()["widgets"]![0]!.DeepClone();
                buttonDown = DesktopMouse(origin, 2); Check(buttonDown, "OS mouse button-down delivered");
                Thread.Sleep(100);
                Check(DesktopMouse(new Point(origin.X + 96, origin.Y + 64), 0), "OS pointer movement delivered");
                Thread.Sleep(200);
                DesktopMouse(new Point(origin.X + 96, origin.Y + 64), 4); buttonDown = false; Thread.Sleep(300);
                Check(Settings()["widgets"]![0]!["x"]!.GetValue<double>() != before["x"]!.GetValue<double>(), "Real input drag changes saved clock position");
            }
        }
        finally
        {
            if (buttonDown) DesktopMouse(origin, 4);
            if (hadCursor) DesktopMouse(oldCursor, 0);
            ShowWindow(Window.Properties.NativeWindowHandle, 5);
            NativeButton(1204);
        }

        if (!locationAllowed)
        {
            Skip("Actual Windows location lookup", "Requires explicit consent for Windows location lookup and sending the selected region coordinates to api.open-meteo.com.");
            File.WriteAllText(Path.Combine(Data, "location-outcome.json"), JsonSerializer.Serialize(new {
                productionWinRtInvoked = false, selected = false, reason = "not-authorized", coordinatesRecorded = false, regionNamePublished = false }));
        }
        else
        {
        // Exercise the production WinRT path only after explicit consent. Applying
        // the selected region causes its coordinates to be sent to Open-Meteo.
        SelectWidget(1);
        var previousRegion = Settings()["widgets"]![1]!["region"]!.GetValue<string>();
        SetForegroundWindow(Window.Properties.NativeWindowHandle); NativeButton(1320);
        until = DateTime.UtcNow.AddSeconds(65);
        while (!Element(1320).IsEnabled && DateTime.UtcNow < until) Thread.Sleep(200);
        Check(Element(1320).IsEnabled, "Actual Windows location request completes without blocking the UI");
        var status = Element(1999).Name;
        bool locationSelected = status.Contains("선택했어요");
        Check(Settings()["widgets"]![1]!["region"]!.GetValue<string>() == previousRegion, "Real location lookup does not save before Apply");
        if (locationSelected)
        {
            NativeButton(1316);
            Check(!string.IsNullOrWhiteSpace(Settings()["widgets"]![1]!["region"]!.GetValue<string>()), "Real Windows location selection is saved with Apply");
        }
        else
        {
            Check(status.Contains("위치") || status.Contains("국내"), "Real location failure gives an actionable message");
            NativeButton(1316);
            Check(Settings()["widgets"]![1]!["region"]!.GetValue<string>() == previousRegion, "Real location failure preserves the selected region");
            Skip("Successful real position fix", status.Contains("허용") ? "Windows location access is denied or disabled; global privacy settings were preserved." : "Windows could not return a supported position; existing region was preserved.");
        }
        File.WriteAllText(Path.Combine(Data, "location-outcome.json"), JsonSerializer.Serialize(new { productionWinRtInvoked = true,
            selected = locationSelected, previousSelectionRetainedOnFailure = !locationSelected, coordinatesRecorded = false, regionNamePublished = false }));
        }

        var saved = Settings().DeepClone(); Restart(ref app, exe);
        Check(JsonNode.DeepEquals(saved, Settings()), "Production restart preserves all settings including location selection");
        VisibleWidgets("windows-live-restarted", 9);
        Check(!State()["fixture"]!.GetValue<bool>(), "Restart still uses actual providers");
        Quit(app!); app = null;
    }
}
