using System.Runtime.InteropServices;
using System.Text.Json.Nodes;
using FlaUI.Core.AutomationElements;
using App = FlaUI.Core.Application;

internal static partial class Program
{
    [DllImport("user32.dll")] static extern uint GetDpiForWindow(nint window);
    [DllImport("gdi32.dll")] static extern uint GetPixel(nint dc, int x, int y);

    // Read the retained GDI pixels. CaptureSettings and PrintWindow both repaint
    // controls and can erase the very stale selection that this suite checks.
    static uint NavigationFill(nint button)
    {
        if (!GetClientRect(button, out var bounds)) throw new Exception("Navigation bounds unavailable");
        var dc = GetDC(button);
        try { return GetPixel(dc, bounds.Width / 2, bounds.Height / 5); }
        finally { ReleaseDC(button, dc); }
    }

    static void SaveNavigationRail(nint main)
    {
        var first = SettingsClient(GetDlgItem(main, 1000));
        var last = SettingsClient(GetDlgItem(main, 1004));
        var origin = SettingsClient(main).Location;
        using var image = new Bitmap(first.Right - origin.X + first.Left - origin.X, last.Bottom - origin.Y + first.Left - origin.X);
        using (var graphics = Graphics.FromImage(image))
        {
            var dest = graphics.GetHdc();
            var source = GetDC(main);
            try
            {
                if (!BitBlt(dest, 0, 0, image.Width, image.Height, source, 0, 0, 0xCC0020))
                    throw new Exception("Could not copy the presented settings rail");
            }
            finally { ReleaseDC(main, source); graphics.ReleaseHdc(dest); }
        }
        image.Save(Path.Combine(Data, "navigation-dark-roundtrip.png"));
    }

