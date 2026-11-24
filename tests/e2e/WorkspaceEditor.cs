using System.Text.Json.Nodes;
using FlaUI.Core.AutomationElements;
using App = FlaUI.Core.Application;

internal static partial class Program
{
    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
    struct GuiThreadInfo
    {
        public int Size, Flags;
        public nint Active, Focus, Capture, MenuOwner, MoveSize, Caret;
        public Rectangle CaretRect;
    }
    [System.Runtime.InteropServices.DllImport("user32.dll")]
    static extern bool GetGUIThreadInfo(uint thread, ref GuiThreadInfo info);
    static bool ThreadFocus(nint control)
    {
        var info = new GuiThreadInfo { Size = System.Runtime.InteropServices.Marshal.SizeOf<GuiThreadInfo>() };
        return GetGUIThreadInfo(GetWindowThreadProcessId(control,out _),ref info) && info.Focus == control;
    }
    static void WorkspaceEditor(ref App? app, string exe)
    {
        Check(!Live && !LiveApis, "Workspace editor uses example collectors and mock APIs");
        nint MainHandle() => Window.Properties.NativeWindowHandle.Value;
        nint Field(int id) => GetDlgItem(MainHandle(), id);
        void Text(int id, string value) => SendText(Field(id), 0xC, 0, value);
        void SelectLayout(int index)
        {
            SendMessage(Field(1802), 0x186, index, 0);
            SendMessage(MainHandle(), 0x111, 1802 | (1 << 16), Field(1802));
        }
        void ResolveNavigation(int result)
        {
            if (IsWindowVisible(Field(1003))) PostMessage(Field(1003), 0xF5, 0, 0);
            else
            {
                SendMessage(Field(1012), 0x14E, 1, 0);
                PostMessage(MainHandle(), 0x111, 1012 | (1 << 16), Field(1012));
            }
            nint dialog = 0;
            for (int i = 0; i < 80 && dialog == 0; i++) { Thread.Sleep(50); dialog = OwnedWindow("#32770"); }
            Check(dialog != 0, "Leaving a dirty editor presents the three choices");
            SendMessage(dialog, 0x400 + 102, result, 0); // TDM_CLICK_BUTTON
            for (int i = 0; i < 80 && IsWindowVisible(dialog); i++) Thread.Sleep(50);
            Check(!IsWindowVisible(dialog), "Draft dialog completes");
            if (result != 2) Check(SpinWait.SpinUntil(() => Element(1011).Name == "배치", 5000), "Navigation completed after resolving draft");
        }
        NativeButton(1316);
        var original = Settings();
        CaptureSettings("editor-original");
        var previewRect = Element(1212).BoundingRectangle;
        var origin = Point.Empty;
        ClientToScreen(MainHandle(), ref origin);
        int sampleX = previewRect.Right - origin.X - 24, sampleY = previewRect.Bottom - origin.Y - 24;
        Text(1324, "서울 시계");
        NativeCombo(1302, 6);
        SendMessage(MainHandle(), 0x111, 1302 | (1 << 16), Field(1302));
        Check(JsonNode.DeepEquals(original, Settings()) && IsWindowEnabled(Field(1325)), "Preview changes leave persisted widgets untouched");
        CaptureSettings("editor-draft");
        using (var before = new Bitmap(Path.Combine(Data,"editor-original.png")))
        using (var after = new Bitmap(Path.Combine(Data,"editor-draft.png")))
            Check(before.GetPixel(sampleX,sampleY) != after.GetPixel(sampleX,sampleY), "Preview pixels follow the unapplied theme");
        NativeButton(1325);
        SendMessage(MainHandle(),0x10,0,0);
        var count = State()["previewDraws"]!.GetValue<long>();
        Thread.Sleep(2400);
        Check(!IsWindowVisible(MainHandle()) && State()["previewDraws"]!.GetValue<long>()==count, "Closing settings stops preview painting");
        SendMessage(MainHandle(),0x111,2001,0);
        Check(Element(1324).AsTextBox().Text == "" && !IsWindowEnabled(Field(1325)), "Revert reloads the selected widget");
        Text(1324, "보류");
        ResolveNavigation(2);
        Check(Element(1011).Name == "위젯" && Element(1324).AsTextBox().Text == "보류", "Continue editing preserves the draft");
        ResolveNavigation(102);
        Check(Element(1011).Name == "배치" && JsonNode.DeepEquals(original, Settings()), "Discard leaves the last saved layout intact");
        Tab(0); Text(1324, "서울 시계"); ResolveNavigation(101);
        Check(Settings()["widgets"]![0]!["alias"]!.GetValue<string>() == "서울 시계", "Apply during navigation saves the alias");
        Tab(0); NativeButton(1206);
        var source = Settings()["widgets"]![0]!;
        var copy = Settings()["widgets"]!.AsArray().Last()!;
        Check(copy["id"]!.ToString() != source["id"]!.ToString() && copy["size"]!.ToString() == source["size"]!.ToString() &&
              copy["alias"]!.ToString().Contains("복사본") && (copy["x"]!.ToString() != source["x"]!.ToString() ||
              copy["y"]!.ToString() != source["y"]!.ToString() || copy["monitor"]!.ToString() != source["monitor"]!.ToString()),
              "Duplicate preserves appearance, allocates an independent ID and a free position");
        var first = Settings();
        Tab(3); Text(1804, "업무용"); NativeButton(1805);
        var work = Settings(); var workId = work["layoutId"]!.ToString();
        Check(work["layoutName"]!.ToString() == "업무용" && work["otherLayouts"]!.AsArray().Count == 1 &&
              !Ids(work).Intersect(Ids(first)).Any(), "A named copy is activated with independent widgets");
        CaptureSettings("layouts");
        Tab(0); NativeButton(1203); var edited = Settings();
        Tab(3); SelectLayout(1);
        using (var locked = new FileStream(Path.Combine(Data,"settings.json"), FileMode.Open, FileAccess.Read, FileShare.Read))
        {
            NativeButton(1807);
            Check(JsonNode.DeepEquals(edited,Settings()) && Element(1999).Name.Contains("저장 실패"), "Failed switch preserves the visible and stored layout");
        }
        NativeButton(1807);
        Check(JsonNode.DeepEquals(first["widgets"], Settings()["widgets"]), "Switching restores the previous layout exactly");
        SendMessage(MainHandle(), 0x111, 3001, 0); // Same command selected by the tray menu
        Check(Settings()["layoutId"]!.ToString() == workId && JsonNode.DeepEquals(edited["widgets"],Settings()["widgets"]), "Tray switch restores the latest edits");
        Restart(ref app,exe);
        Check(Settings()["layoutId"]!.ToString() == workId && State()["windows"]!.AsArray().Count == edited["widgets"]!.AsArray().Count,
              "Restart opens only the last active layout's windows");
        NativeButton(1316);
        foreach (var mode in new[] {"light","dark","contrast"})
        {
            File.WriteAllText(Path.Combine(Data,"settings-theme-example.txt"),mode);
            SendMessage(MainHandle(),0x31A,0,0);
            CaptureSettings("editor-theme-example-"+mode);
        }
        File.Delete(Path.Combine(Data,"settings-theme-example.txt"));
        SendMessage(MainHandle(),0x31A,0,0);
        SetWindowPos(MainHandle(),0,0,0,530,650,0x16);
        Check(IsWindowVisible(Field(1012)) && !IsWindowVisible(Field(1000)), "Narrow window uses compact navigation");
        Tab(3); CaptureSettings("layouts-narrow"); Tab(0);
        Element(1324).Focus(); PostMessage(Field(1324), 0x100, 9, 0);
        Check(SpinWait.SpinUntil(() => ThreadFocus(Field(1300)), 3000), "Tab follows the form from name to size");
        SendMessage(MainHandle(), 0x115, 7, 0); // SB_BOTTOM
        CaptureSettings("editor-narrow-scrolled");
        var portable=Path.Combine(Data,"portable.json");
        FileDialog(1503,portable);
        var file=JsonNode.Parse(File.ReadAllText(portable))!;
        Check(file["version"]!.GetValue<int>()==1 && file["otherLayouts"]==null && file["layoutId"]==null, "Export contains one portable layout");
        file["widgets"]=new JsonArray(); file["weatherProvider"]="kma"; File.WriteAllText(portable,file.ToJsonString());
        FileDialog(1504,portable);
        Check(Settings()["widgets"]!.AsArray().Count==0 && Settings()["weatherProvider"]!.ToString()=="kma" &&
              JsonNode.DeepEquals(Settings()["otherLayouts"],edited["otherLayouts"]), "Import replaces only the active layout and the global weather service");
        SelectLayout(1);
        Text(1804,work["layoutName"]!.ToString()); NativeButton(1806);
        Check(Element(1999).Name.Contains("다른 배치 이름"), "Duplicate layout names are rejected");
        Text(1804,"읽기용"); NativeButton(1806);
        Check(Settings()["otherLayouts"]![0]!["layoutName"]!.ToString()=="읽기용", "Renaming the inactive layout keeps its data");
        SelectLayout(1);
        PostMessage(Field(1808), 0xF5, 0, 0);
        nint confirm = 0;
        Check(SpinWait.SpinUntil(() => (confirm = OwnedWindow("#32770")) != 0,3000), "Removing an inactive layout asks for confirmation");
        PostMessage(GetDlgItem(confirm,6),0xF5,0,0); // IDYES on the owned test dialog
        Check(SpinWait.SpinUntil(() => Settings()["otherLayouts"]!.AsArray().Count == 0,5000) &&
              Settings()["layoutId"]!.ToString()==workId, "Removing an inactive layout preserves the active one");
        Quit(app!); app=null;
        // Start with an on-disk v1 profile inside this run's sandbox.
        var legacy = first.DeepClone().AsObject();
        legacy["version"] = 1;
        legacy.Remove("layoutId");
        legacy.Remove("layoutName");
        legacy.Remove("otherLayouts");
        foreach (var widget in legacy["widgets"]!.AsArray()) widget!.AsObject().Remove("alias");
        File.WriteAllText(Path.Combine(Data,"settings.json"),legacy.ToJsonString());
        app = Start(exe);
        NativeButton(1316);
        var migrated = Settings();
        Check(migrated["version"]!.GetValue<int>() == 2 && migrated["layoutName"]!.ToString() == "기본 배치" &&
              Ids(migrated).SequenceEqual(Ids(legacy)) && migrated["widgets"]![0]!["x"]!.ToString() == legacy["widgets"]![0]!["x"]!.ToString(),
              "Opening and saving a v1 profile preserves widget IDs and positions in the default layout");
        Restart(ref app,exe);
        Check(JsonNode.DeepEquals(migrated,Settings()),"Migrated settings survive a second launch");
        Quit(app!); app=null;
    }
}
