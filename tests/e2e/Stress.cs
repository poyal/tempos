using FlaUI.Core.AutomationElements;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text.Json;
using App = FlaUI.Core.Application;

internal static partial class Program
{
    [DllImport("ntdll.dll")] static extern int NtQueryInformationProcess(nint process, int informationClass, nint buffer, uint size, out uint required);
    [DllImport("ntdll.dll")] static extern int NtQueryObject(nint handle, int informationClass, nint buffer, uint size, out uint required);
    [DllImport("kernel32.dll")] static extern bool DuplicateHandle(nint sourceProcess, nint source, nint targetProcess, out nint target, uint access, bool inherit, uint options);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(nint handle);
    [DllImport("kernel32.dll")] static extern uint GetThreadId(nint thread);
    [DllImport("kernel32.dll")] static extern bool GetExitCodeThread(nint thread, out uint code);
    [DllImport("kernel32.dll")] static extern int GetThreadDescription(nint thread, out nint description);
    [DllImport("kernel32.dll")] static extern nint LocalFree(nint memory);
    [DllImport("ntdll.dll")] static extern int NtQueryInformationThread(nint thread, int informationClass, out nint address, uint size, out uint returned);
    delegate bool WindowEnumeration(nint window, nint parameter);
    [DllImport("user32.dll")] static extern bool EnumWindows(WindowEnumeration callback, nint parameter);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(nint parent, WindowEnumeration callback, nint parameter);

    sealed record HandleEntry(long Value, uint TypeIndex, string Type, uint Access);
    sealed record StressResources(long Private, int Handles, uint Gdi, uint User, int AppThreadHandles = 0);
    sealed record ThreadEntry(long handle, uint threadId, string description, string? module, long offset, uint? exitCode, int queryStatus);

    static ThreadEntry[] ThreadInventory(Process process, List<HandleEntry> handles)
    {
        var result = new List<ThreadEntry>();
        var modules = process.Modules.Cast<ProcessModule>().ToArray();
        foreach (var handle in handles.Where(h => h.Type == "Thread"))
        {
            if (!DuplicateHandle(process.Handle, (nint)handle.Value, -1, out var copy, 0, false, 2)) continue;
            try
            {
                string description = "";
                if (GetThreadDescription(copy, out var text) >= 0)
                {
                    try { description = Marshal.PtrToStringUni(text) ?? ""; }
                    finally { LocalFree(text); }
                }
                // Read-only diagnostics for our own TestHost. The returned start address
                // contract is documented at learn.microsoft.com/windows/win32/api/winternl/nf-winternl-ntqueryinformationthread.
                int status = NtQueryInformationThread(copy, 9, out var address, (uint)IntPtr.Size, out _);
                var module = status >= 0 ? modules.FirstOrDefault(m => address.ToInt64() >= m.BaseAddress.ToInt64() &&
                    address.ToInt64() < m.BaseAddress.ToInt64() + m.ModuleMemorySize) : null;
                bool hasExitCode = GetExitCodeThread(copy, out uint exitCode);
                result.Add(new(handle.Value, GetThreadId(copy), description,
                    module?.ModuleName, module == null ? 0 : address.ToInt64() - module.BaseAddress.ToInt64(),
                    hasExitCode ? (uint?)exitCode : null, status));
            }
            finally { CloseHandle(copy); }
        }
        return result.ToArray();
    }

    static object[] OwnedWindowInventory()
    {
        var result = new List<object>();
        var seen = new HashSet<nint>();
        bool Add(nint window, nint unused)
        {
            uint thread = GetWindowThreadProcessId(window, out var owner);
            if (owner != ProcessId || !seen.Add(window)) return true;
            var name = new System.Text.StringBuilder(256); GetClassName(window, name, name.Capacity);
            result.Add(new { handle = window.ToInt64(), thread, className = name.ToString(), visible = IsWindowVisible(window) });
            return true;
        }
        EnumWindows((window, unused) => { Add(window, 0); EnumChildWindows(window, Add, 0); return true; }, 0);
        for (nint window = FindWindowEx(new nint(-3), 0, null, null); window != 0;
             window = FindWindowEx(new nint(-3), window, null, null))
        {
            Add(window, 0); EnumChildWindows(window, Add, 0);
        }
        return result.ToArray();
    }

