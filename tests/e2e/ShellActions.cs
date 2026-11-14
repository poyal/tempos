using System.Runtime.InteropServices;
using System.Text.Json.Nodes;
using App = FlaUI.Core.Application;

internal static partial class Program
{
    static void ShellActions(ref App? app, string exe)
    {
        NativeButton(1316);
        var handle = Window.Properties.NativeWindowHandle;
        var saved = Settings().DeepClone();
        SendMessage(handle, 0x111, 2002, 0);
        Check(OwnedWindow("Tempos.Grid") != 0, "Tray edit command creates grid");
        SendMessage(handle, 0x112, 0xF060, 0);
        Check(!IsWindowVisible(handle) && OwnedWindow("Tempos.Grid") == 0,
              "System close command hides settings and ends editing");
        SendMessage(handle, 0x111, 2001, 0);
        Check(IsWindowVisible(handle) && OwnedWindow("Tempos.Grid") == 0, "Tray settings command reopens without editing");
        SendMessage(handle, 0x111, 2003, 0);
        Check(State()["windows"]!.AsArray().All(w => !w!["visible"]!.GetValue<bool>()), "Tray hide command hides all widgets");
        SendMessage(handle, 0x111, 2003, 0);
        SendMessage(handle, 0x111, 2004, 0);
        Check(JsonNode.DeepEquals(saved, Settings()), "Tray show and refresh preserve layout");
        Tab(1); NativeButton(1402);
        Check(Element(1999).Name.Contains("목업"), "Mock weather information action does not open external website");

        dynamic shell = Activator.CreateInstance(Type.GetTypeFromProgID("Shell.Application")!)!;
        var before = new HashSet<long>();
        dynamic windows = shell.Windows();
        try
        {
            foreach (dynamic window in windows)
            {
                try { before.Add((long)window.HWND); }
                finally { Marshal.FinalReleaseComObject(window); }
            }
            Tab(2); NativeButton(1506);
            bool found = false;
            for (int attempt = 0; attempt < 100 && !found; attempt++)
            {
                Thread.Sleep(100);
                foreach (dynamic window in windows)
                {
                    try
                    {
                        string path;
                        try { path = (string)window.Document.Folder.Self.Path; }
                        catch { continue; }
                        if (!Path.IsPathFullyQualified(path)) continue;
                        if (!string.Equals(Path.GetFullPath(path), Path.GetFullPath(Data), StringComparison.OrdinalIgnoreCase)) continue;
                        found = true;
                        // Never close a pre-existing Explorer window or a reused user tab.
                        if (!before.Contains((long)window.HWND)) window.Quit();
                        break;
                    }
                    finally { Marshal.FinalReleaseComObject(window); }
                }
            }
            Check(found, "Data folder action opens this run's isolated folder in Explorer");
        }
        finally { Marshal.FinalReleaseComObject(windows); Marshal.FinalReleaseComObject(shell); }
        SendMessage(handle, 0x111, 2005, 0);
        Check(WaitExit(app!, 10000), "Tray exit command terminates owned process");
        app!.Dispose(); app = null;
        app = Start(exe);
        Check(JsonNode.DeepEquals(saved, Settings()), "Tray exit/restart preserves complete settings");
        Quit(app); app = null;
    }
}
