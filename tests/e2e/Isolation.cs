using FlaUI.Core.AutomationElements;
using Microsoft.Win32;
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using App = FlaUI.Core.Application;

internal static partial class Program
{
    sealed class InstalledProfileSnapshot
    {
        readonly string root = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Tempos");
        readonly string[] files = ["settings.json", "settings.backup.json"];
        readonly string[] hashes;
        readonly object? startup;
        static object? ReadStartup()
        {
            using var key = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Run");
            return key?.GetValue("Tempos");
        }
        string Hash(string name) => File.Exists(Path.Combine(root,name))
            ? Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(Path.Combine(root,name)))) : "absent";
        public InstalledProfileSnapshot() { hashes = files.Select(Hash).ToArray(); startup = ReadStartup(); }
        public bool Verify()
        {
            bool same = hashes.SequenceEqual(files.Select(Hash)) && Equals(startup, ReadStartup());
            Results.Add(new { name = "Installed layout, backup and startup registration remain unchanged", passed = same });
            Console.WriteLine((same ? "PASS" : "FAIL") + " Installed layout, backup and startup registration remain unchanged");
            return same;
        }
    }
    static void Isolation(ref App? app, string exe)
    {
        var realProfile = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Tempos");
        foreach (var arguments in new[] { "--settings", $"--data-dir \"{realProfile}\"", $"--data-dir \"{realProfile}\\.\\nested\\..\"" })
        {
            using var rejected = Process.Start(new ProcessStartInfo(exe, arguments) { UseShellExecute = false, WindowStyle = ProcessWindowStyle.Hidden })!;
            Check(rejected.WaitForExit(5000) && rejected.ExitCode == 64, "TestHost rejects missing or installed profile before app startup");
        }
        var firstWindow = Window.Properties.NativeWindowHandle.Value;
        string Class(nint hwnd) { var text = new StringBuilder(256); GetClassName(hwnd,text,text.Capacity); return text.ToString(); }
        var otherData = Path.Combine(Data,"another-profile"); Directory.CreateDirectory(otherData);
        File.WriteAllText(Path.Combine(otherData,"settings.json"), "{\"version\":1,\"widgets\":[]}");
        using var other = App.Launch(new ProcessStartInfo(exe, $"--data-dir \"{otherData}\" --settings") { UseShellExecute = false, WindowStyle = ProcessWindowStyle.Hidden });
        nint otherWindow = 0;
        try
        {
            // Hidden launch still creates a native top-level window. Enumerate by PID.
            for (int attempt = 0; attempt < 100 && otherWindow == 0; attempt++)
            {
                for (nint candidate = FindWindowEx(0,0,null!,null); candidate != 0; candidate = FindWindowEx(0,candidate,null!,null))
                {
                    GetWindowThreadProcessId(candidate,out var owner);
                    if (owner == other.ProcessId && Class(candidate).StartsWith("Tempos.TestHost.Management."))
                    { otherWindow = candidate; break; }
                }
                if (otherWindow == 0) Thread.Sleep(100);
            }
            Check(otherWindow != 0 && Class(firstWindow).StartsWith("Tempos.TestHost.Management.") && Class(otherWindow) != Class(firstWindow),
                  "Concurrent test profiles have separate management window classes");
            ShowWindow(firstWindow, 0);
            using var duplicate = Process.Start(new ProcessStartInfo(exe, $"--data-dir \"{Data}\" --settings") { UseShellExecute = false, WindowStyle = ProcessWindowStyle.Hidden })!;
            bool completed = duplicate.WaitForExit(5000);
            for (int attempt = 0; attempt < 50 && !IsWindowVisible(firstWindow); attempt++) Thread.Sleep(100);
            Console.WriteLine($"Duplicate exit={(completed ? duplicate.ExitCode : -1)}, matchingWindowVisible={IsWindowVisible(firstWindow)}");
            Check(completed && duplicate.ExitCode == 0 && IsWindowVisible(firstWindow),
                  "Duplicate test launch activates only its matching profile window");
            Check(!app!.HasExited && !other.HasExited, "Independent test profiles remain running");
        }
        finally
        {
            if (otherWindow != 0) PostMessage(otherWindow,0x111,1507,0);
            if (!WaitExit(other,5000)) other.Kill();
        }
        Quit(app!); app = null;
    }
}
