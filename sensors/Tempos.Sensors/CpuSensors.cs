using LibreHardwareMonitor.Hardware;
using LibreHardwareMonitor.PawnIo;
using System.Diagnostics;

namespace Tempos.Sensors;

public record SensorValue(string Name, float? Value);
public record CpuPackage(string Identifier, bool Amd, IReadOnlyList<SensorValue> Sensors);
public record TemperatureReply(int version, string status, double? celsius, double ageMs);
public interface ICpuSensors : IDisposable
{
    TemperatureReply Read(int interval);
    void ReleaseIdle();
}

public sealed class CpuSensors : ICpuSensors
{
    private Computer? computer;
    private long sampledAt, requestedAt;
    private TemperatureReply last = new(1, "unsupported", null, 0);

    // Representative die/package temperatures only: never substitute a motherboard,
    // core average, distance-to-TjMax, or a different device's sensor.
    public static double? Select(IReadOnlyList<CpuPackage> packages)
    {
        List<double> values = [];
        foreach (var package in packages)
        {
            string[] names = package.Amd ? ["Core (Tdie)", "Core (Tctl/Tdie)"] : ["CPU Package"];
            foreach (var name in names)
            {
                var sensor = package.Sensors.FirstOrDefault(s => s.Name == name && s.Value is > 0 and <= 150);
                if (sensor?.Value is float value && float.IsFinite(value))
                {
                    values.Add(value);
                    break;
                }
            }
        }
        return values.Count == 0 ? null : values.Max();
    }
    public TemperatureReply Read(int interval)
    {
        requestedAt = Stopwatch.GetTimestamp();
        if (sampledAt != 0 && Stopwatch.GetElapsedTime(sampledAt).TotalSeconds < interval)
            return last with { ageMs = Stopwatch.GetElapsedTime(sampledAt).TotalMilliseconds };
        try
        {
            if (!PawnIo.IsInstalled)
                return last = new(1, "unavailable", null, 0);
            if (computer is null)
            {
                computer = new Computer { IsCpuEnabled = true };
                computer.Open();
            }
            List<CpuPackage> packages = [];
            foreach (var cpu in computer.Hardware.Where(h => h.HardwareType == HardwareType.Cpu))
            {
                cpu.Update();
                packages.Add(new(cpu.Identifier.ToString(), cpu.Identifier.ToString().StartsWith("/amdcpu/"),
                    cpu.Sensors.Where(s => s.SensorType == SensorType.Temperature)
                        .Select(s => new SensorValue(s.Name, s.Value)).ToArray()));
            }
            var value = Select(packages);
            sampledAt = Stopwatch.GetTimestamp();
            return last = new(1, value.HasValue ? "ready" : "unsupported", value, 0);
        }
        catch
        {
            Dispose();
            return last = new(1, "unavailable", null, 0);
        }
    }
    public void ReleaseIdle()
    {
        if (requestedAt != 0 && Stopwatch.GetElapsedTime(requestedAt).TotalSeconds >= 30)
            Dispose();
    }
    public void Dispose()
    {
        try { computer?.Close(); } catch { }
        computer = null;
        sampledAt = 0;
        last = new(1, "unsupported", null, 0);
    }
}
