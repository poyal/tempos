using Tempos.Sensors;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Microsoft.Win32.SafeHandles;

static class Tests
{
    static int passed;
    static void Check(bool success, string name)
    {
        if (!success) throw new Exception(name);
        Console.WriteLine($"PASS {++passed} {name}");
    }
    static async Task<int> Main()
    {
        SetErrorMode(0x0002);
        try { await Run(); return 0; }
        catch (Exception error) { Console.Error.WriteLine("FAIL " + error); return 1; }
    }
    [DllImport("kernel32.dll")] static extern uint SetErrorMode(uint mode);
    static async Task Run()
    {
        Check(CpuSensors.Select([new("/amdcpu/0", true, [new("Core (Tdie)", 51), new("Core (Tctl/Tdie)", 61)])]) == 51,
            "AMD die temperature takes precedence");
        Check(CpuSensors.Select([new("/amdcpu/0", true, [new("Core (Tdie)", null), new("Core (Tctl/Tdie)", 61)])]) == 61,
            "AMD combined temperature is the supported fallback");
        Check(CpuSensors.Select([new("/intelcpu/0", false, [new("CPU Package", 55), new("CPU Core #1", 90)])]) == 55,
            "Intel uses package temperature rather than individual cores");
        Check(CpuSensors.Select([new("/amdcpu/0", true, [new("Core (Tctl)", 75), new("Core (Tdie)", 0)])]) == null,
            "Missing die sensors cannot be substituted with offsets or zero");
        Check(CpuSensors.Select([new("/intelcpu/0", false, [new("CPU Package", 45)]),
            new("/intelcpu/1", false, [new("CPU Package", 65)])]) == 65, "Multiple packages use maximum");
        foreach (float invalid in new[] { float.NaN, float.PositiveInfinity, -10, 0, 151 })
            Check(CpuSensors.Select([new("cpu", false, [new("CPU Package", invalid)])]) == null, "Invalid sensor reading rejected");
        Check(SensorServer.TryRequest("""{"version":1,"intervalSeconds":2}""", out var interval) && interval == 2,
            "Versioned request accepts a supported interval");
        foreach (var request in new[] { "{}", "[]", "bad", """{"version":2,"intervalSeconds":2}""",
                     """{"version":1,"intervalSeconds":0}""", """{"version":1,"intervalSeconds":11}""",
                     """{"version":1,"intervalSeconds":2,"command":"run"}""", new string('x', 257) })
            Check(!SensorServer.TryRequest(request, out _), "Malformed or unauthorized pipe request rejected");
        // A unique test pipe and injected sensor boundary; never installs/opens a driver.
        using var stop = new CancellationTokenSource();
        var name = "Tempos.SensorTests." + Guid.NewGuid().ToString("N");
        var source = new FakeSensors();
        var server = new SensorServer(source, name).Run(stop.Token);
        using (var client = await Connect(name))
        {
            await client.WriteAsync(Encoding.UTF8.GetBytes("""{"version":1,"intervalSeconds":2}""" + "\n"));
            using var reader = new StreamReader(client);
            var response = await reader.ReadLineAsync().WaitAsync(TimeSpan.FromSeconds(3));
            using var json = JsonDocument.Parse(response!);
            Check(json.RootElement.GetProperty("celsius").GetDouble() == 49 &&
                  json.RootElement.GetProperty("ageMs").GetDouble() == 250, "Real local pipe carries sensor boundary sample and age");
        }
        using (var client = await Connect(name))
        {
            await client.WriteAsync(Encoding.UTF8.GetBytes("""{"version":1,"intervalSeconds":2,"command":"run"}""" + "\n"));
            byte[] buffer = new byte[1];
            Check(await client.ReadAsync(buffer).AsTask().WaitAsync(TimeSpan.FromSeconds(3)) == 0, "Invalid request is disconnected");
        }
        stop.Cancel();
        await server.WaitAsync(TimeSpan.FromSeconds(3));
        Check(source.Reads == 1 && source.Disposed, "Invalid request never samples; shutdown releases sensors");
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern SafePipeHandle CreateFile(string name, uint access, uint share, nint attributes, uint disposition, uint flags, nint template);
    static async Task<NamedPipeClientStream> Connect(string name)
    {
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            var handle = CreateFile(@"\\.\pipe\" + name, 0x100003, 0, 0, 3, 0x40000000, 0);
            if (!handle.IsInvalid) return new NamedPipeClientStream(PipeDirection.InOut, true, true, handle);
            handle.Dispose();
            await Task.Delay(20);
        }
        throw new TimeoutException("Test pipe did not start; Win32 error " + Marshal.GetLastWin32Error());
    }
    sealed class FakeSensors : ICpuSensors
    {
        public int Reads;
        public bool Disposed;
        public TemperatureReply Read(int interval) { Reads++; return new(1, "ready", 49, 250); }
        public void ReleaseIdle() { }
        public void Dispose() => Disposed = true;
    }
}
