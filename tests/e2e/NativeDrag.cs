using System.Runtime.InteropServices;

internal static partial class Program
{
    [StructLayout(LayoutKind.Sequential)] struct DragInput
    {
        public nint Target;
        public int FromX, FromY, ToX, ToY, Move;
    }
    [StructLayout(LayoutKind.Sequential)] struct CopyData
    {
        public nuint Kind;
        public uint Size;
        public nint Data;
    }
    [DllImport("user32.dll", EntryPoint="SendMessageW")]
    static extern nint SendDragCopy(nint window, uint message, nint sender, ref CopyData data);
    static (double Move, double Drop, double Total) NativeDragBatch(nint widget, int fromX, int fromY, int toX, int toY, bool move = true)
    {
        GetWindowThreadProcessId(widget, out var owner);
        if (owner != ProcessId) throw new InvalidOperationException("Drag target must belong to this TestHost.");
        var input = new DragInput { Target = widget, FromX = fromX, FromY = fromY, ToX = toX, ToY = toY, Move = move ? 1 : 0 };
        nint memory = Marshal.AllocHGlobal(Marshal.SizeOf<DragInput>());
        try {
            Marshal.StructureToPtr(input, memory, false);
            var data = new CopyData { Kind = 0x544d5044, Size = (uint)Marshal.SizeOf<DragInput>(), Data = memory };
            long start = System.Diagnostics.Stopwatch.GetTimestamp();
            ulong result = unchecked((ulong)SendDragCopy(Window.Properties.NativeWindowHandle, 0x4A, 0, ref data).ToInt64());
            double total = System.Diagnostics.Stopwatch.GetElapsedTime(start).TotalMilliseconds;
            if (result == 0) throw new InvalidOperationException("TestHost rejected native drag batch.");
            return (((result & 0xffffffff) - 1) / 1000.0, (result >> 32) / 1000.0, total);
        } finally { Marshal.FreeHGlobal(memory); }
    }
}