    // Windows 8+ ProcessHandleInformation, layout from:
    // https://github.com/winsiderss/phnt/blob/master/ntpsapi.h
    // Query only object types: querying file/pipe object names can block.
    static List<HandleEntry> HandleInventory(Process process)
    {
        var entries = new List<HandleEntry>();
        var types = new Dictionary<uint, string>();
        uint size = 65536;
        for (;;)
        {
            nint buffer = Marshal.AllocHGlobal(checked((int)size));
            try
            {
                int status = NtQueryInformationProcess(process.Handle, 51, buffer, size, out uint required);
                if (status < 0)
                {
                    if (status != unchecked((int)0xC0000004) && status != unchecked((int)0xC0000023)) throw new Exception($"Handle snapshot NTSTATUS {status:X8}");
                    size = Math.Max(size * 2, required);
                    if (size > 16 * 1024 * 1024) throw new Exception("Handle snapshot unexpectedly large");
                    continue;
                }
                long count = Marshal.ReadIntPtr(buffer).ToInt64();
                int stride = 3 * IntPtr.Size + 16;
                if (count < 0 || 2 * IntPtr.Size + count * stride > size) throw new Exception("Invalid handle snapshot layout");
                for (int i = 0; i < count; i++)
                {
                    nint entry = buffer + 2 * IntPtr.Size + i * stride;
                    nint handle = Marshal.ReadIntPtr(entry);
                    uint access = unchecked((uint)Marshal.ReadInt32(entry, 3 * IntPtr.Size));
                    uint typeIndex = unchecked((uint)Marshal.ReadInt32(entry, 3 * IntPtr.Size + 4));
                    if (!types.TryGetValue(typeIndex, out string? type))
                    {
                        type = "TypeIndex:" + typeIndex;
                        if (DuplicateHandle(process.Handle, handle, -1, out nint copy, 0, false, 2))
                        {
                            nint typeBuffer = Marshal.AllocHGlobal(4096);
                            try
                            {
                                if (NtQueryObject(copy, 2, typeBuffer, 4096, out _) >= 0)
                                {
                                    int length = (ushort)Marshal.ReadInt16(typeBuffer);
                                    type = Marshal.PtrToStringUni(Marshal.ReadIntPtr(typeBuffer, IntPtr.Size), length / 2) ?? type;
                                    types[typeIndex] = type;
                                }
                            }
                            finally { Marshal.FreeHGlobal(typeBuffer); CloseHandle(copy); }
                        }
                    }
                    entries.Add(new(handle.ToInt64(), typeIndex, type, access));
                }
                return entries;
            }
            finally { Marshal.FreeHGlobal(buffer); }
        }
    }

