using FlaUI.Core.AutomationElements;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json.Nodes;
using App = FlaUI.Core.Application;

internal static partial class Program
{
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(nint window, StringBuilder text, int length);
    static void SelectWidget(int index)
    {
        var list = Element(1200).Properties.NativeWindowHandle.Value;
        SendMessage(list, 0x186, index, 0);
        SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1200 | (1 << 16), list);
    }
    static JsonObject State()
    {
        SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1505, 0);
        return JsonNode.Parse(File.ReadAllText(Path.Combine(Data, "diagnostics.json")))!.AsObject();
    }
    static string[] Ids(JsonNode settings) => settings["widgets"]!.AsArray().Select(w => w!["id"]!.GetValue<string>()).ToArray();
    static void VisibleWidgets(string stage, int count, bool hitTest = false)
    {
        var state = State();
        File.WriteAllText(Path.Combine(Data, stage + ".json"), state.ToJsonString());
        var windows = state["windows"]!.AsArray();
        Check(SendMessage(GetDlgItem(Window.Properties.NativeWindowHandle, 1200), 0x18B, 0, 0) == count && windows.Count == count, stage + ": list and native window counts");
        Check(windows.Select(w => w!["id"]!.GetValue<string>()).SequenceEqual(Ids(Settings())), stage + ": window identities match saved widgets");
        Check(!state["hidden"]!.GetValue<bool>() && windows.All(w => w!["placed"]!.GetValue<bool>() && w["visible"]!.GetValue<bool>() && IsWindowVisible(WidgetHandle(w))), stage + ": all widgets placed and visible");
        Check(windows.All(w => w!["width"]!.GetValue<int>() > 1 && w["height"]!.GetValue<int>() > 1 && GetWindowThreadProcessId(WidgetHandle(w), out var pid) != 0 && pid == ProcessId), stage + ": live windows owned by test process");
        if (!hitTest) return;
        ShowWindow(Window.Properties.NativeWindowHandle, 0);
        try
        {
            Thread.Sleep(300);
            foreach (var w in windows)
            {
                var point = new Point(w!["x"]!.GetValue<int>() + w["width"]!.GetValue<int>() / 2, w["y"]!.GetValue<int>() + w["height"]!.GetValue<int>() / 2);
                var actual = WindowFromPoint(point);
                var cls = new StringBuilder(128); GetClassName(actual, cls, cls.Capacity);
                Check(actual == WidgetHandle(w), stage + ": desktop hit test kind " + w["kind"] + " (hit " + cls + ")");
            }
            var first = windows.First();
            Rectangle? crop = stage.StartsWith("clock-") || stage.StartsWith("design-") ? new Rectangle(first!["x"]!.GetValue<int>(), first["y"]!.GetValue<int>(), first["width"]!.GetValue<int>(), first["height"]!.GetValue<int>()) : null;
            Screenshot(stage, crop);
        }
        finally { ShowWindow(Window.Properties.NativeWindowHandle, 5); }
    }
    static void Restart(ref App? app, string exe)
    {
        Quit(app!); app = null; app = Start(exe);
    }
    static void SavedLayout(ref App? app, string exe)
    {
        int count = Settings()["widgets"]!.AsArray().Count;
        VisibleWidgets("copied-layout-started", count, true);
        var saved = Settings().DeepClone();
        for (int i = 0; i < count; i++) { SelectWidget(i); NativeButton(1316); VisibleWidgets("copied-layout-apply-" + i, count, true); }
        // Applying a weather region resolves its coordinates from the packaged
        // region table; old profiles can contain the rounded default coordinates.
        var regions = JsonNode.Parse(File.ReadAllText(Path.Combine(Root, "data", "regions.json")))!.AsArray();
        foreach (var w in saved["widgets"]!.AsArray().Where(w => w!["kind"]!.GetValue<int>() == 1))
        {
            var region = regions.FirstOrDefault(r => r!["name"]!.GetValue<string>() == w!["region"]!.GetValue<string>());
            if (region == null) continue;
            w!["latitude"] = region["lat"]!.DeepClone(); w["longitude"] = region["lon"]!.DeepClone();
            w["nx"] = region["nx"]!.DeepClone(); w["ny"] = region["ny"]!.DeepClone();
        }
        Check(JsonNode.DeepEquals(saved, Settings()), "Applying every copied widget preserves complete saved layout");
        Button(1204);
        for (int kind = 0; kind < 9; kind++)
        {
            NativeCombo(1201, kind); NativeButton(1202); NativeButton(1316);
            VisibleWidgets("copied-layout-add-" + kind, count + kind + 1);
        }
        Button(1204); VisibleWidgets("copied-layout-edit-exit", count + 9, true);
        saved = Settings().DeepClone(); Restart(ref app, exe);
        Check(JsonNode.DeepEquals(saved, Settings()), "Copied layout plus added widgets survives production restart");
        VisibleWidgets("copied-layout-restarted", count + 9, true);
        Quit(app!); app = null;
    }
    static void Persistence(ref App? app, string exe)
    {
        for (int i = 0; i < 5; i++) Button(1203);
        Check(Settings()["widgets"]!.AsArray().Count == 0, "Empty layout saved");
        for (int kind = 0; kind < 9; kind++)
        {
            Combo(1201, kind); Button(1202);
            VisibleWidgets("added-" + kind, kind + 1);
            Combo(1302, kind + 1); Button(1316);
            VisibleWidgets("applied-" + kind, kind + 1);
        }
        var saved = Settings().DeepClone();
        VisibleWidgets("nine-saved", 9, true);
        for (int cycle = 0; cycle < 3; cycle++)
        {
            Button(1204);
            VisibleWidgets("edit-enter-" + cycle, 9, true);
            SelectWidget(cycle); Button(1316);
            VisibleWidgets("edit-apply-" + cycle, 9, true);
            Button(1204);
            VisibleWidgets("edit-exit-" + cycle, 9, true);
        }
        Check(JsonNode.DeepEquals(saved, Settings()), "Repeated apply and edit transitions preserve entire layout");
        PostMessage(Window.Properties.NativeWindowHandle, 0x10, 0, 0); Thread.Sleep(300);
        Check(!app!.HasExited && !IsWindowVisible(Window.Properties.NativeWindowHandle), "Actual settings close keeps process running");
        VisibleWidgets("settings-closed", 9, true);
        Restart(ref app, exe);
        Check(JsonNode.DeepEquals(saved, Settings()), "Restart preserves every widget and setting");
        VisibleWidgets("restarted", 9, true);
        foreach (var notification in new[] { 0x007Eu, 0x001Au, RegisterWindowMessage("TaskbarCreated") })
        {
            SendMessage(Window.Properties.NativeWindowHandle, notification, 0, 0);
            VisibleWidgets("shell-notification-" + notification, 9, true);
        }
        Button(1205);
        Check(State()["windows"]!.AsArray().All(w => !w!["visible"]!.GetValue<bool>()), "Hide hides every native widget");
        Restart(ref app, exe);
        Check(Settings()["hidden"]!.GetValue<bool>() && State()["windows"]!.AsArray().All(w => !w!["visible"]!.GetValue<bool>()), "Restart preserves intentional hidden state");
        Button(1205); VisibleWidgets("shown-after-restart", 9, true);
        var beforeDelete = Ids(Settings()); SelectWidget(4); Button(1203);
        Check(Ids(Settings()).SequenceEqual(beforeDelete.Where((_, i) => i != 4)), "Removing middle widget preserves all other identities");
        VisibleWidgets("middle-removed", 8, true);
        Restart(ref app, exe); VisibleWidgets("deletion-restarted", 8, true);
        Quit(app!); app = null;
    }
    static void Capacity(ref App? app, string exe)
    {
        for (int i = 0; i < 5; i++) NativeButton(1203);
        for (int i = 0; i < 64; i++)
        {
            NativeCombo(1201, 0); NativeButton(1202);
            Check(Settings()["widgets"]!.AsArray().Count == i + 1, "Capacity add " + (i + 1));
            NativeCombo(1300, 0); NativeButton(1316);
        }
        var saved = Settings().DeepClone();
        Check(Ids(saved).Distinct().Count() == 64, "64 duplicate-kind widgets have distinct identities");
        NativeButton(1202);
        Check(Element(1999).Name.Contains("64") && JsonNode.DeepEquals(saved, Settings()), "65th add rejected without changing layout");
        VisibleWidgets("capacity-full", 64);
        Restart(ref app, exe); VisibleWidgets("capacity-restarted", 64);
        for (int i = 0; i < 64; i++) NativeButton(1203);
        Check(Settings()["widgets"]!.AsArray().Count == 0 && State()["windows"]!.AsArray().Count == 0, "Removing all widgets releases native windows");
        Check(!Element(1316).IsEnabled, "Apply disabled without selected widget");
        Tab(3); Check(!Element(1620).IsEnabled, "Display options disabled without selected widget");
        Restart(ref app, exe);
        Check(Element(1200).AsListBox().Items.Length == 0 && State()["windows"]!.AsArray().Count == 0, "Empty layout remains empty after restart");
        Tab(0); NativeCombo(1201, 8); NativeButton(1202);
        VisibleWidgets("added-after-empty", 1, true);
        Quit(app!); app = null;
    }
    static void Failures(ref App? app, string exe)
    {
        Button(1316);
        var saved = Settings().DeepClone();
        foreach (var value in new[] { "-1", "101", "", "abc", "1.5", "2147483648", "10x" })
        {
            Element(1306).AsTextBox().Text = value; NativeButton(1316);
            Check(Element(1999).Name.Contains("입력값") && JsonNode.DeepEquals(saved, Settings()), "Invalid transparency preserves layout: " + value);
        }
        SelectWidget(0);
        Element(1315).AsTextBox().Text = "Invalid/Timezone"; NativeButton(1316);
        Check(Element(1999).Name.Contains("입력값") && JsonNode.DeepEquals(saved, Settings()), "Invalid timezone preserves layout");
        SelectWidget(0);
        void BlockedWrite(string name, Action action, ref App? currentApp)
        {
            var before = Settings().DeepClone();

            using (var lockedFile = new FileStream(Path.Combine(Data, "settings.json"), FileMode.Open, FileAccess.Read, FileShare.Read))
            {
                action();
                for (int wait = 0; wait < 20 && !Element(1999).Name.Contains("저장 실패"); wait++) Thread.Sleep(100);
                Check(Element(1999).Name.Contains("저장 실패"), name + ": reports save failure (" + Element(1999).Name + ")");
                Check(JsonNode.DeepEquals(before, Settings()), name + ": preserves on-disk settings");
                var state = State();
                Check(state["windows"]!.AsArray().Select(w => w!["id"]!.GetValue<string>()).SequenceEqual(Ids(before)) && state["hidden"]!.GetValue<bool>() == before["hidden"]!.GetValue<bool>(), name + ": preserves live widgets");
            }
            // An unlocked save must not silently commit a previously rejected change.
            Restart(ref currentApp, exe);
            Check(JsonNode.DeepEquals(before, Settings()), name + ": rejected change stays rejected after exit/restart");
        }
        BlockedWrite("Apply", () => { NativeCombo(1302, 8); NativeButton(1316); }, ref app);
        BlockedWrite("Add", () => { NativeCombo(1201, 8); NativeButton(1202); }, ref app);
        BlockedWrite("Remove", () => NativeButton(1203), ref app);
        BlockedWrite("Hide", () => NativeButton(1205), ref app);
        BlockedWrite("Global theme", () => { Tab(2); NativeCombo(1500, 8); NativeButton(1501); }, ref app);
        BlockedWrite("Display options", () => { Tab(3); Element(1600).AsCheckBox().IsChecked = true; NativeButton(1620); }, ref app);
        var importPath = Path.Combine(Data, "import.json");
        var imported = saved.DeepClone(); imported["theme"] = 7;
        File.WriteAllText(importPath, imported.ToJsonString());
        BlockedWrite("Import", () => { Tab(2); FileDialog(1504, importPath); }, ref app);

        var invalidImports = new Dictionary<string, JsonNode>();
        var duplicate = saved.DeepClone(); duplicate["widgets"]!.AsArray().Add(duplicate["widgets"]![0]!.DeepClone()); invalidImports["duplicate IDs"] = duplicate;
        var future = saved.DeepClone(); future["version"] = 99; invalidImports["unsupported version"] = future;
        var wrongSize = saved.DeepClone(); wrongSize["widgets"]![0]!["size"] = 4; invalidImports["non-calendar XL"] = wrongSize;
        var invalidCount = saved.DeepClone(); invalidCount["widgets"] = new JsonObject(); invalidImports["invalid widget array"] = invalidCount;
        var excessive = saved.DeepClone(); excessive["widgets"] = new JsonArray(Enumerable.Range(0, 65).Select(i => { var w = saved["widgets"]![0]!.DeepClone(); w["id"] = "too-many-" + i; return w; }).ToArray()); invalidImports["65 widgets"] = excessive;
        Tab(2);
        foreach (var pair in invalidImports)
        {
            File.WriteAllText(importPath, pair.Value.ToJsonString()); FileDialog(1504, importPath);
            Check(Element(1999).Name.Contains("처리") && JsonNode.DeepEquals(saved, Settings()), "Rejected import preserves layout: " + pair.Key);
        }
        File.WriteAllText(importPath, "{broken"); FileDialog(1504, importPath);
        Check(JsonNode.DeepEquals(saved, Settings()) && Element(1999).Name.Contains("처리"), "Malformed JSON import preserves layout");
        File.WriteAllText(importPath, imported.ToJsonString()); FileDialog(1504, importPath);
        Check(Settings()["theme"]!.GetValue<int>() == 7 && SendMessage(Element(1500).Properties.NativeWindowHandle, 0x147, 0, 0) == 7, "Import synchronizes global theme control");
        VisibleWidgets("imported", 5);
        var backup = Settings().DeepClone();
        Restart(ref app, exe);
        Quit(app!); app = null;
        File.WriteAllText(Path.Combine(Data, "settings.json"), "{broken");
        app = Start(exe);
        Check(Element(1999).Name.Contains("복구"), "Corrupt settings displays recovery warning");
        Check(Element(1200).AsListBox().Items.Length == 5 && Directory.GetFiles(Data, "settings.corrupt.*.json").Length == 1, "Corrupt settings retained for diagnosis and backup restored");
        NativeButton(1316);
        Check(JsonNode.DeepEquals(backup, Settings()), "Recovery preserves full last-good layout");
        Quit(app!); app = null;
    }
    static void Options(ref App? app, string exe)
    {
        for (int kind = 5; kind < 9; kind++) { NativeCombo(1201, kind); NativeButton(1202); }
        for (int kind = 0; kind < 9; kind++)
        {
            SelectWidget(kind); NativeCombo(1300, 0); NativeCombo(1302, kind + 1); NativeButton(1316);
            var original = Settings().DeepClone();
            foreach (int scale in new[] { 0, 1, 2 })
            {
                NativeCombo(1301, scale); NativeButton(1316);
                int percent = new[] { 100, 150, 200 }[scale];
                var state = State(); var widget = state["windows"]![kind]!; var dpi = widget["dpi"]!.GetValue<int>();
                Check(Settings()["widgets"]![kind]!["scale"]!.GetValue<int>() == percent && widget["width"]!.GetValue<int>() == 160 * percent / 100 * dpi / 96 && widget["height"]!.GetValue<int>() == widget["width"]!.GetValue<int>(), $"Kind {kind}: scale {percent} saved and rendered");
            }
            NativeCombo(1301, 0); NativeButton(1316);
            var current = Settings();
            Check(current["widgets"]!.AsArray().Where((_, i) => i != kind).Select(w => w!.ToJsonString()).SequenceEqual(original["widgets"]!.AsArray().Where((_, i) => i != kind).Select(w => w!.ToJsonString())), $"Kind {kind}: edits preserve every other widget");
        }
        foreach (var option in new[] { (0, 1600, "twelveHour"), (1, 1601, "fahrenheit"), (6, 1602, "networkBytes"), (7, 1603, "binaryDisk"), (8, 1604, "weekStart") })
        {
            Tab(0); SelectWidget(option.Item1); Tab(3); Element(option.Item2).AsCheckBox().IsChecked = true; NativeButton(1620);
            var field = Settings()["widgets"]![option.Item1]![option.Item3]!;
            Check(option.Item3 == "weekStart" ? field.GetValue<int>() == 1 : field.GetValue<bool>(), "Display option persisted: " + option.Item3);
            var view = State()["windows"]![option.Item1]!;
            var optionSummary = Automation.FromHandle(WidgetHandle(view)).Name;
            if (option.Item1 == 0) Check(optionSummary.Contains("오전") || optionSummary.Contains("오후"), "Clock accessible time uses selected twelve-hour format");
            if (option.Item1 == 1) Check(optionSummary.Contains("°F"), "Weather accessible temperature uses selected Fahrenheit unit");
            if (option.Item1 == 6) Check(optionSummary.Contains("KiB/s") || optionSummary.Contains("MiB/s"), "Network accessible rates use selected byte units");
            if (option.Item1 == 7) Check(optionSummary.Contains("GiB") || optionSummary.Contains("TiB"), "Disk accessible space uses selected binary units");
            Tab(0); SelectWidget((option.Item1 + 1) % 9); Tab(3);
            Check(Element(option.Item2).AsCheckBox().IsChecked == false, "Display option isolated to selected widget: " + option.Item3);
        }
        Tab(0); SelectWidget(5); Tab(3);
        for (int i = 0; i < 5; i++) Element(1610 + i).AsCheckBox().IsChecked = i % 2 == 0;
        NativeButton(1620);
        Check(Settings()["widgets"]![5]!["systemFields"]!.AsArray().Select(v => v!.GetValue<bool>()).SequenceEqual(new[] { true, false, true, false, true }), "System field selection persisted");
        Tab(0); SelectWidget(0);
        Element(1310).AsCheckBox().IsChecked = true; NativeButton(1316);
        var clock = State()["windows"]![0]!; var summary = Automation.FromHandle(WidgetHandle(clock)).Name;
        Thread.Sleep(1500);
        Check(Automation.FromHandle(WidgetHandle(clock)).Name != summary, "Clock seconds change rendered accessible time");
        Element(1310).AsCheckBox().IsChecked = false; NativeButton(1316);
        Element(1309).AsCheckBox().IsChecked = true; NativeButton(1316);
        Check((State()["windows"]![0]!["extendedStyle"]!.GetValue<long>() & 8) != 0, "Always-on-top sets native topmost state");
        Element(1309).AsCheckBox().IsChecked = false; NativeButton(1316);
        Check((State()["windows"]![0]!["extendedStyle"]!.GetValue<long>() & 8) == 0, "Disabling always-on-top clears native topmost state");
        VisibleWidgets("topmost-return", 9, true);
        var overrides = Settings()["widgets"]!.DeepClone(); Tab(2);
        for (int theme = 0; theme < 9; theme++)
        {
            NativeCombo(1500, theme); NativeButton(1501);
            Check(Settings()["theme"]!.GetValue<int>() == theme && JsonNode.DeepEquals(overrides, Settings()["widgets"]), "Global theme preserves per-widget overrides: " + theme);
        }
        Tab(0); SelectWidget(8); NativeCombo(1300, 3); NativeButton(1316);
        NativeButton(1319); var next = Settings()["widgets"]![8]!["source"]!.GetValue<string>();
        NativeButton(1317); Check(Settings()["widgets"]![8]!["source"]!.GetValue<string>() != next, "Calendar previous month reverses next month");
        NativeButton(1318); Check(Settings()["widgets"]![8]!["source"]!.GetValue<string>() == "", "Calendar today clears month override");
        var saved = Settings().DeepClone(); Restart(ref app, exe);
        Check(JsonNode.DeepEquals(saved, Settings()), "All display, theme and scale options survive restart");
        VisibleWidgets("options-restarted", 9, true);
        Quit(app!); app = null;
    }
    static void ClockLayout(ref App? app)
    {
        for (int i = 0; i < 5; i++) NativeButton(1203);
        NativeCombo(1201, 0); NativeButton(1202);
        for (int period = 0; period < 2; period++)
        {
            Tab(3); Element(1600).AsCheckBox().IsChecked = period == 1; NativeButton(1620); Tab(0);
            for (int size = 0; size < 4; size++)
                for (int seconds = 0; seconds < 2; seconds++)
                {
                    NativeCombo(1300, size); Element(1310).AsCheckBox().IsChecked = seconds == 1; NativeButton(1316);
                    var state = State(); var summary = Automation.FromHandle(WidgetHandle(state["windows"]![0]!)).Name;
                    Check((summary.Contains("오전") || summary.Contains("오후")) == (period == 1), $"Clock size {size}: period option reflected in displayed time");
                    VisibleWidgets($"clock-size-{size}-period-{period}-seconds-{seconds}", 1, true);
                }
        }
        Quit(app!); app = null;
    }
    static void Placement(ref App? app, string exe)
    {
        NativeButton(1316);
        var imported = Settings().DeepClone();
        var template = imported["widgets"]![0]!.DeepClone();
        template["size"] = 3; template["monitor"] = "disconnected-test-monitor";
        template["x"] = 16; template["y"] = 16;
        imported["widgets"] = new JsonArray(Enumerable.Range(0, 64).Select(i => { var w = template.DeepClone(); w["id"] = "relocated-" + i; return w; }).ToArray());
        var file = Path.Combine(Data, "relocated.json");
        File.WriteAllText(file, imported.ToJsonString()); Tab(2); FileDialog(1504, file);
        var visible = State()["windows"]!.AsArray().Where(w => w!["placed"]!.GetValue<bool>()).Select(w => w!["id"]!.GetValue<string>()).ToHashSet();
        Check(visible.Count > 0 && visible.Count < 64, "Disconnected-monitor layout exposes parked widgets when screens are full");
        imported["widgets"] = new JsonArray(imported["widgets"]!.AsArray().Where(w => visible.Contains(w!["id"]!.GetValue<string>())).Select(w => w!.DeepClone()).ToArray());
        File.WriteAllText(file, imported.ToJsonString()); FileDialog(1504, file);
        VisibleWidgets("relocated-full", visible.Count);
        Tab(0); NativeCombo(1201, 2);
        bool noSpace = false;
        for (int i = 0; i < 64 - visible.Count; i++)
        {
            var before = Settings().DeepClone(); NativeButton(1202);
            if (JsonNode.DeepEquals(before, Settings()))
            {
                Check(Element(1999).Name.Contains("빈 공간"), "Full-screen add reports no space without altering saved widgets");
                noSpace = true; break;
            }
            VisibleWidgets("relocated-added-" + i, visible.Count + i + 1);
            Check(Settings()["widgets"]!.AsArray().Where(w => visible.Contains(w!["id"]!.GetValue<string>())).All(w => w!["monitor"]!.GetValue<string>() == "disconnected-test-monitor"), "Adding preserves original disconnected monitor assignments");
        }
        Check(noSpace, "Visible placement capacity reached before the 64-widget limit");
        var saved = Settings().DeepClone(); Restart(ref app, exe);
        Check(JsonNode.DeepEquals(saved, Settings()), "Temporary placements preserve saved originals through restart");
        VisibleWidgets("relocated-restarted", Ids(saved).Length);
        Quit(app!); app = null;
    }
}
