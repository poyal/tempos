using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Microsoft.Win32.SafeHandles;
using System.ComponentModel;
using System.Security.Principal;

namespace Tempos.Sensors;

public sealed class SensorServer(ICpuSensors sensors, string pipeName = "Tempos.Sensors.v1")
{
    public static bool TryRequest(string json, out int interval)
    {
        interval = 0;
        if (json.Length > 256) return false;
        try
        {
            using var doc = JsonDocument.Parse(json);
            var root = doc.RootElement;
            return root.GetProperty("version").GetInt32() == 1 &&
                root.GetProperty("intervalSeconds").TryGetInt32(out interval) && interval is >= 1 and <= 10 &&
                root.EnumerateObject().Count() == 2;
        }
        catch (Exception e) when (e is JsonException or InvalidOperationException or KeyNotFoundException or FormatException)
        { return false; }
    }
    public async Task Run(CancellationToken stop)
    {
        using (sensors)
        {
            while (!stop.IsCancellationRequested)
            {
                sensors.ReleaseIdle();
                try
                {
                    using var pipe = CreateLocalPipe(pipeName);
                    using var wait = CancellationTokenSource.CreateLinkedTokenSource(stop);
                    wait.CancelAfter(TimeSpan.FromSeconds(10));
                    await pipe.WaitForConnectionAsync(wait.Token);
                    using var request = CancellationTokenSource.CreateLinkedTokenSource(stop);
                    request.CancelAfter(TimeSpan.FromSeconds(2));
                    var buffer = new byte[257];
                    int count = 0;
                    while (count < buffer.Length)
                    {
                        int read = await pipe.ReadAsync(buffer.AsMemory(count, 1), request.Token);
                        if (read == 0 || buffer[count] == (byte)'\n') break;
                        count += read;
                    }
                    if (count > 256 || !TryRequest(Encoding.UTF8.GetString(buffer, 0, count), out int interval))
                        continue;
                    var response = sensors.Read(interval);
                    byte[] bytes = Encoding.UTF8.GetBytes(JsonSerializer.Serialize(response) + "\n");
                    await pipe.WriteAsync(bytes, request.Token);
                }
                catch (OperationCanceledException) { }
                catch (Exception e) when (e is IOException or UnauthorizedAccessException or Win32Exception)
                {
                    // Bounded retry also prevents a busy loop if a pipe name is occupied.
                    try { await Task.Delay(1000, stop); } catch (OperationCanceledException) { }
                }
            }
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct SecurityAttributes { public int Length; public nint Descriptor; public int Inherit; }
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool ConvertStringSecurityDescriptorToSecurityDescriptor(string text, int revision, out nint descriptor, out int size);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern SafePipeHandle CreateNamedPipe(string name, uint mode, uint pipeMode, uint instances,
        uint outputSize, uint inputSize, uint timeout, ref SecurityAttributes attributes);
    [DllImport("kernel32.dll")] private static extern nint LocalFree(nint memory);
    private static NamedPipeServerStream CreateLocalPipe(string name)
    {
        // Authenticated local users may request temperature only. They cannot create a
        // competing server instance (FILE_CREATE_PIPE_INSTANCE) or reach it over SMB.
        var owner = WindowsIdentity.GetCurrent().User!.Value;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptor(
                $"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;{owner})(A;;0x120183;;;AU)", 1, out var descriptor, out _))
            throw new Win32Exception();
        try
        {
            var sa = new SecurityAttributes { Length = Marshal.SizeOf<SecurityAttributes>(), Descriptor = descriptor };
            // DUPLEX | OVERLAPPED | FIRST_PIPE_INSTANCE; BYTE | REJECT_REMOTE_CLIENTS.
            var handle = CreateNamedPipe(@"\\.\pipe\" + name, 0x40080003, 0x8, 1, 4096, 256, 0, ref sa);
            if (handle.IsInvalid) { handle.Dispose(); throw new Win32Exception(); }
            try { return new NamedPipeServerStream(PipeDirection.InOut, true, false, handle); }
            catch { handle.Dispose(); throw; }
        }
        finally { LocalFree(descriptor); }
    }
}
