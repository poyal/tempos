using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using App = FlaUI.Core.Application;

internal static partial class Program
{
    // Real elapsed time, native windows, storage and restarts. External APIs stay mocked.
    static void Soak(ref App? app, string exe)
    {
        using var desktop = new SoakDesktopLease();
        NativeButton(1316);
        var watch = Stopwatch.StartNew();
        const int durationSeconds = 7200;
        int cycles = 0, restarts = 0;
        var samples = new List<object>();
        var baselines = new Dictionary<int, (int Handles, uint Gdi, uint User)>();
        while (watch.Elapsed.TotalSeconds < durationSeconds)
        {
            desktop.Verify();
            Tab(0);
            int count = Settings()["widgets"]!.AsArray().Count;
            NativeCombo(1201, cycles % 9); NativeButton(1202);
            Check(Settings()["widgets"]!.AsArray().Count == count + 1, "Soak adds widget kind " + cycles % 9);
            NativeCombo(1300, 0); NativeCombo(1301, cycles % 3); NativeCombo(1302, cycles % 10);
            NativeButton(1316);
            var saved = Settings().DeepClone();
            NativeButton(1204);
            Check(OwnedWindow("Tempos.Grid") != 0, "Soak edit grid exists");
            var state = State();
            var widget = state["windows"]!.AsArray().Last()!;
            var handle = WidgetHandle(widget);
            GetWindowThreadProcessId(handle, out var owner);
            Check(owner == app!.ProcessId, "Soak drag targets owned widget");
            NativeDragBatch(handle, 20, 20, 36, 36);
            var management = Window.Properties.NativeWindowHandle;
            SendMessage(management, 0x10, 0, 0);
            Check(OwnedWindow("Tempos.Grid") == 0 && !IsWindowVisible(management), "Soak close ends editing");
            ShowWindow(management, 5); Tab(0);
            NativeButton(1203);
            Check(Settings()["widgets"]!.AsArray().Count == count, "Soak removes added widget");
            if (cycles % 5 == 0)
            {
                NativeButton(1205);
                Check(State()["windows"]!.AsArray().All(w => !w!["visible"]!.GetValue<bool>()), "Soak hides every widget");
                Tab(0); NativeButton(1205);
            }
            saved = Settings().DeepClone();
            if (cycles > 0 && cycles % 60 == 0)
            {
                Restart(ref app, exe); restarts++;
                Check(JsonNode.DeepEquals(saved, Settings()), "Soak restart preserves complete layout");
            }
            using (var process = Process.GetProcessById(app!.ProcessId))
            {
                process.Refresh();
                uint gdi = GetGuiResources(process.Handle, 0), user = GetGuiResources(process.Handle, 1);
                if (!baselines.ContainsKey(process.Id)) baselines[process.Id] = (process.HandleCount, gdi, user);
                if (cycles % 30 == 0) {
                    var baseline = baselines[process.Id];
                    Check(process.PrivateMemorySize64 <= 64L * 1024 * 1024, "Soak private memory stays within 64 MiB");
                    Check(process.HandleCount <= baseline.Handles + 64 && gdi <= baseline.Gdi + 32 && user <= baseline.User + 32,
                          "Soak handles and GUI resources stay bounded within each process lifetime");
                }
                samples.Add(new { cycle = cycles, seconds = watch.Elapsed.TotalSeconds, pid = process.Id,
                    privateBytes = process.PrivateMemorySize64, handles = process.HandleCount,
                    gdi, user });
            }
            File.WriteAllText(Path.Combine(Data, "soak-progress.json"), JsonSerializer.Serialize(new {
                targetSeconds = durationSeconds, elapsedSeconds = watch.Elapsed.TotalSeconds, cycles, restarts,
                inputDesktopVerified = true, maximumVerificationGapSeconds = desktop.MaximumGapSeconds, samples }));
            Console.WriteLine($"SOAK {watch.Elapsed.TotalSeconds:F0}/{durationSeconds}s cycles={cycles} restarts={restarts}");
            cycles++;
            var next = watch.Elapsed.TotalSeconds + 10;
            while (watch.Elapsed.TotalSeconds < next && watch.Elapsed.TotalSeconds < durationSeconds) Thread.Sleep(500);
        }
        desktop.Verify();
        Check(watch.Elapsed.TotalSeconds >= durationSeconds && cycles >= 100 && restarts > 0,
              "Two hours of mixed interaction, persistence and process restarts completed");
        File.WriteAllText(Path.Combine(Data, "soak.json"), JsonSerializer.Serialize(new {
            targetSeconds = durationSeconds, actualSeconds = watch.Elapsed.TotalSeconds, cycles, restarts,
            processLifetimes = baselines.Count, inputDesktopVerified = true,
            maximumVerificationGapSeconds = desktop.MaximumGapSeconds,
            sleepPrevention = "scoped-display-and-system-request" }));
        VisibleWidgets("soak-final", Settings()["widgets"]!.AsArray().Count);
        Quit(app!); app = null;
    }

    // Keep the test awake without changing the user's power or lock settings.
    // A locked desktop or suspended runner invalidates elapsed-time UI coverage.
    sealed class SoakDesktopLease : IDisposable
    {
        readonly uint previous;
        readonly Stopwatch interval = Stopwatch.StartNew();
        public double MaximumGapSeconds { get; private set; }
        public SoakDesktopLease()
        {
            previous = SetThreadExecutionState(0x80000003);
            if (previous == 0) throw new InvalidOperationException("Cannot request an active soak session");
            try { Verify(); } catch { Dispose(); throw; }
        }
        public void Verify()
        {
            MaximumGapSeconds = Math.Max(MaximumGapSeconds, interval.Elapsed.TotalSeconds);
            if (interval.Elapsed.TotalSeconds > 60)
                throw new InvalidOperationException("Soak interrupted by suspension or excessive scheduling delay");
            var input = OpenInputDesktop(0, false, 1);
            if (input == 0) throw new InvalidOperationException("Soak input desktop is unavailable");
            try
            {
                var a = new StringBuilder(256); var b = new StringBuilder(256);
                if (!GetUserObjectInformation(input, 2, a, 512, out _) ||
                    !GetUserObjectInformation(GetThreadDesktop(GetCurrentThreadId()), 2, b, 512, out _) ||
                    a.ToString() != b.ToString())
                    throw new InvalidOperationException("Soak input desktop changed or locked");
            }
            finally { CloseDesktop(input); }
            interval.Restart();
        }
        public void Dispose() => SetThreadExecutionState(previous);
        [DllImport("kernel32.dll")] static extern uint SetThreadExecutionState(uint flags);
        [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
        [DllImport("user32.dll")] static extern nint GetThreadDesktop(uint thread);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        static extern bool GetUserObjectInformation(nint handle, int index, StringBuilder value, uint length, out uint needed);
    }
}
