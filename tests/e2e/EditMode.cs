using FlaUI.Core.AutomationElements;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json.Nodes;
using App = FlaUI.Core.Application;

internal static partial class Program
{
    [DllImport("user32.dll")] static extern nint GetWindow(nint window, uint command);
    static void EditModeCycles(ref App? app, string exe)
    {
        var userFolder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Tempos");
        string Hash(string name) => File.Exists(Path.Combine(userFolder, name))
            ? Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(Path.Combine(userFolder, name)))) : "absent";
        var before = new[] { Hash("settings.json"), Hash("settings.backup.json") };
        var installedProcesses = Process.GetProcessesByName("Tempos").Select(p => p.Id).ToArray();
        NativeButton(1316);
        var saved = Settings().DeepClone();
        var identities = Ids(saved);
        for (int cycle = 0; cycle < 12; cycle++)
        {
            NativeButton(1204);
            var state = State();
            File.WriteAllText(Path.Combine(Data, "edit-cycle-" + cycle + ".json"), state.ToJsonString());
            var widgets = state["windows"]!.AsArray();
            Check(widgets.Select(w => w!["id"]!.GetValue<string>()).SequenceEqual(identities), "Toggle " + cycle + ": widget identities retained");
            Check(widgets.All(w => w!["visible"]!.GetValue<bool>() && w["placed"]!.GetValue<bool>() && IsWindowVisible(WidgetHandle(w))),
                "Toggle " + cycle + ": every widget visible");
            if (cycle % 2 == 0)
            {
                var grid = OwnedWindow("Tempos.Grid");
                Check(grid != 0 && IsWindowVisible(grid), "Toggle " + cycle + ": edit grid present");
                foreach (var widget in widgets)
                {
                    var handle = WidgetHandle(widget!);
                    bool aboveGrid = false;
                    for (var other = GetWindow(grid, 3); other != 0; other = GetWindow(other, 3))
                        if (other == handle) { aboveGrid = true; break; }
                    Check(aboveGrid, "Toggle " + cycle + ": widget " + widget!["kind"] + " above grid");
                }
                ShowWindow(Window.Properties.NativeWindowHandle, 0);
                try
                {
                    foreach (var widget in widgets)
                    {
                        var point = new Point(widget!["x"]!.GetValue<int>() + widget["width"]!.GetValue<int>() / 2,
                                              widget["y"]!.GetValue<int>() + widget["height"]!.GetValue<int>() / 2);
                        Check(WindowFromPoint(point) == WidgetHandle(widget), "Toggle " + cycle + ": actual screen hit belongs to edited widget " + widget["kind"]);
                        CaptureWidget("edit-toggle-" + cycle + "-kind-" + widget!["kind"], widget);
                    }
                }
                finally { ShowWindow(Window.Properties.NativeWindowHandle, 5); }
            }
            else
            {
                Check(OwnedWindow("Tempos.Grid") == 0, "Toggle " + cycle + ": edit grid removed");
                Check(widgets.All(w => ((w!["extendedStyle"]!.GetValue<long>() & 8) != 0) ==
                    saved["widgets"]!.AsArray().Single(s => s!["id"]!.GetValue<string>() == w["id"]!.GetValue<string>())!["topmost"]!.GetValue<bool>()),
                    "Toggle " + cycle + ": saved always-on-top preference restored");
            }
        }
        Check(JsonNode.DeepEquals(saved, Settings()), "Repeated edit toggles preserve complete test layout");
        Restart(ref app, exe);
        Check(JsonNode.DeepEquals(saved, Settings()), "Test layout survives restart after repeated edit toggles");
        SelectWidget(0); Element(1309).AsCheckBox().IsChecked = true; NativeButton(1316);
        var pinned = Settings().DeepClone();
        for (int cycle = 0; cycle < 4; cycle++)
        {
            NativeButton(1204);
            var state = State();
            Check(state["windows"]!.AsArray().All(w => ((w!["extendedStyle"]!.GetValue<long>() & 8) != 0) ==
                (cycle % 2 == 0 || w["id"]!.GetValue<string>() == identities[0])), "Mixed always-on-top widgets retain their correct band after toggle " + cycle);
        }
        Check(JsonNode.DeepEquals(pinned, Settings()), "Temporary edit layer does not persist always-on-top changes");
        Quit(app!); app = null;
        Check(before.SequenceEqual(new[] { Hash("settings.json"), Hash("settings.backup.json") }), "Installed layout and backup remain byte-for-byte unchanged");
        Check(installedProcesses.All(id => { try { return !Process.GetProcessById(id).HasExited; } catch { return false; } }),
              "Installed user app remains running during isolated E2E");
    }
}
