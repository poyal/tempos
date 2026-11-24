using System.Diagnostics;
using FlaUI.Core.AutomationElements;
using System.Text.Json.Nodes;
using App = FlaUI.Core.Application;

internal static partial class Program
{
    static string PrepareUpdateInstall(string source)
    {
        string installed = Directory.CreateDirectory(Path.Combine(Data, "installed")).FullName;
        string stage = Directory.CreateDirectory(Path.Combine(Data, "candidate")).FullName;
        string binary = Path.GetDirectoryName(source)!;
        void Payload(string dir, string app, string marker)
        {
            File.Copy(app, Path.Combine(dir,"Tempos.TestHost.exe"));
            File.WriteAllText(Path.Combine(dir, marker), marker);
            var files = Directory.GetFiles(dir).Select(p => new { path = Path.GetFileName(p), sha256 = Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(p))) }).ToList();
            File.WriteAllText(Path.Combine(dir,"tempos-files.json"), System.Text.Json.JsonSerializer.Serialize(files));
        }
        Payload(installed, source, "obsolete.txt");
        Payload(stage, Path.Combine(binary,"Tempos.UpdateCandidate.exe"), "added.txt");
        string runtime = Path.Combine(binary,"clang_rt.asan_dynamic-x86_64.dll");
        if (File.Exists(runtime)) File.Copy(runtime,Path.Combine(installed,Path.GetFileName(runtime)));
        File.WriteAllText(Path.Combine(installed,"user-note.txt"),"사용자 파일");
        System.IO.Compression.ZipFile.CreateFromDirectory(stage,Path.Combine(Data,"update-fixture.zip"));
        return Path.Combine(installed,"Tempos.TestHost.exe");
    }

    static void Updates(ref App? app, string exe)
    {
        NativeButton(1316);
        var saved = Settings().DeepClone();
        var version = FileVersionInfo.GetVersionInfo(exe).ProductVersion!;
        Tab(4);
        JsonObject UpdateState() => State()["update"]!.AsObject();
        JsonObject AwaitUpdate()
        {
            var deadline = DateTime.UtcNow.AddSeconds(12);
            while (DateTime.UtcNow < deadline)
            {
                var state = UpdateState();
                if (!state["busy"]!.GetValue<bool>()) return state;
                Thread.Sleep(50);
            }
            throw new Exception("Update state did not settle");
        }
        void Scenario(string check = "normal", string download = "normal", string? latest = "99.0.0", int installerExit = 0)
        {
            File.WriteAllText(Path.Combine(Data, "update-scenario.json"), new JsonObject {
                ["version"] = latest ?? version, ["check"] = check, ["download"] = download, ["installerExit"] = installerExit
            }.ToJsonString());
        }
        JsonObject CheckUpdate(string check = "normal", string download = "normal", string? latest = "99.0.0", int installerExit = 0)
        {
            Scenario(check, download, latest, installerExit);
            NativeButton(1702); return AwaitUpdate();
        }
        Check(AwaitUpdate()["phase"]!.GetValue<int>() == 2, "Startup checks latest version through mocked HTTP boundary");
        Check(Element(1700).Name.Contains(version) && Element(1700).Name.Contains("Windows x64"), "About shows executable resource version and Windows architecture");
        foreach (var (id, target) in new[] {(1706,"https://github.com/poyal/tempos"),(1707,"https://github.com/poyal/tempos/issues"),
            (1708,"https://github.com/poyal/tempos/blob/main/README.md"),(1712,"mailto:poyal.work@gmail.com")})
        {
            NativeButton(id);
            var link=JsonNode.Parse(File.ReadAllText(Path.Combine(Data,"about-link.json")))!;
            Check(link["mode"]!.GetValue<string>() == "mock" && link["url"]!.GetValue<string>() == target, "About link has a fixed trusted target without launching a browser: " + id);
        }
        var available=CheckUpdate();
        Check(available["phase"]!.GetValue<int>() == 3 && IsWindowEnabled(GetDlgItem(Window.Properties.NativeWindowHandle,1703)), "New stable Windows installer enables installation");
        NativeButton(1705);
        Check(JsonNode.Parse(File.ReadAllText(Path.Combine(Data,"about-link.json")))!["url"]!.GetValue<string>() == "https://github.com/poyal/tempos/releases/tag/v99.0.0", "Release notes target the checked version");
        CaptureSettings("about-update-available-example");
        foreach(var mode in new[]{"missing","bad-url","bad-digest"})
        {
            CheckUpdate(mode);
            Check(!IsWindowEnabled(GetDlgItem(Window.Properties.NativeWindowHandle,1703)) && IsWindowEnabled(GetDlgItem(Window.Properties.NativeWindowHandle,1705)), "Untrusted or missing installer disables install but keeps release notes: "+mode);
        }
        foreach(var mode in new[]{"malformed","draft","prerelease","quota","timeout","server-error"})
            Check(CheckUpdate(mode)["phase"]!.GetValue<int>() == 6 && !IsWindowEnabled(GetDlgItem(Window.Properties.NativeWindowHandle,1703)), "Failed check clears install eligibility: "+mode);
        Check(CheckUpdate(latest:"0.0.1")["phase"]!.GetValue<int>() == 2 && !IsWindowEnabled(GetDlgItem(Window.Properties.NativeWindowHandle,1703)), "Older release cannot downgrade");
        Scenario("slow"); NativeButton(1702);
        Check(!IsWindowEnabled(GetDlgItem(Window.Properties.NativeWindowHandle,1702)) && IsWindowEnabled(GetDlgItem(Window.Properties.NativeWindowHandle,1704)), "Checking disables duplicate work and permits cancellation");
        NativeButton(1704); Check(AwaitUpdate()["phase"]!.GetValue<int>() == 0, "Check cancellation returns to idle");
        foreach(var mode in new[]{"bad-signature","corrupt","short","oversize","unsafe-redirect","redirect-loop","timeout"})
        {
            CheckUpdate(download:mode); NativeButton(1703);
            Check(AwaitUpdate()["phase"]!.GetValue<int>() == 6 && !app!.HasExited, "Rejected download keeps current app running: "+mode);
            Check(!Directory.GetFiles(Path.Combine(Data,"updates"),"*-update.zip",SearchOption.AllDirectories).Any(), "Rejected download leaves no executable: "+mode);
        }
        CheckUpdate(download:"slow"); NativeButton(1703);
        Check(UpdateState()["busy"]!.GetValue<bool>(), "Installer download is asynchronous");
        NativeButton(1704); Check(AwaitUpdate()["phase"]!.GetValue<int>() == 3 && !app!.HasExited, "Download cancellation preserves current process");
        Check(JsonNode.DeepEquals(saved,Settings()), "Update checks and failed downloads preserve all widget settings");
        CheckUpdate(download:"slow"); NativeButton(1703);
        Quit(app!); app=null;
        Check(!Directory.GetFiles(Path.Combine(Data,"updates"),"*-update.zip",SearchOption.AllDirectories).Any(), "Closing during download cancels work and deletes the partial file without an installer launch");
        app=Start(exe); Tab(4); AwaitUpdate();
        NativeButton(1713);
        Check(!Settings()["checkUpdatesOnStart"]!.GetValue<bool>(), "Startup update setting persists disabled state");
        Quit(app!); app=null; app=Start(exe); Tab(4);
        Check(UpdateState()["phase"]!.GetValue<int>() == 0, "Disabled startup check remains idle after restart");
        Tab(3); FileDialog(1503,Path.Combine(Data,"update-layout.json")); FileDialog(1504,Path.Combine(Data,"update-layout.json"));
        Check(!Settings()["checkUpdatesOnStart"]!.GetValue<bool>() && !File.ReadAllText(Path.Combine(Data,"update-layout.json")).Contains("checkUpdatesOnStart"), "Layout import/export preserves the local update preference");
        Tab(4); NativeButton(1713);
        Check(JsonNode.DeepEquals(saved,Settings()), "Restoring startup preference preserves original complete settings");

        string installDir=Path.GetDirectoryName(exe)!;
        for(int attempt=0;attempt<4;attempt++)
        {
            int installerExit=attempt==0?7:attempt==1?17:attempt==2?9:0;
            var resultPath=Path.Combine(Data,"update-result.json");
            if(File.Exists(resultPath))File.Delete(resultPath);
            CheckUpdate(download:"redirect",installerExit:installerExit==9?0:installerExit);
            NativeButton(1703);
            Check(AwaitUpdate()["phase"]!.GetValue<int>()==5 && !app!.HasExited,"Signed ZIP ready; user chooses restart");
            if(attempt==0)
            {
                Quit(app!); app=Start(exe); Tab(4);
                Check(AwaitUpdate()["phase"]!.GetValue<int>()==5,"Ready package survives a normal restart");
                File.WriteAllText(Path.Combine(installDir,"added.txt"),"foreign");
                NativeButton(1703);
                Check(!app.HasExited && File.ReadAllText(Path.Combine(installDir,"added.txt"))=="foreign" &&
                      !string.IsNullOrEmpty(UpdateState()["installError"]!.GetValue<string>()),"Unmanaged file collision detected before app shutdown");
                File.Delete(Path.Combine(installDir,"added.txt"));
            }
            using var lockedFile=installerExit==9
                ? File.Open(Path.Combine(installDir,"obsolete.txt"), FileMode.Open, FileAccess.Read, FileShare.Read) : null;
            int originalPid=app!.ProcessId;
            NativeButton(1703);
            Check(WaitExit(app,30000),"Verified update handoff exits only the owned TestHost");
            app.Dispose(); app=null;
            if(installerExit==17)
            {
                var interrupted=Path.Combine(Data,"test-update-interrupted");
                var until=DateTime.UtcNow.AddSeconds(30);
                while(!File.Exists(interrupted) && DateTime.UtcNow<until)Thread.Sleep(100);
                Check(File.Exists(interrupted),"Interrupted file replacement leaves a recoverable transaction");
                string interruptedJob=File.ReadAllText(Path.Combine(Data,"update-pending"));
                string helperPath=Path.Combine(Data,"updates",interruptedJob,"Tempos.UpdateTestRunner.exe");
                var exited=DateTime.UtcNow.AddSeconds(20);
                while(true)
                {
                    try { using var released=File.Open(helperPath,FileMode.Open,FileAccess.ReadWrite,FileShare.None); break; }
                    catch(IOException) when(DateTime.UtcNow<exited) { Thread.Sleep(50); }
                }
                using var trigger=Process.Start(new ProcessStartInfo(exe,$"--settings --data-dir \"{Data}\""){UseShellExecute=false})!;
                trigger.WaitForExit(10000);
            }
            JsonNode? result=null;
            var deadline=DateTime.UtcNow.AddSeconds(30);
            while(DateTime.UtcNow<deadline)
            {
                if(File.Exists(resultPath))
                {
                    try { result=JsonNode.Parse(File.ReadAllText(resultPath)); if(result?["restartedPid"]?.GetValue<int>()>0) break; }
                    catch(System.IO.IOException) { }
                }
                Thread.Sleep(100);
            }
            Check(result?["restartedPid"]?.GetValue<int>()>0,"Update helper restarts owned app after installer exit");
            int restartedPid=result!["restartedPid"]!.GetValue<int>();
            using(var process=Process.GetProcessById(restartedPid))
                Check(restartedPid!=originalPid && string.Equals(process.MainModule!.FileName,Path.GetFullPath(exe),StringComparison.OrdinalIgnoreCase),"Restart targets the original TestHost executable");
            app=App.Attach(restartedPid); ProcessId=restartedPid; Controls.Clear();
            for(int i=0;i<100;i++)
            {
                var handle=OwnedManagementWindow(restartedPid);
                if(handle!=0 && IsWindowVisible(handle) && GetDlgItem(handle,1700)!=0){Window=Automation.FromHandle(handle).AsWindow();break;}
                Thread.Sleep(100);
            }
            Check(Window.Properties.ProcessId.Value==restartedPid,"Reopened About window belongs to restarted app");
            Tab(4); AwaitUpdate();
            bool success=installerExit==0;
            Check(result["success"]!.GetValue<bool>()==success && result["previous"]!.GetValue<string>()==version,"Result recorded after verified startup or rollback");
            Check(FileVersionInfo.GetVersionInfo(exe).ProductVersion==(success?"99.0.0":version),"Running executable is the expected real version");
            Check(File.Exists(Path.Combine(installDir,"added.txt"))==success && File.Exists(Path.Combine(installDir,"obsolete.txt"))!=success,"Managed additions and removals follow installed version");
            Check(File.ReadAllText(Path.Combine(installDir,"user-note.txt"))=="사용자 파일","Unmanaged file preserved");
            Check(JsonNode.DeepEquals(saved,Settings()),"Rollbacks and successful update preserve settings");
            Check(Element(1714).Name.Contains(success?"완료했습니다":"복원했습니다"),"About displays the final installed or restored version");
        }
        Quit(app!); app=null;
    }
}
