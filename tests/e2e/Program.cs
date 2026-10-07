using FlaUI.Core.AutomationElements;
using FlaUI.UIA3;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text.Json.Nodes;
using App = FlaUI.Core.Application;

internal static class Program
{
    [DllImport("user32.dll")] static extern bool PostMessage(nint hwnd, uint msg, nint w, nint l);
    [DllImport("user32.dll")] static extern bool ShowWindow(nint hwnd, int cmd);
    [DllImport("user32.dll")] static extern nint SendMessage(nint hwnd, uint msg, nint w, nint l);
    [DllImport("user32.dll")] static extern uint GetGuiResources(nint process, uint flag);
    [DllImport("user32.dll")] static extern nint GetForegroundWindow();
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(nint hwnd);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(nint hwnd, out uint process);
    [DllImport("user32.dll")] static extern bool SetWindowPos(nint hwnd, nint after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] static extern uint RegisterWindowMessage(string name);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern nint FindWindow(string className, string? title);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern nint FindWindowEx(nint parent, nint after, string? className, string? title);
    [DllImport("user32.dll")] static extern nint SetParent(nint child, nint parent);
    [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW")] static extern nint SetWindowLong(nint window, int index, nint value);
    [DllImport("user32.dll")] static extern nint WindowFromPoint(Point point);
    [DllImport("user32.dll")] static extern bool GetWindowRect(nint window, out Rectangle rectangle);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(nint window);
    [DllImport("user32.dll")] static extern bool IsIconic(nint window);
    [DllImport("user32.dll")] static extern nint GetDlgItem(nint dialog, int id);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool SetWindowText(nint window, string text);
    [DllImport("user32.dll")] static extern nint GetDC(nint window);
    [DllImport("user32.dll")] static extern int ReleaseDC(nint window, nint dc);
    [DllImport("gdi32.dll")] static extern bool BitBlt(nint dest, int x, int y, int width, int height, nint source, int sx, int sy, uint rop);
    static readonly List<object> Results = [];
    static UIA3Automation Automation = null!;
    static Window Window = null!;
    static readonly Dictionary<int, AutomationElement> Controls = [];
    static string Root = "", Data = "";
    static int ProcessId;
    static bool Live;
    static bool DesktopToggled;
    static void ToggleDesktop() { dynamic shell = Activator.CreateInstance(Type.GetTypeFromProgID("Shell.Application")!)!; try { shell.ToggleDesktop(); DesktopToggled = !DesktopToggled; Thread.Sleep(800); } finally { Marshal.FinalReleaseComObject(shell); } }
    static void Settle(int seconds, string phase) { for (int elapsed = 0; elapsed < seconds; elapsed += 30) { Thread.Sleep(Math.Min(30, seconds - elapsed) * 1000); Console.WriteLine($"STABILIZE {phase} {Math.Min(elapsed + 30, seconds)}/{seconds}s"); } }
    static nint OwnedWindow(string className) { nint handle = 0; while ((handle = FindWindowEx(0, handle, className, null)) != 0) { GetWindowThreadProcessId(handle, out uint owner); if (owner == ProcessId) return handle; } return 0; }
    static void Check(bool value, string name) { Results.Add(new { name, passed = value }); Console.WriteLine($"{(value ? "PASS" : "FAIL")} {name}"); if (!value) throw new Exception(name); }
    static void Skip(string name, string reason) { Results.Add(new { name, passed = (bool?)null, skipped = true, reason }); Console.WriteLine("SKIP " + name + ": " + reason); }
    static AutomationElement Element(int id) { if (Controls.TryGetValue(id, out var value)) return value; return Controls[id] = Window.FindFirstDescendant(c => c.ByAutomationId(id.ToString())) ?? throw new Exception($"Control {id} missing"); }
    static void Button(int id) { var element = Element(id); Console.WriteLine($"BUTTON {id} {element.Name} offscreen={element.IsOffscreen}"); element.AsButton().Invoke(); Thread.Sleep(500); if (id != 1507) Console.WriteLine($"STATUS {Element(1999).Name}"); }
    static void Combo(int id, int index) { Element(id).AsComboBox().Select(index); Thread.Sleep(100); }
    static void Tab(int index) { var tab = Element(1000).AsTab(); tab.TabItems[index].Patterns.SelectionItem.Pattern.Select(); Thread.Sleep(300); Console.WriteLine($"TAB {index} selected={tab.SelectedTabItemIndex} bounds={tab.BoundingRectangle}"); }
    static JsonObject Settings() => JsonNode.Parse(File.ReadAllText(Path.Combine(Data, "settings.json")))!.AsObject();
    static JsonObject Diagnostic() { Tab(2); Button(1505); return JsonNode.Parse(File.ReadAllText(Path.Combine(Data, "diagnostics.json")))!.AsObject(); }
    static nint WidgetHandle(JsonNode item) => (nint)item["handle"]!.GetValue<long>();
    static void NativeCombo(int id, int index) { SendMessage(Element(id).Properties.NativeWindowHandle, 0x14E, index, 0); }
    static void NativeButton(int id) { SendMessage(Element(id).Properties.NativeWindowHandle, 0xF5, 0, 0); Thread.Sleep(100); }
    static void FileDialog(int command, string file)
    {
        PostMessage(Window.Properties.NativeWindowHandle, 0x111, command, 0); nint handle = 0;
        for (int i = 0; i < 100; i++) { Thread.Sleep(100); handle = OwnedWindow("#32770"); if (handle != 0 && IsWindowVisible(handle) && GetDlgItem(handle, 1) != 0) break; }
        Check(handle != 0 && IsWindowVisible(handle), "Native layout file dialog opens"); Thread.Sleep(1000); var dialog = Automation.FromHandle(handle);
        var nativeName = GetDlgItem(handle, 1152);
        if (nativeName != 0) { Check(SetWindowText(nativeName, file), "Native file name entered"); PostMessage(handle, 0x111, 1, 0); Thread.Sleep(1000); return; }
        var name = dialog.FindFirstDescendant(c => c.ByAutomationId("FileNameControlHost")) ?? dialog.FindFirstDescendant(c => c.ByAutomationId("1148"));
        var edit = name?.FindFirstDescendant(c => c.ByControlType(FlaUI.Core.Definitions.ControlType.Edit)) ?? dialog.FindFirstDescendant(c => c.ByAutomationId("1152"));
        Check(edit != null, "File name control found");
        edit!.AsTextBox().Text = file;
        var accept = GetDlgItem(handle, 1); Check(accept != 0, "File dialog accept button found"); PostMessage(accept, 0xF5, 0, 0);
        for (int i = 0; i < 100 && IsWindowVisible(handle); i++) Thread.Sleep(100);
        Check(!IsWindowVisible(handle), "File dialog completed");
    }
    static bool WaitExit(App app, int milliseconds) { var until = DateTime.UtcNow.AddMilliseconds(milliseconds); while (!app.HasExited && DateTime.UtcNow < until) Thread.Sleep(100); return app.HasExited; }
    static void Screenshot(string name)
    {
        var bounds = SystemInformation.VirtualScreen;
        using var image = new Bitmap(bounds.Width, bounds.Height); using var graphics = Graphics.FromImage(image);
        var dest = graphics.GetHdc(); var source = GetDC(0);
        try { if (!BitBlt(dest, 0, 0, bounds.Width, bounds.Height, source, bounds.X, bounds.Y, 0x40CC0020)) throw new Exception("Desktop capture failed"); }
        finally { graphics.ReleaseHdc(dest); ReleaseDC(0, source); }
        image.Save(Path.Combine(Data, name + ".png"));
    }
    static App Start(string exe)
    {
        Controls.Clear();
        var app = App.Launch(new ProcessStartInfo(exe, $"--data-dir \"{Data}\" --settings" + (Live ? " --live" : "")) { WorkingDirectory = Root, UseShellExecute = false });
        ProcessId = app.ProcessId;
        for (int i = 0; i < 100; i++) { Thread.Sleep(100); var windows = app.GetAllTopLevelWindows(Automation); var window = windows.FirstOrDefault(x => x.Title.Contains("위젯 설정")); if (window != null) { Window = window; return app; } if (app.HasExited) throw new Exception($"App exited before its settings window was created: {app.ExitCode:X8}"); }
        throw new Exception("Management window startup timeout");
    }
    static void Quit(App app) { Tab(2); Button(1507); Check(WaitExit(app, 10000), "Clean process shutdown"); app.Dispose(); }
    [STAThread]
    static int Main(string[] args)
    {
        System.Windows.Forms.Application.SetHighDpiMode(HighDpiMode.PerMonitorV2);
        Root = Path.GetFullPath(args.Length > 0 ? args[0] : "."); Data = Path.Combine(Root, "test-results", "e2e-" + DateTime.Now.ToString("yyyyMMdd-HHmmss")); Directory.CreateDirectory(Data);
        Live = args.Contains("--live-smoke");
        string exe = Path.Combine(Root, "out", "release", "Tempos.TestHost.exe");
        int exeArg = Array.IndexOf(args, "--exe"); if (exeArg >= 0 && exeArg + 1 < args.Length) exe = Path.GetFullPath(args[exeArg + 1]);
        App? app = null; Automation = new UIA3Automation();
        try
        {
            app = Start(exe); Check(Element(1200).AsListBox().Items.Length == 5, "Initial five widgets");
            if (Live) { Thread.Sleep(4000); var state = Diagnostic(); foreach (var entry in state["windows"]!.AsArray().Where(x => new[] { 2, 3, 4 }.Contains(x!["kind"]!.GetValue<int>()))) { var summary = Automation.FromHandle(WidgetHandle(entry!)).Name; Console.WriteLine("LIVE " + summary); Check(summary.Contains('%'), "Live system usage available: " + entry!["kind"]); } Quit(app); app = null; return 0; }
            if (args.Contains("--layout")) { Tab(2); FileDialog(1503, Path.Combine(Data, "layout-export.json")); Check(File.Exists(Path.Combine(Data, "layout-export.json")), "Layout export created through UI"); FileDialog(1504, Path.Combine(Data, "layout-export.json")); Check(Settings()["widgets"]!.AsArray().Count == 5, "Layout import through UI"); Quit(app); app = null; return 0; }
            if (args.Contains("--smoke"))
            {
                Check(SendMessage(Element(1314).Properties.NativeWindowHandle, 0x146, 0, 0) == 3837, "All 3837 packaged weather regions loaded");
                var state = Diagnostic(); Check(File.Exists(Path.Combine(Data, "diagnostics.json")), "Diagnostic file created");
                if (exeArg >= 0) { Check(!state["fixture"]!.GetValue<bool>(), "Packaged executable uses production providers"); Check(!File.Exists(Path.Combine(Path.GetDirectoryName(exe)!, "Tempos.TestHost.exe")), "Package excludes test host"); }
                Quit(app); app = null; return 0;
            }
            if (args.Contains("--interaction"))
            {
                for (int i = 0; i < 5; i++) NativeButton(1203);
                NativeCombo(1201, 0); NativeButton(1202); Button(1204);
                void DragCurrent()
                {
                    var state = Diagnostic(); var entry = state["windows"]![0]!;
                    var point = new Point(entry["x"]!.GetValue<int>() + 80, entry["y"]!.GetValue<int>() + 70);
                    ShowWindow(Window.Properties.NativeWindowHandle, 0); Thread.Sleep(300);
                    Check(WindowFromPoint(point) == WidgetHandle(entry), "Drag targets the owned widget");
                    var widget = WidgetHandle(entry);
                    // SendInput/SetCursorPos are unavailable in this automation session.
                    // Drive native mouse messages on the owned widget without moving the user's pointer.
                    SendMessage(widget, 0x201, 1, (nint)((70 << 16) | 80));
                    SendMessage(widget, 0x200, 1, (nint)((189 << 16) | 253));
                    SendMessage(widget, 0x202, 0, (nint)((70 << 16) | 80));
                    ShowWindow(Window.Properties.NativeWindowHandle, 5);
                    Thread.Sleep(300); Tab(0);
                }
                var before = Settings()["widgets"]![0]!.DeepClone(); DragCurrent(); var moved = Settings()["widgets"]![0]!;
                Check(moved["x"]!.GetValue<double>() != before["x"]!.GetValue<double>(), "Native drag messages change saved position");
                Check(moved["x"]!.GetValue<double>() % 4 == 0 && moved["y"]!.GetValue<double>() % 4 == 0, "Native drag messages snap to four-DIP grid");
                Element(1308).AsCheckBox().IsChecked = true; NativeButton(1316); before = Settings()["widgets"]![0]!.DeepClone(); DragCurrent();
                Check(Settings()["widgets"]![0]!["x"]!.GetValue<double>() == before["x"]!.GetValue<double>() && Settings()["widgets"]![0]!["y"]!.GetValue<double>() == before["y"]!.GetValue<double>(), "Locked widget rejects native drag messages");
                Button(1204); Element(1307).AsCheckBox().IsChecked = true; NativeButton(1316); var normal = Diagnostic(); Check((normal["windows"]![0]!["extendedStyle"]!.GetValue<long>() & 0x20) != 0, "Click-through enabled outside edit mode");
                Tab(0); Button(1204); var editing = Diagnostic(); Check((editing["windows"]![0]!["extendedStyle"]!.GetValue<long>() & 0x20) == 0, "Edit mode restores pointer access");
                Quit(app); app = null; return 0;
            }
            if (args.Contains("--hosts"))
            {
                var hostState = Diagnostic(); var shell = FindWindow("Progman", null); var view = FindWindowEx(shell, 0, "SHELLDLL_DefView", null);
                ShowWindow(Window.Properties.NativeWindowHandle, 0); Thread.Sleep(500); Screenshot("host-progman");
                foreach (var entry in hostState["windows"]!.AsArray())
                {
                    var h = WidgetHandle(entry!); SetParent(h, view); SetWindowPos(h, 0, entry!["x"]!.GetValue<int>(), entry["y"]!.GetValue<int>(), entry["width"]!.GetValue<int>(), entry["height"]!.GetValue<int>(), 0x50); SendMessage(h, 0xF, 0, 0);
                }
                Thread.Sleep(500); Screenshot("host-defview");
                foreach (var entry in hostState["windows"]!.AsArray())
                {
                    var h = WidgetHandle(entry!); SetParent(h, 0); SetWindowLong(h, -16, unchecked((nint)0x90000000)); SetWindowLong(h, -8, shell); SetWindowPos(h, new nint(1), entry!["x"]!.GetValue<int>(), entry["y"]!.GetValue<int>(), entry["width"]!.GetValue<int>(), entry["height"]!.GetValue<int>(), 0x70); SendMessage(h, 0xF, 0, 0);
                }
                Thread.Sleep(500); Screenshot("host-owned-popup"); ShowWindow(Window.Properties.NativeWindowHandle, 5); Quit(app); app = null; return 0;
            }
            if (args.Contains("--visual"))
            {
                var visualState = Diagnostic(); Tab(0);
                Button(1204); ShowWindow(Window.Properties.NativeWindowHandle, 0); Thread.Sleep(1000); Screenshot("edit-mode"); ShowWindow(Window.Properties.NativeWindowHandle, 5); Button(1204);
                ShowWindow(Window.Properties.NativeWindowHandle, 0); Thread.Sleep(500); Screenshot("desktop-visual");
                void DesktopVisible(string stage) { foreach (var entry in visualState["windows"]!.AsArray()) { var point = new Point(entry!["x"]!.GetValue<int>() + entry["width"]!.GetValue<int>() / 2, entry["y"]!.GetValue<int>() + entry["height"]!.GetValue<int>() / 2); Check(WindowFromPoint(point) == WidgetHandle(entry), stage + " widget " + entry["kind"]); } }
                DesktopVisible("Desktop hit test");
                ShowWindow(Window.Properties.NativeWindowHandle, 9); Thread.Sleep(300); ToggleDesktop();
                if (IsIconic(Window.Properties.NativeWindowHandle) || !IsWindowVisible(Window.Properties.NativeWindowHandle))
                {
                    Check(true, "Shell Show Desktop hides management window"); Screenshot("show-desktop"); DesktopVisible("Shell Show Desktop preserves");
                }
                else Skip("Shell Show Desktop / physical Win+D", "Shell.ToggleDesktop had no observable effect in this automation session; physical keyboard input must be verified separately.");
                ToggleDesktop(); ShowWindow(Window.Properties.NativeWindowHandle, 9);
                Tab(2); Button(1505); Quit(app); app = null; return 0;
            }
            if (args.Contains("--matrix"))
            {
                for (int i = 0; i < 5; i++) NativeButton(1203);
                NativeCombo(1201, 0); NativeButton(1202);
                Check(Element(1200).AsListBox().Items.Length == 1, "Matrix uses one widget to check all supported sizes");
                for (int kind = 0; kind < 9; kind++)
                {
                    if (kind > 0) { NativeButton(1203); NativeCombo(1201, kind); NativeButton(1202); }
                    for (int size = 0; size < (kind == 8 ? 5 : 4); size++)
                    {
                        NativeCombo(1300, size); NativeCombo(1301, 0); NativeButton(1316);
                        Check(Settings()["widgets"]![0]!["kind"]!.GetValue<int>() == kind && Settings()["widgets"]![0]!["size"]!.GetValue<int>() == size, $"Widget {kind} size {size} accepted");
                        var matrixDiagnostic = Diagnostic(); var item = matrixDiagnostic["windows"]![0]!;
                        int dpi = item["dpi"]!.GetValue<int>(); int width = new[] { 160, 336, 336, 336, 1008 }[size], height = new[] { 160, 72, 160, 336, 840 }[size];
                        Check(item["placed"]!.GetValue<bool>() && Math.Abs(item["width"]!.GetValue<int>() - width * dpi / 96.0) < 2 && Math.Abs(item["height"]!.GetValue<int>() - height * dpi / 96.0) < 2, $"Widget {kind} size {size} keeps aspect ratio at {dpi} DPI");
                        Check((item["extendedStyle"]!.GetValue<long>() & 0x80) != 0, $"Widget {kind} size {size} has tool-window style");
                        var widget = Automation.FromHandle(WidgetHandle(item));
                        Check(!string.IsNullOrWhiteSpace(widget.Name), $"Widget {kind} size {size} accessible summary");
                        if (kind == 8) Check(!widget.Name.Contains("Google") && !widget.Name.Contains("구글"), $"Calendar size {size} hides provider branding");
                        Tab(0);
                        if (size == (kind == 8 ? 4 : 3)) { Button(1204); ShowWindow(Window.Properties.NativeWindowHandle, 0); Thread.Sleep(200); Screenshot($"widget-{kind}-size-{size}"); ShowWindow(Window.Properties.NativeWindowHandle, 5); Button(1204); }
                    }
                    NativeCombo(1300, 0); NativeCombo(1301, 2); NativeButton(1316);
                    Check(Settings()["widgets"]![0]!["scale"]!.GetValue<int>() == 200, $"Widget {kind} scales to 200 percent");
                }
                NativeCombo(1300, 2); NativeCombo(1301, 0); Element(1306).AsTextBox().Text = "100"; NativeButton(1316);
                Check(Settings()["widgets"]![0]!["transparency"]!.GetValue<int>() == 100, "Fully transparent widget persists");
                Button(1204); Check(Element(1999).Name.Contains("정렬 모드"), "Invisible widget can enter recovery edit mode"); Button(1204);
                Element(1306).AsTextBox().Text = "0"; NativeButton(1316);
                var state = Diagnostic(); Check(state["trayRegistered"]!.GetValue<bool>(), "Tray icon registered");
                var handle = WidgetHandle(state["windows"]![0]!); Tab(0);
                SendMessage(handle, 0x201, 1, (nint)((60 << 16) | 40)); Thread.Sleep(300);
                var detailsHandle = OwnedWindow("Tempos.Details");
                var details = detailsHandle == 0 ? null : Automation.FromHandle(detailsHandle).AsWindow();
                Check(details != null, "Calendar detail opens from widget");
                var content = details!.FindFirstDescendant(c => c.ByAutomationId("1800"))!;
                Check(content.Patterns.Value.Pattern.IsReadOnly, "Calendar detail is strictly read-only"); PostMessage(details.Properties.NativeWindowHandle, 0x10, 0, 0);
                PostMessage(Window.Properties.NativeWindowHandle, RegisterWindowMessage("TaskbarCreated"), 0, 0); Thread.Sleep(300);
                state = Diagnostic(); Check(state["trayRegistered"]!.GetValue<bool>(), "Tray re-registration message handled");
                Quit(app); app = null; return 0;
            }
            if (args.Contains("--behavior"))
            {
                for (int i = 0; i < 5; i++) NativeButton(1203);
                NativeCombo(1201, 2); NativeButton(1202); NativeCombo(1303, 3); NativeButton(1316);
                Check(Settings()["widgets"]![0]!["interval"]!.GetValue<int>() == 10, "CPU individual refresh set to ten seconds");
                NativeButton(1202); NativeCombo(1303, 0); NativeButton(1316); Thread.Sleep(2500);
                var before = Diagnostic(); Check(before["subscriptions"]!.GetValue<int>() == 1, "Duplicate CPU widgets share one collector");
                Thread.Sleep(4500); var after = Diagnostic();
                long slow = after["windows"]![0]!["redraws"]!.GetValue<long>() - before["windows"]![0]!["redraws"]!.GetValue<long>();
                long fast = after["windows"]![1]!["redraws"]!.GetValue<long>() - before["windows"]![1]!["redraws"]!.GetValue<long>();
                Check(fast >= 3 && slow < fast, "Shared collection preserves separate presentation intervals");
                Tab(0); Button(1205); before = Diagnostic(); Thread.Sleep(2300); after = Diagnostic();
                Check(before["redraws"]!.GetValue<long>() == after["redraws"]!.GetValue<long>(), "Hidden widgets stop drawing");
                Tab(0); Button(1205);
                int screens = Element(1304).AsComboBox().Items.Length;
                for (int monitor = 0; monitor < screens; monitor++)
                {
                    NativeCombo(1304, monitor); NativeButton(1316); var state = Diagnostic(); var view = state["windows"]![1]!; var screen = state["monitors"]![monitor]!;
                    int dpi = screen["dpi"]!.GetValue<int>();
                    Check(view["dpi"]!.GetValue<int>() == dpi && view["width"]!.GetValue<int>() == 336 * dpi / 96, $"Monitor {monitor} uses its own {dpi} DPI");
                    Check(view["x"]!.GetValue<int>() >= screen["left"]!.GetValue<int>() && view["y"]!.GetValue<int>() >= screen["top"]!.GetValue<int>(), $"Monitor {monitor} origin applied"); Tab(0);
                }
                NativeButton(1203); NativeButton(1203); NativeCombo(1201, 0); NativeButton(1202);
                Element(1310).AsCheckBox().IsChecked = true; NativeButton(1316); Check(Settings()["widgets"]![0]!["seconds"]!.GetValue<bool>() && Settings()["widgets"]![0]!["interval"]!.GetValue<int>() == 1, "Clock seconds and interval agree");
                NativeCombo(1303, 1); NativeButton(1316); Check(!Settings()["widgets"]![0]!["seconds"]!.GetValue<bool>() && Settings()["widgets"]![0]!["interval"]!.GetValue<int>() == 60, "Clock refresh dropdown updates seconds display");
                Tab(3); Element(1600).AsCheckBox().IsChecked = true; Button(1620); Check(Settings()["widgets"]![0]!["twelveHour"]!.GetValue<bool>(), "Twelve-hour display persists");
                Tab(0); Element(1315).AsTextBox().Text = "Invalid/Timezone"; NativeButton(1316); Check(Settings()["widgets"]![0]!["timezone"]!.GetValue<string>() == "", "Invalid timezone rejected");
                Element(1315).AsTextBox().Text = "Asia/Seoul"; NativeButton(1316); Check(Settings()["widgets"]![0]!["timezone"]!.GetValue<string>() == "Asia/Seoul", "IANA timezone accepted");
                Tab(2); FileDialog(1503, Path.Combine(Data, "layout-export.json")); Check(File.Exists(Path.Combine(Data, "layout-export.json")), "Layout export created through UI");
                var exported = File.ReadAllText(Path.Combine(Data, "layout-export.json")); Check(!exported.Contains("calendar-client") && !exported.Contains("refresh_token"), "Layout export excludes account data");
                Tab(0); NativeButton(1203); Check(Settings()["widgets"]!.AsArray().Count == 0, "All widgets can be removed");
                Tab(2); FileDialog(1504, Path.Combine(Data, "layout-export.json")); Check(Settings()["widgets"]!.AsArray().Count == 1, "Layout import restores widget");
                Quit(app); app = null; return 0;
            }
            if (args.Contains("--stress"))
            {
                var add = Element(1202).Properties.NativeWindowHandle.Value; var remove = Element(1203).Properties.NativeWindowHandle.Value;
                var list = Element(1200).Properties.NativeWindowHandle.Value;
                using var process = Process.GetProcessById(app.ProcessId);
                void Cycle() { SendMessage(add, 0xF5, 0, 0); if (SendMessage(list, 0x18B, 0, 0) != 6) throw new Exception("Stress add count"); SendMessage(remove, 0xF5, 0, 0); if (SendMessage(list, 0x18B, 0, 0) != 5) throw new Exception("Stress remove count"); }
                for (int i = 0; i < 100; i++) { Cycle(); Thread.Sleep(2); }
                Settle(300, "before"); process.Refresh(); long startPrivate = process.PrivateMemorySize64; int startHandles = process.HandleCount; uint startGdi = GetGuiResources(process.Handle, 0), startUser = GetGuiResources(process.Handle, 1);
                for (int i = 0; i < 1000; i++) { Cycle(); Thread.Sleep(2); if ((i + 1) % 100 == 0) { process.Refresh(); Console.WriteLine($"STRESS {i + 1}/1000 private={process.PrivateMemorySize64} growth={process.PrivateMemorySize64 - startPrivate}"); } }
                Settle(300, "after"); process.Refresh(); long growth = process.PrivateMemorySize64 - startPrivate;
                var stress = new { cycles = 1000, warmupSeconds = 300, settleSeconds = 300, privateGrowth = growth, handleGrowth = process.HandleCount - startHandles, gdiGrowth = (long)GetGuiResources(process.Handle, 0) - startGdi, userGrowth = (long)GetGuiResources(process.Handle, 1) - startUser };
                File.WriteAllText(Path.Combine(Data, "stress.json"), System.Text.Json.JsonSerializer.Serialize(stress)); Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(stress));
                Check(growth <= 2 * 1024 * 1024, "1000 add/remove private growth <=2 MiB"); Check(stress.gdiGrowth <= 0 && stress.userGrowth <= 0, "1000 add/remove GDI and USER resources return to baseline");
                Quit(app); app = null; return 0;
            }
            Button(1316); Check(File.Exists(Path.Combine(Data, "settings.json")), "Settings persisted by UI");
            for (int kind = 5; kind <= 8; kind++) { Combo(1201, kind); Button(1202); }
            Check(Element(1200).AsListBox().Items.Length == 9, "All nine widget types added");
            Combo(1302, 9); Element(1305).AsTextBox().Text = "37"; Element(1306).AsTextBox().Text = "24"; Button(1316);
            var last = Settings()["widgets"]!.AsArray().Last()!; Check(last["theme"]!.GetValue<int>() == 8, "Per-widget theme");
            Check(last["backgroundTransparency"]!.GetValue<int>() == 37 && last["transparency"]!.GetValue<int>() == 24, "Independent opacity persistence");
            Combo(1300, 3); Button(1316); Check(Settings()["widgets"]!.AsArray().Last()!["size"]!.GetValue<int>() == 3, "Calendar large size");
            Button(1319); Check(Settings()["widgets"]!.AsArray().Last()!["source"]!.GetValue<string>().Length > 0, "Calendar next month"); Button(1318);
            Element(1305).AsTextBox().Text = "101"; Button(1316); Check(Settings()["widgets"]!.AsArray().Last()!["backgroundTransparency"]!.GetValue<int>() == 37, "Invalid opacity rejected"); Element(1305).AsTextBox().Text = "37";
            Button(1204); Check(Element(1999).Name.Contains("정렬 모드"), "Edit mode entered"); Button(1204);
            Button(1205); Tab(2); Button(1505); var diagnostic = JsonNode.Parse(File.ReadAllText(Path.Combine(Data, "diagnostics.json")))!; Check(diagnostic["hidden"]!.GetValue<bool>(), "All widgets hidden");
            Tab(0); Button(1205); Tab(1); Check(Element(1412).Name == "브라우저에서 연결", "Calendar connection only in settings");
            Check(!Window.FindAllDescendants().Any(x => new[] { "일정 추가", "일정 편집", "일정 삭제" }.Contains(x.Name)), "No calendar mutation controls");
            Button(1412); Check(Element(1999).Name.Contains("클라이언트 ID"), "Missing OAuth credentials handled");
            Tab(2); Combo(1500, 4); Button(1501); Check(Settings()["theme"]!.GetValue<int>() == 4, "Global theme persistence");
            Screenshot("settings"); Tab(0); ShowWindow(Window.Properties.NativeWindowHandle, 0);
            Thread.Sleep(700); Screenshot("desktop-widgets");
            Check(!app.HasExited, "Closing management keeps widgets alive"); ShowWindow(Window.Properties.NativeWindowHandle, 5);
            Quit(app); app = null; app = Start(exe); Check(Element(1200).AsListBox().Items.Length == 9, "Restart restores nine widgets");
            Tab(2); Button(1505); var diagnostic2 = JsonNode.Parse(File.ReadAllText(Path.Combine(Data, "diagnostics.json")))!; Check(diagnostic2["windows"]!.AsArray().Count == 9, "Nine native widget windows");
            Console.WriteLine(diagnostic2.ToJsonString());
            Quit(app); app = null; Check(!File.ReadAllText(Path.Combine(Data, "settings.json")).Contains("refresh_token"), "Settings contain no OAuth token");
            return 0;
        }
        catch (Exception e) { Results.Add(new { name = "Unhandled test failure: " + e.Message, passed = false }); Console.Error.WriteLine(e); Console.WriteLine($"App exited={app?.HasExited}"); try { Screenshot("failure"); } catch { } return 1; }
        finally
        {
            if (DesktopToggled) { try { ToggleDesktop(); } catch { } }
            if (app != null) { try { var dialog = OwnedWindow("#32770"); if (dialog != 0) { PostMessage(dialog, 0x10, 0, 0); Thread.Sleep(200); } PostMessage(Window.Properties.NativeWindowHandle, 0x111, 1507, 0); WaitExit(app, 5000); } catch { } app.Dispose(); }
            Automation.Dispose(); File.WriteAllText(Path.Combine(Data, "results.json"), System.Text.Json.JsonSerializer.Serialize(new { completed = DateTimeOffset.Now, mode = args.FirstOrDefault(a => a.StartsWith("--")) ?? "full", executableSha256 = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(exe))), results = Results }, new System.Text.Json.JsonSerializerOptions { WriteIndented = true }));
        }
    }
}
