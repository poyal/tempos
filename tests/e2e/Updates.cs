using System.Diagnostics;
using FlaUI.Core.AutomationElements;
using System.Text.Json.Nodes;
using App = FlaUI.Core.Application;

internal static partial class Program
{
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
        foreach(var mode in new[]{"corrupt","short","oversize","unsafe-redirect","redirect-loop","timeout"})
        {
            CheckUpdate(download:mode); NativeButton(1703);
            Check(AwaitUpdate()["phase"]!.GetValue<int>() == 6 && !app!.HasExited, "Rejected download keeps current app running: "+mode);
            Check(!Directory.GetFiles(Path.Combine(Data,"updates"),"*.exe",SearchOption.AllDirectories).Any(), "Rejected download leaves no executable: "+mode);
        }
        CheckUpdate(download:"slow"); NativeButton(1703);
        Check(UpdateState()["busy"]!.GetValue<bool>(), "Installer download is asynchronous");
        NativeButton(1704); Check(AwaitUpdate()["phase"]!.GetValue<int>() == 3 && !app!.HasExited, "Download cancellation preserves current process");
        Check(JsonNode.DeepEquals(saved,Settings()), "Update checks and failed downloads preserve all widget settings");
        CheckUpdate(download:"slow"); NativeButton(1703);
        Quit(app!); app=null;
        Check(!Directory.GetFiles(Path.Combine(Data,"updates"),"*.exe",SearchOption.AllDirectories).Any(), "Closing during download cancels work and deletes the partial file without an installer launch");
        app=Start(exe); Tab(4); AwaitUpdate();
        NativeButton(1713);
        Check(!Settings()["checkUpdatesOnStart"]!.GetValue<bool>(), "Startup update setting persists disabled state");
        Quit(app!); app=null; app=Start(exe); Tab(4);
        Check(UpdateState()["phase"]!.GetValue<int>() == 0, "Disabled startup check remains idle after restart");
        Tab(2); FileDialog(1503,Path.Combine(Data,"update-layout.json")); FileDialog(1504,Path.Combine(Data,"update-layout.json"));
        Check(!Settings()["checkUpdatesOnStart"]!.GetValue<bool>() && !File.ReadAllText(Path.Combine(Data,"update-layout.json")).Contains("checkUpdatesOnStart"), "Layout import/export preserves the local update preference");
        Tab(4); NativeButton(1713);
        Check(JsonNode.DeepEquals(saved,Settings()), "Restoring startup preference preserves original complete settings");

        for(int attempt=0;attempt<2;attempt++)
        {
            int installerExit=attempt==0?7:0;
            var resultPath=Path.Combine(Data,"update-result.json");
            if(File.Exists(resultPath))File.Delete(resultPath);
            CheckUpdate(download:"redirect",installerExit:installerExit);
            int originalPid=app!.ProcessId;
            NativeButton(1703);
            Check(WaitExit(app,20000), "Verified update handoff cleanly exits only the owned TestHost");
            app.Dispose(); app=null;
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
            Check(result["installerMode"]!.GetValue<string>()=="mock" && result["exit"]!.GetValue<int>()==installerExit && result["success"]!.GetValue<bool>()==(installerExit==0),"Real process handoff records mocked installer success/failure accurately");
            Check(Element(1714).Name.Contains(installerExit==0?"완료했습니다":"완료하지 못했습니다"),"About shows the installation outcome after restart");
            Check(JsonNode.DeepEquals(saved,Settings()),"Installer handoff and restart preserve complete settings");
            var folder=result["jobFolder"]!.GetValue<string>();
            Check(Path.GetFullPath(folder).StartsWith(Path.GetFullPath(Path.Combine(Data,"updates"))+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase),"Helper and mock installer artifacts stay inside this run's update directory");
            Check(JsonNode.Parse(File.ReadAllText(Path.Combine(folder,"mock-installer.json")))!["mock"]!.GetValue<bool>(),"Installer boundary uses a dedicated mock process, never the user installation");
        }
        Quit(app!); app=null;
    }
}