    static void SettingsNavigation(ref App? app)
    {
        Check(!Live && !LiveApis, "Settings navigation uses fixture collectors and mock APIs");
        var main = Window.Properties.NativeWindowHandle.Value;
        nint Field(int id) => GetDlgItem(main, id);
        string[] pages = ["위젯", "연동", "앱", "배치", "정보 및 업데이트"];
        int[] order = [0, 3, 1, 2, 4], sections = [1210, 1406, 1520, 1801, 1720];
        int[] pageFields = [1200, 1403, 1500, 1804, 1713];
        int failed = 0;
        void Observe(bool ok, string name, string detail = "")
        {
            Results.Add(new { name, passed = ok, detail = ok ? "" : detail });
            if (!ok) { failed++; Console.WriteLine($"FAIL {name} {detail}"); }
        }
        NativeButton(1316);
        var saved = Settings();
        int dpi = (int)GetDpiForWindow(main);
        void Resize(bool wide)
        {
            if (!SetWindowPos(main, 0, 0, 0, (wide ? 900 : 540) * dpi / 96, 640 * dpi / 96, 0x16))
                throw new Exception("Could not resize settings");
        }
        foreach (var theme in new[] { "dark", "light", "contrast" })
        {
            File.WriteAllText(Path.Combine(Data, "settings-theme-example.txt"), theme);
            SendMessage(main, 0x31A, 0, 0); // WM_THEMECHANGED, only to TestHost
            uint ColorRef(Color c) => (uint)(c.R | c.G << 8 | c.B << 16);
            var accent = ColorRef(theme == "contrast" ? SystemColors.Highlight :
                                  theme == "dark" ? Color.FromArgb(151, 198, 255) : Color.FromArgb(25, 86, 158));
            var pane = ColorRef(theme == "contrast" ? SystemColors.Window :
                                theme == "dark" ? Color.FromArgb(38, 42, 48) : Color.White);
            void Inspect(int page, bool wide, string stage)
            {
                // Let the application's normal message loop present the selection.
                // Do not send any paint or redraw messages from the capture path.
                Thread.Sleep(60);
                var picker = Field(1012);
                bool matches = Element(1011).Name == pages[page] && order.All(p =>
                    IsWindowVisible(Field(sections[p])) == (p == page) && IsWindowVisible(Field(1000 + p)) == wide)
                    && IsWindowVisible(picker) != wide;
                string detail;
                if (wide)
                {
                    var colors = order.Select(p => NavigationFill(Field(1000 + p))).ToArray();
                    matches &= colors.SequenceEqual(order.Select(p => p == page ? accent : pane));
                    detail = "rail COLORREF: " + string.Join(", ", colors.Select(c => c.ToString("X6")));
                }
                else
                {
                    int index = (int)SendMessage(picker, 0x147, 0, 0); // CB_GETCURSEL
                    matches &= index == Array.IndexOf(order, page);
                    detail = "picker index: " + index;
                }
                Observe(matches, $"{theme} {(wide ? "rail" : "picker")} {stage} -> {pages[page]}", detail);
            }
            Resize(true);
            SetWindowPos(main, 0, 0, 0, 900 * dpi / 96, 380 * dpi / 96, 0x16);
            foreach (int page in order)
            {
                Tab(page);
                Element(pageFields[page]).Focus();
            }
            Tab(0);
            Inspect(0, true, "round trip");
            if (theme == "dark") SaveNavigationRail(main);
            foreach (bool wide in new[] { true, false })
            {
                Resize(wide);
                foreach (int from in order)
                    foreach (int to in order)
                    {
                        Tab(from);
                        Element(pageFields[from]).Focus();
                        Tab(to);
                        Inspect(to, wide, pages[from]);
                    }
            }
            foreach (int page in order)
            {
                Tab(page);
                Resize(true);
                Inspect(page, true, "expand");
                Resize(false);
                Inspect(page, false, "collapse");
            }
        }
        Observe(JsonNode.DeepEquals(saved, Settings()), "Tab changes, theme changes and resizing preserve stored settings");
        Tab(0);
        var list = Field(1200);
        GetClientRect(list, out var bounds);
        var rowHeight = (int)SendMessage(list, 0x1A1, 0, 0); // LB_GETITEMHEIGHT
        Observe(rowHeight > 0 && bounds.Height >= rowHeight * 5 && bounds.Height % rowHeight == 0,
                "The default five widgets occupy five complete visible rows", $"client {bounds.Height}, row {rowHeight}");

        // Applying a draft rebuilds the list while LBN_SELCHANGE is in progress.
        foreach (var choice in new[] { 2, 102, 101 }) // continue, discard, apply
        {
            SelectWidget(0);
            SendText(Field(1324), 0xC, 0, "위젯 선택 검사");
            SendMessage(list, 0x186, 1, 0);
            PostMessage(main, 0x111, 1200 | (1 << 16), list);
            nint dialog = 0;
            Check(SpinWait.SpinUntil(() => (dialog = OwnedWindow("#32770")) != 0, 5000), "Widget switch prompts for the draft");
            SendMessage(dialog, 0x400 + 102, choice, 0);
            Check(SpinWait.SpinUntil(() => !IsWindowVisible(dialog), 5000), "Widget switch finishes the draft dialog");
            SendMessage(main, 0, 0, 0);
            int selected = (int)SendMessage(list, 0x188, 0, 0);
            var alias = Element(1324).AsTextBox().Text;
            var widgets = Settings()["widgets"]!;
            bool continued = choice == 2;
            Observe(selected == (continued ? 0 : 1) && alias == (continued ? "위젯 선택 검사" : widgets[1]!["alias"]!.ToString())
                    && widgets[0]!["alias"]!.ToString() == (choice == 101 ? "위젯 선택 검사" : saved["widgets"]![0]!["alias"]!.ToString())
                    && IsWindowEnabled(Field(1325)) == continued,
                    $"Widget switch keeps list, editor and saved alias in sync after choice {choice}", $"selected {selected}, editor {alias}");
            if (continued) NativeButton(1325);
        }
        Quit(app!);
        app = null;
        Check(failed == 0, $"Settings navigation matrix completed with {failed} mismatches");
    }
}
