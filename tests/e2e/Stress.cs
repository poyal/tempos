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

    sealed record HandleEntry(long Value, uint TypeIndex, string Type, uint Access);

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
        var add = Element(1202).Properties.NativeWindowHandle.Value;
        var remove = Element(1203).Properties.NativeWindowHandle.Value;
        var list = Element(1200).Properties.NativeWindowHandle.Value;
        var apply = Element(1316).Properties.NativeWindowHandle.Value;
        var edit = Element(1204).Properties.NativeWindowHandle.Value;
        var opacity = Element(1306).Properties.NativeWindowHandle.Value;
        using var process = Process.GetProcessById(app!.ProcessId);
        var watch = Stopwatch.StartNew();
        var trace = new List<object>();
        int cycles = 0;
        void Capture(string phase)
        {
            process.Refresh();
            var inventory = HandleInventory(process);
            var counts = inventory.GroupBy(h => h.Type).OrderBy(g => g.Key).ToDictionary(g => g.Key, g => g.Count());
            var sample = new { phase, seconds = watch.Elapsed.TotalSeconds, cycles, privateBytes = process.PrivateMemorySize64, handles = process.HandleCount, threads = process.Threads.Count, gdi = GetGuiResources(process.Handle, 0), user = GetGuiResources(process.Handle, 1), counts, inventory };
            trace.Add(sample);
            File.WriteAllText(Path.Combine(Data, "stress-handles.json"), JsonSerializer.Serialize(trace, new JsonSerializerOptions { WriteIndented = true }));
            Console.WriteLine($"STRESS TRACE {phase} cycles={cycles} private={sample.privateBytes} handles={sample.handles} threads={sample.threads} types={JsonSerializer.Serialize(counts)}");
        }
        void Stabilize(string phase)
        {
            for (int i = 30; i <= 300; i += 30) { Thread.Sleep(30000); Capture(phase + "-" + i); }
        }
        void Cycle()
        {
            SendMessage(add, 0xF5, 0, 0);
            if (SendMessage(list, 0x18B, 0, 0) != 6) throw new Exception("Stress add count");
            NativeCombo(1300, 0); NativeCombo(1301, cycles % 3); NativeCombo(1302, cycles % 10);
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
        for (int i = 0; i < 100; i++) { Cycle(); Thread.Sleep(2); }
        Capture("warmup-cycles");
        Stabilize("before");
        process.Refresh();
        long startPrivate = process.PrivateMemorySize64;
        int startHandles = process.HandleCount;
        uint startGdi = GetGuiResources(process.Handle, 0), startUser = GetGuiResources(process.Handle, 1);
        for (int i = 0; i < 1000; i++) { Cycle(); Thread.Sleep(2); if ((i + 1) % 100 == 0) Capture("measured-" + (i + 1)); }
        Stabilize("after");
        process.Refresh();
        var stress = new { cycles = 1000, warmupCycles = 100, warmupSeconds = 300, settleSeconds = 300, verifiedStyleSaves = cycles, privateGrowth = process.PrivateMemorySize64 - startPrivate, handleGrowth = process.HandleCount - startHandles, gdiGrowth = (long)GetGuiResources(process.Handle, 0) - startGdi, userGrowth = (long)GetGuiResources(process.Handle, 1) - startUser };
        File.WriteAllText(Path.Combine(Data, "stress.json"), JsonSerializer.Serialize(stress)); Console.WriteLine(JsonSerializer.Serialize(stress));
        Check(cycles == 1100, "Every stress cycle persists scale, theme and actual transparency");
        Check(stress.privateGrowth <= 2 * 1024 * 1024, "1000 add/remove/style/edit private growth <=2 MiB");
        Check(stress.gdiGrowth <= 0 && stress.userGrowth <= 0 && stress.handleGrowth <= 0, "1000 add/remove/style/edit GDI, USER and handles return to baseline");
        VisibleWidgets("after-stress", 5, true);
        Quit(app); app = null;
    }
}
