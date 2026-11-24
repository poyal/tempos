using System.Diagnostics;
using System.Text.Json;
using System.Text.Json.Nodes;
using App = FlaUI.Core.Application;

internal static partial class Program
{
    static void DragPerformance(ref App? app)
    {
        for (int i = 0; i < 5; i++) NativeButton(1203);
        NativeCombo(1201, 0); NativeButton(1202); NativeButton(1204);
        void Measure(int widgets)
        {
        var view = State()["windows"]![0]!;
        var widget = WidgetHandle(view);
        int step = (int)Math.Round(8 * view["dpi"]!.GetValue<double>() / 96);
        GetWindowThreadProcessId(widget, out var owner);
        Check(owner == ProcessId, "Drag latency targets owned TestHost widget");
        var moves = new List<double>(); var drops = new List<double>(); var totals = new List<double>();
        var savedAt = File.GetLastWriteTimeUtc(Path.Combine(Data, "settings.json"));
        for (int i = 0; i < 1100; i++)
        {
            var measured = NativeDragBatch(widget, 20, 20, i % 2 == 0 ? 20 + step : 20 - step, 20);
            if (i >= 100) { moves.Add(measured.Move); drops.Add(measured.Drop); totals.Add(measured.Total); }
            if (i % 100 == 0) Console.WriteLine($"DRAG widgets={widgets} latency cycle {i}");
        }
        moves.Sort(); drops.Sort(); totals.Sort();
        var report = new { testClass = "native-message-batch-drag-performance", widgets, warmupCycles = 100, cycles = 1000,
            moveP95Milliseconds = moves[949], dropP95Milliseconds = drops[949],
            roundTripP95Milliseconds = totals[949],
            moveMaximumMilliseconds = moves[^1], dropMaximumMilliseconds = drops[^1] };
        File.WriteAllText(Path.Combine(Data, $"drag-performance-{widgets}.json"), JsonSerializer.Serialize(report));
        Check(report.moveP95Milliseconds <= 16.7, "Native move p95 fits 60 Hz frame budget");
        Check(report.dropP95Milliseconds <= 100, "Native drop including storage p95 <=100ms");
        Check(report.roundTripP95Milliseconds <= 100, "Native drag batch round-trip p95 <=100ms");
        var saved = Settings()["widgets"]![0]!;
        Check(saved["x"]!.GetValue<double>() == 16 && saved["y"]!.GetValue<double>() == 16,
              "1100 alternating drags do not accumulate position drift");
        if (widgets == 64)
            Check(File.GetLastWriteTimeUtc(Path.Combine(Data, "settings.json")) == savedAt,
                  "Rejected/no-op dense drops do not rewrite identical settings");
        }
        Measure(1);
        NativeButton(1204);
        var imported = Settings().DeepClone();
        var template = imported["widgets"]![0]!.DeepClone();
        imported["widgets"] = new JsonArray(Enumerable.Range(0, 64).Select(i => {
            var w = template.DeepClone(); w["id"] = "drag-capacity-" + i; w["size"] = 0;
            w["x"] = 16; w["y"] = 16; return w;
        }).ToArray());
        var file = Path.Combine(Data, "drag-capacity.json");
        File.WriteAllText(file, imported.ToJsonString()); Tab(3); FileDialog(1504, file); Tab(0);
        NativeButton(1204); Measure(64);
        NativeButton(1204); Quit(app!); app = null;
    }
}
