using FlaUI.Core.AutomationElements;
using Microsoft.Win32;
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json.Nodes;
using App = FlaUI.Core.Application;

internal static partial class Program
{
    static void InstallerLifecycle(string installer)
    {
        const string appKey = @"Software\Tempos\Installer";
        const string uninstallKey = @"Software\Microsoft\Windows\CurrentVersion\Uninstall\Tempos";
        const string runKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
        using var registry = RegistryKey.OpenBaseKey(RegistryHive.CurrentUser, RegistryView.Registry64);
        using var run = registry.CreateSubKey(runKey);
        var originalStartup = run.GetValue("Tempos");
        var originalStartupKind = originalStartup == null ? RegistryValueKind.String : run.GetValueKind("Tempos");
        var desktop = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "Tempos.lnk");
        var startMenu = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.StartMenu), "Programs", "Tempos");
        var installDir = Path.Combine(Data, "설치 테스트", "Tempos");
        var installedExe = Path.Combine(installDir, "Tempos.exe");
        var uninstaller = Path.Combine(installDir, "Uninstall Tempos.exe");
        var uninstallerCopy = Path.Combine(Data, "uninstall-test.exe");
        var userSettings = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Tempos", "settings.json");
        string Hash(string file) => File.Exists(file) ? Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(file))) : "absent";
        var originalSettingsHash = Hash(userSettings);
        var package = JsonNode.Parse(File.ReadAllText(Path.Combine(Root, "artifacts", "installer-package.json")))!;
        Check(Hash(installer) == package["sha256"]!.GetValue<string>(), "Installer under test matches build manifest");
        Check(System.Text.Encoding.UTF8.GetString(File.ReadAllBytes(installer)).Contains("requestedExecutionLevel level=\"asInvoker\""),
              "Installer manifest requests current-user execution without elevation");
        Check(registry.OpenSubKey(appKey) == null && registry.OpenSubKey(uninstallKey) == null,
              "Installer test does not replace an existing installed Tempos");
        Check(!File.Exists(desktop) && !Directory.Exists(startMenu), "Installer test owns only newly created shortcuts");
        Check(!Process.GetProcessesByName("Tempos").Any(), "No user Tempos process is running before installer lifecycle");
        Check(Path.GetFullPath(installDir).StartsWith(Path.GetFullPath(Data) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase),
              "Install and uninstall targets are within the isolated test workspace");
        App? active = null;
        int Run(string file, string arguments)
        {
            using var process = Process.Start(new ProcessStartInfo(file, arguments) { UseShellExecute = false, WindowStyle = ProcessWindowStyle.Hidden })!;
            if (!process.WaitForExit(30000)) { process.Kill(); throw new Exception("Installer command timed out"); }
            return process.ExitCode;
        }
        void Attach(App process, string title)
        {
            Controls.Clear(); ProcessId = process.ProcessId;
            for (int i = 0; i < 150; i++)
            {
                if (process.HasExited) throw new Exception("Process exited while waiting for " + title + ": " + process.ExitCode);
                if (title == "위젯 설정")
                {
                    var handle = OwnedWindow("Tempos.Management");
                    if (handle != 0)
                    {
                        ShowWindow(handle, 5);
                        Window = Automation.FromHandle(handle).AsWindow();
                        return;
                    }
                    Thread.Sleep(100);
                    continue;
                }
                var found = process.GetAllTopLevelWindows(Automation).FirstOrDefault(w => w.Title.Contains(title));
                if (found != null) { Window = found; return; }
                Thread.Sleep(100);
            }
            throw new Exception("Window unavailable: " + title);
        }
        void LaunchInstalled()
        {
            active = App.Launch(new ProcessStartInfo(installedExe, $"--data-dir \"{Data}\" --settings") {
                WorkingDirectory = Path.GetTempPath(), UseShellExecute = false, WindowStyle = ProcessWindowStyle.Hidden });
            Attach(active, "위젯 설정");
            Check(Element(1200).AsListBox().Items.Length == 1, "Installed production app opens isolated clock-only profile");
            Check(SendMessage(GetDlgItem(Window.Properties.NativeWindowHandle, 1314), 0x146, 0, 0) > 3000,
                  "Installed app finds bundled regions outside repository working directory");
        }
        void CloseOwnedApp()
        {
            if (active != null && !active.HasExited)
            {
                ProcessId = active.ProcessId;
                PostMessage(OwnedWindow("Tempos.Management"), 0x111, 1507, 0);
                Check(WaitExit(active, 10000), "Installed app exits cleanly through native command");
            }
            active?.Dispose(); active = null;
        }
        try
        {
            Check(Run(installer, "/S /allusers /D=" + installDir) == 2 && !File.Exists(installedExe),
                  "All-users install override is rejected without creating payload");
            var invalidDirectoryExit = Run(installer, "/S /D=" + Path.GetPathRoot(installDir));
            Console.WriteLine("Invalid-directory exit: " + invalidDirectoryExit);
            Check(invalidDirectoryExit != 0 && !File.Exists(Path.Combine(Path.GetPathRoot(installDir)!, "Tempos.exe")),
                  "Silent installation rejects a drive root");
            Check(Run(installer, "/S /D=" + Path.GetDirectoryName(userSettings)) == 6 && Hash(userSettings) == originalSettingsHash,
                  "Silent installation rejects the user data directory without changing settings");
            active = App.Launch(new ProcessStartInfo(installer, "/D=" + installDir) { UseShellExecute = false, WindowStyle = ProcessWindowStyle.Hidden });
            Attach(active, "Tempos");
            AutomationElement? edit = null;
            for (int i = 0; i < 100 && edit == null; i++)
            {
                edit = Window.FindAllDescendants(c => c.ByControlType(FlaUI.Core.Definitions.ControlType.Edit))
                    .FirstOrDefault(e => e.AsTextBox().Text == installDir);
                if (edit == null) Thread.Sleep(100);
            }
            Check(edit != null, "First installer page is a folder chooser retaining the requested Unicode path");
            Check(Window.FindAllDescendants(c => c.ByControlType(FlaUI.Core.Definitions.ControlType.RadioButton)).Length == 0,
                  "Installer has no all-users scope selector");
            // Demonstration text avoids publishing the current user's workspace path.
            edit!.AsTextBox().Text = @"C:\Apps\Tempos";
            CaptureSettings("installer-folder");
            edit!.AsTextBox().Text = installDir;
            SendMessage(GetDlgItem(Window.Properties.NativeWindowHandle, 1), 0xF5, 0, 0);
            AutomationElement? launch = null;
            for (int i = 0; i < 200 && launch == null; i++)
            {
                Thread.Sleep(100);
                launch = Window.FindAllDescendants(c => c.ByControlType(FlaUI.Core.Definitions.ControlType.CheckBox))
                    .FirstOrDefault(e => e.Name.Contains("Tempos 실행"));
            }
            Check(launch != null && File.Exists(installedExe), "Interactive installer reaches completion page");
            SendMessage(launch!.Properties.NativeWindowHandle, 0xF1, 0, 0);
            CaptureSettings("installer-complete");
            SendMessage(GetDlgItem(Window.Properties.NativeWindowHandle, 1), 0xF5, 0, 0);
            Check(WaitExit(active, 10000) && active.ExitCode == 0, "Interactive current-user installation completes");
            active.Dispose(); active = null;
            foreach (var file in package["files"]!.AsArray())
                Check(Hash(Path.Combine(installDir, file!["path"]!.GetValue<string>())) == file["sha256"]!.GetValue<string>(),
                      "Installed payload hash: " + file["path"]!.GetValue<string>());
            using (var registration = registry.OpenSubKey(uninstallKey))
                Check((string?)registration?.GetValue("InstallLocation") == installDir && (string?)registration?.GetValue("DisplayVersion") == package["version"]!.GetValue<string>(),
                      "Windows installed-app entry records version and exact path");
            Check(File.Exists(desktop) && File.Exists(Path.Combine(startMenu, "Tempos.lnk")), "Desktop and Start menu shortcuts installed");
            dynamic shell = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell")!)!;
            try
            {
                foreach (var shortcut in new[] {desktop, Path.Combine(startMenu, "Tempos.lnk")})
                {
                    dynamic link = shell.CreateShortcut(shortcut);
                    try { Check((string)link.TargetPath == installedExe && (string)link.Arguments == "--settings", "Shortcut targets installed executable and settings"); }
                    finally { System.Runtime.InteropServices.Marshal.FinalReleaseComObject(link); }
                }
            }
            finally { System.Runtime.InteropServices.Marshal.FinalReleaseComObject(shell); }
            Check(Run(installer, "/S /D=" + Path.Combine(Data,"another-install")) == 6 && !Directory.Exists(Path.Combine(Data,"another-install")),
                  "Existing installation cannot silently relocate to a different folder");
            // A production launch with only a clock needs no HTTP API or location.
            File.WriteAllText(Path.Combine(Data,"settings.json"), "{\"version\":1,\"theme\":4,\"weatherProvider\":\"open-meteo\",\"widgets\":[{\"id\":\"installer-clock\",\"kind\":0,\"size\":2}]}");
            File.WriteAllText(Path.Combine(installDir, "user-note.txt"), "preserve unrelated file");
            LaunchInstalled(); NativeButton(1316);
            Check(State()["windows"]!.AsArray().All(w => w!["kind"]!.GetValue<int>() == 0), "Installer startup smoke exercises no API widgets");
            var savedSettings = Hash(Path.Combine(Data, "settings.json"));
            File.Copy(uninstaller, uninstallerCopy, true);
            Check(Run(installer, "/S /D=" + installDir) == 10, "Update refuses to overwrite a running app");
            Check(Run(uninstallerCopy, "/S _?=" + installDir) == 10, "Uninstall refuses to remove a running app");
            CloseOwnedApp();
            Check(Run(installer, "/S /D=" + installDir) == 0, "Same-folder reinstall updates successfully");
            Check(Hash(installedExe) == package["executableSha256"]!.GetValue<string>(), "Reinstall retains the verified production executable");
            Check(Hash(Path.Combine(Data,"settings.json")) == savedSettings && File.Exists(Path.Combine(installDir,"user-note.txt")),
                  "Reinstall preserves saved profile and unrelated files");
            LaunchInstalled();
            Check(Settings()["theme"]!.GetValue<int>() == 4, "Installed app restart preserves theme and clock");
            CloseOwnedApp();
            File.Copy(uninstaller, uninstallerCopy, true);
            Check(Run(uninstallerCopy, "/S /allusers _?=" + installDir) == 2, "All-users uninstall override rejected");
            Check(Run(uninstallerCopy, "/S _?=" + Data) == 5, "Uninstaller rejects a different directory");
            run.SetValue("Tempos", '"' + installedExe + '"', RegistryValueKind.String);
            Check(Run(uninstallerCopy, "/S _?=" + installDir) == 0, "Current-user uninstaller completes");
            Check(!File.Exists(installedExe) && !File.Exists(uninstaller), "Uninstaller removes installed executable and itself");
            Check(registry.OpenSubKey(uninstallKey) == null && registry.OpenSubKey(appKey) == null, "Uninstaller removes its Windows registration");
            Check(!File.Exists(desktop) && !Directory.Exists(startMenu), "Uninstaller removes only its shortcuts");
            Check(run.GetValue("Tempos") == null, "Uninstall clears matching automatic-start registration");
            Check(File.ReadAllText(Path.Combine(installDir,"user-note.txt")) == "preserve unrelated file" && Hash(Path.Combine(Data,"settings.json")) == savedSettings,
                  "Uninstall preserves unrelated files and user configuration");
            Check(Hash(userSettings) == originalSettingsHash, "Real user Tempos settings untouched");
            // A later reinstall must not remove another copy's startup registration.
            Check(Run(installer, "/S /D=" + installDir) == 0, "Reinstall after removal succeeds");
            File.Copy(uninstaller, uninstallerCopy, true);
            run.SetValue("Tempos", "\"C:\\Unrelated\\Tempos.exe\"", RegistryValueKind.String);
            Check(Run(uninstallerCopy, "/S _?=" + installDir) == 0 && (string?)run.GetValue("Tempos") == "\"C:\\Unrelated\\Tempos.exe\"",
                  "Uninstall preserves another copy's automatic-start command");
        }
        catch (Exception error)
        {
            Console.Error.WriteLine("Installer lifecycle failure before cleanup: " + error);
            throw;
        }
        finally
        {
            try
            {
                if (active != null)
                {
                    if (!active.HasExited)
                    {
                        if (Process.GetProcessById(active.ProcessId).ProcessName == "Tempos")
                        {
                            ProcessId = active.ProcessId;
                            PostMessage(OwnedWindow("Tempos.Management"), 0x111, 1507, 0);
                            if (!WaitExit(active, 5000)) active.Kill();
                            active.Dispose(); active = null;
                        }
                        else { active.Kill(); active.Dispose(); active = null; }
                    }
                    else { active.Dispose(); active = null; }
                }
                if (File.Exists(uninstaller))
                {
                    File.Copy(uninstaller, uninstallerCopy, true);
                    if (Run(uninstallerCopy, "/S _?=" + installDir) != 0) throw new Exception("Installer cleanup failed");
                }
            }
            finally
            {
                if (originalStartup == null) run.DeleteValue("Tempos", false);
                else run.SetValue("Tempos", originalStartup, originalStartupKind);
            }
        }
    }
}
