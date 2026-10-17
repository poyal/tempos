using System.ServiceProcess;
namespace Tempos.Sensors;

internal static class Program
{
    static void Main() => ServiceBase.Run(new SensorService());
}

internal sealed class SensorService : ServiceBase
{
    private readonly CancellationTokenSource stop = new();
    private Task? loop;
    public SensorService()
    {
        ServiceName = "TemposSensors";
        CanStop = true;
        AutoLog = false;
    }
    protected override void OnStart(string[] args) =>
        loop = Task.Run(() => new SensorServer(new CpuSensors()).Run(stop.Token));
    protected override void OnStop()
    {
        stop.Cancel();
        try { loop?.Wait(TimeSpan.FromSeconds(5)); } catch (AggregateException) { }
    }
}
