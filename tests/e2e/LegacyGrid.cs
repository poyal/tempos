using System.Text.Json.Nodes;
using App = FlaUI.Core.Application;

internal static partial class Program
{
    static void LegacyGrid(ref App? app, string exe)
    {
        NativeButton(1316);
        var monitor = State()["monitors"]![0]!;
        double dpi = monitor["dpi"]!.GetValue<double>();
        double areaWidth = (monitor["right"]!.GetValue<double>() - monitor["left"]!.GetValue<double>()) * 96 / dpi;
        double areaHeight = (monitor["bottom"]!.GetValue<double>() - monitor["top"]!.GetValue<double>()) * 96 / dpi;
        if (areaWidth < 536 || areaHeight < 280)
        {
            Skip("Legacy Slim 150% native layout", "Work area cannot fit two 504×112 DIP widgets; model coverage remains separate.");
            Quit(app!); app = null; return;
        }
        var imported = Settings().DeepClone();
        var first = imported["widgets"]![0]!.DeepClone();
        double x = Math.Min(800, Math.Floor((areaWidth - 520) / 8) * 8);
        first["id"] = "legacy-slim-first"; first["size"] = 1; first["scale"] = 150;
        first["monitor"] = monitor["id"]!.GetValue<string>(); first["x"] = x; first["y"] = 20;
        var second = first.DeepClone(); second["id"] = "legacy-slim-second"; second["y"] = 144;
        imported["widgets"] = new JsonArray(first, second);
        var file = Path.Combine(Data, "legacy-four-dip-layout.json");
        File.WriteAllText(file, imported.ToJsonString()); Tab(3); FileDialog(1504, file); Tab(0);
        void Verify(string phase)
        {
            var state = State(); var windows = state["windows"]!.AsArray();
            Check(windows.Count == 2 && windows.All(w => w!["placed"]!.GetValue<bool>() && w["visible"]!.GetValue<bool>()),
                  phase + ": both legacy widgets remain visible");
            var view = windows.Single(w => w!["id"]!.GetValue<string>() == "legacy-slim-second")!;
            int Pixel(double dip) => (int)Math.Round(dip * dpi / 96, MidpointRounding.AwayFromZero);
            Check(view["x"]!.GetValue<int>() == monitor["left"]!.GetValue<int>() + Pixel(x) &&
                  view["y"]!.GetValue<int>() == monitor["top"]!.GetValue<int>() + Pixel(152),
                  phase + ": expanded legacy neighbor moves only one grid cell, not to the screen origin");
        }
        Verify("import");
        Restart(ref app, exe); Verify("restart before apply");
        SelectWidget(1); NativeButton(1316);
        Check(Settings()["widgets"]![1]!["x"]!.GetValue<double>() == x && Settings()["widgets"]![1]!["y"]!.GetValue<double>() == 152,
              "Applying migrated widget saves the displayed nearby position");
        var saved = Settings().DeepClone(); Restart(ref app, exe); Verify("restart after apply");
        Check(JsonNode.DeepEquals(saved, Settings()), "Migrated layout is stable after apply and restart");
        Quit(app!); app = null;
    }
}