    static void Stress(ref App? app)
    {
        var management = Window.Properties.NativeWindowHandle.Value;
        var add = GetDlgItem(management, 1202);
        var remove = GetDlgItem(management, 1203);
        var list = GetDlgItem(management, 1200);
        var apply = GetDlgItem(management, 1316);
        var edit = GetDlgItem(management, 1204);
        var opacity = GetDlgItem(management, 1306);
        var sizes = GetDlgItem(management, 1300);
        var scales = GetDlgItem(management, 1301);
        var themes = GetDlgItem(management, 1302);
        Check(new[] {add, remove, list, apply, edit, opacity, sizes, scales, themes}.All(h => h != 0),
              "Stress operates on existing native controls");
        using var process = Process.GetProcessById(app!.ProcessId);
        var watch = Stopwatch.StartNew();
        var trace = new List<object>();
        int cycles = 0;
        StressResources Capture(string phase)
        {
            // Read on the app's UI thread, after completed paint/input handlers.
            // An out-of-process GetGuiResources call can catch temporary paint objects.
            SendMessage(management, 0x111, 1505, 0);
            using var diagnostic = JsonDocument.Parse(File.ReadAllText(Path.Combine(Data, "diagnostics.json")));
            var root = diagnostic.RootElement;
            var resources = new StressResources(root.GetProperty("privateBytes").GetInt64(),
                root.GetProperty("handles").GetInt32(), root.GetProperty("gdiObjects").GetUInt32(),
                root.GetProperty("userObjects").GetUInt32());
            process.Refresh();
            var inventory = HandleInventory(process);
            var counts = inventory.GroupBy(h => h.Type).OrderBy(g => g.Key).ToDictionary(g => g.Key, g => g.Count());
            var threadDetails = ThreadInventory(process, inventory);
            if (threadDetails.Length != inventory.Count(h => h.Type == "Thread") || threadDetails.Any(t => t.queryStatus < 0 || t.module == null))
                throw new Exception("Cannot classify every owned thread handle for stress measurement");
            resources = resources with { AppThreadHandles = threadDetails.Count(t => t.module == "Tempos.TestHost.exe") };
            var sample = new { phase, seconds = watch.Elapsed.TotalSeconds, cycles, privateBytes = resources.Private, handles = resources.Handles, threads = process.Threads.Count, gdi = resources.Gdi, user = resources.User, counts, inventory,
                appThreadHandles = resources.AppThreadHandles, threadDetails, windows = OwnedWindowInventory() };
            trace.Add(sample);
            File.WriteAllText(Path.Combine(Data, "stress-handles.json"), JsonSerializer.Serialize(trace, new JsonSerializerOptions { WriteIndented = true }));
            Console.WriteLine($"STRESS TRACE {phase} cycles={cycles} private={sample.privateBytes} handles={sample.handles} threads={sample.threads} types={JsonSerializer.Serialize(counts)}");
            return resources;
        }
        List<StressResources> Stabilize(string phase)
        {
            var samples = new List<StressResources>();
            for (int i = 30; i <= 300; i += 30) { Thread.Sleep(30000); samples.Add(Capture(phase + "-" + i)); }
            return samples;
        }
        void Cycle()
        {
            SendMessage(add, 0xF5, 0, 0);
            if (SendMessage(list, 0x18B, 0, 0) != 6) throw new Exception("Stress add count");
            // Keep measurement native: querying Window.Properties here also exercises
            // the automation server's RPC worker pool on every style change.
            if (SendMessage(sizes, 0x14E, 0, 0) == -1 || SendMessage(scales, 0x14E, cycles % 3, 0) == -1 ||
                SendMessage(themes, 0x14E, cycles % 10, 0) == -1) throw new Exception("Stress combo selection failed");
            if (SendText(opacity, 0xC, 0, (cycles % 101).ToString()) == 0) throw new Exception("Stress transparency input failed");
            SendMessage(apply, 0xF5, 0, 0);
            var saved = Settings()["widgets"]!.AsArray().Last()!;
            if (saved["transparency"]!.GetValue<int>() != cycles % 101 || saved["scale"]!.GetValue<int>() != new[] { 100, 150, 200 }[cycles % 3] || saved["theme"]!.GetValue<int>() != cycles % 10 - 1)
                throw new Exception("Stress style changes did not persist");
            SendMessage(edit, 0xF5, 0, 0); SendMessage(edit, 0xF5, 0, 0);
            SendMessage(remove, 0xF5, 0, 0);
            if (SendMessage(list, 0x18B, 0, 0) != 5) throw new Exception("Stress remove count");
            cycles++;
        }
        Capture("initial");
        for (int i = 0; i < 100; i++) {
            Cycle(); Thread.Sleep(2);
            // Exercise remote-cache writes on the worker pool before the baseline,
            // rather than first creating thread-local runtime state at the 15-minute refresh.
            if (i % 20 == 19) { SendMessage(management, 0x111, 2004, 0); Thread.Sleep(100); }
        }
        Capture("warmup-cycles");
        var beforeWindow = Stabilize("before");
        var baseline = Capture("baseline");
        beforeWindow.Add(baseline);
        for (int i = 0; i < 1000; i++) { Cycle(); Thread.Sleep(2); if ((i + 1) % 100 == 0) Capture("measured-" + (i + 1)); }
        var afterWindow = Stabilize("after");
        var final = Capture("final");
        afterWindow.Add(final);
        // Windows thread-pool workers retire/reappear independently of the last
        // operation. Compare equal complete settling windows using their median
        // and peak. Minimum counts describe how aggressively Windows retired idle
        // workers, not steady or peak resource demand; retain them as diagnostics.
        long[] Distribution(Func<StressResources, long> value, List<StressResources> samples)
        {
            var ordered = samples.Select(value).Order().ToArray();
            return new[] { ordered[0], ordered[ordered.Length / 2], ordered[^1] };
        }
        var distributions = new[] {
            (Name: "privateBytes", Value: (Func<StressResources, long>)(r => r.Private), Limit: 2L * 1024 * 1024),
            (Name: "handles", Value: (Func<StressResources, long>)(r => r.Handles), Limit: 0L),
            (Name: "gdi", Value: (Func<StressResources, long>)(r => r.Gdi), Limit: 0L),
            (Name: "user", Value: (Func<StressResources, long>)(r => r.User), Limit: 0L),
            (Name: "appThreadHandles", Value: (Func<StressResources, long>)(r => r.AppThreadHandles), Limit: 0L)
        }.Select(metric => {
            var before = Distribution(metric.Value, beforeWindow);
            var after = Distribution(metric.Value, afterWindow);
            var growth = before.Zip(after, (a, b) => b - a).ToArray();
            return new { metric = metric.Name, statistics = new[] { "minimum", "median", "maximum" },
                before, after, growth, assertedStatistics = new[] { "median", "maximum" },
                limit = metric.Limit, passed = growth.Skip(1).All(v => v <= metric.Limit) };
        }).ToArray();
        var stress = new { cycles = 1000, warmupCycles = 100, warmupRefreshes = 5, warmupSeconds = 300, settleSeconds = 300, verifiedStyleSaves = cycles,
            endpoint = new { privateGrowth = final.Private - baseline.Private, handleGrowth = final.Handles - baseline.Handles,
                gdiGrowth = (long)final.Gdi - baseline.Gdi, userGrowth = (long)final.User - baseline.User },
            samplesPerWindow = beforeWindow.Count, distributions };
        File.WriteAllText(Path.Combine(Data, "stress.json"), JsonSerializer.Serialize(stress)); Console.WriteLine(JsonSerializer.Serialize(stress));
        Check(cycles == 1100, "Every stress cycle persists scale, theme and actual transparency");
        foreach (var distribution in distributions)
            Check(distribution.passed, "1000 add/remove/style/edit settling-window median and maximum " + distribution.metric + " growth <= " + distribution.limit);
        Check(final.AppThreadHandles <= baseline.AppThreadHandles, "Application thread handles return to baseline independently of Windows workers");
        VisibleWidgets("after-stress", 5, true);
        Quit(app); app = null;
    }
}
