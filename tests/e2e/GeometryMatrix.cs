using FlaUI.Core.AutomationElements;
using System.Text.Json.Nodes;
using App = FlaUI.Core.Application;

internal static partial class Program
{
    static void GeometryMatrix(ref App? app, string exe)
    {
        for (int i = 0; i < 5; i++) NativeButton(1203);
        for (int kind = 0; kind < 9; kind++)
        {
            NativeCombo(1201, kind); NativeButton(1202);
            for (int size = 0; size < (kind == 8 ? 5 : 4); size++)
                for (int scale = 0; scale < 3; scale++)
                {
                    var before = Settings().DeepClone();
                    var state = State();
                    var monitor = state["monitors"]![0]!;
                    double dpi = monitor["dpi"]!.GetValue<double>();
                    double width = Math.Ceiling(new[] {160,336,336,336,1008}[size] * new[] {1,1.5,2}[scale] / 8) * 8;
                    double height = Math.Ceiling(new[] {160,72,160,336,840}[size] * new[] {1,1.5,2}[scale] / 8) * 8;
                    double availableWidth = (monitor["right"]!.GetValue<double>() - monitor["left"]!.GetValue<double>()) * 96 / dpi;
                    double availableHeight = (monitor["bottom"]!.GetValue<double>() - monitor["top"]!.GetValue<double>()) * 96 / dpi;
                    NativeCombo(1300, size); NativeCombo(1301, scale); NativeButton(1316);
                    string label = $"Geometry kind={kind} size={size} scale={scale} dpi={dpi}";
                    if (width + 32 > availableWidth || height + 32 > availableHeight)
                    {
                        Check(JsonNode.DeepEquals(before, Settings()), label + ": oversized request preserves layout");
                        Check(Element(1999).Name.Contains("빈 공간"), label + ": oversized request explains rejection");
                        NativeButton(1325);
                        continue;
                    }
                    var actual = State()["windows"]![0]!;
                    Check(actual["placed"]!.GetValue<bool>() && actual["visible"]!.GetValue<bool>(), label + ": visible");
                    Check(Math.Abs(actual["width"]!.GetValue<double>() - width * dpi / 96) < 1.1 &&
                          Math.Abs(actual["height"]!.GetValue<double>() - height * dpi / 96) < 1.1, label + ": physical bounds");
                    var saved = Settings()["widgets"]![0]!;
                    Check(saved["size"]!.GetValue<int>() == size && saved["scale"]!.GetValue<int>() == new[] {100,150,200}[scale], label + ": saved size and scale");
                    double x = saved["x"]!.GetValue<double>(), y = saved["y"]!.GetValue<double>();
                    Check(x % 8 == 0 && y % 8 == 0 && (x + width) % 8 == 0 && (y + height) % 8 == 0,
                          label + ": all four edges fill complete grid cells");
                    int Pixel(double dip) => (int)Math.Round(dip * dpi / 96, MidpointRounding.AwayFromZero);
                    int left = actual["x"]!.GetValue<int>() - monitor["left"]!.GetValue<int>();
                    int top = actual["y"]!.GetValue<int>() - monitor["top"]!.GetValue<int>();
                    Check(left == Pixel(x) && top == Pixel(y) &&
                          left + actual["width"]!.GetValue<int>() == Pixel(x + width) &&
                          top + actual["height"]!.GetValue<int>() == Pixel(y + height),
                          label + ": native left/right/top/bottom coincide with drawn grid lines");
                    Check(!string.IsNullOrWhiteSpace(Automation.FromHandle(WidgetHandle(actual)).Name), label + ": accessible content");
                    if (kind == 0 && size == 1 && scale == 1)
                    {
                        // 72 DIP × 150% used to stop at 108 DIP, halfway through
                        // an eight-DIP cell. Check rendered fill, not just HWND size.
                        SendText(GetDlgItem(Window.Properties.NativeWindowHandle, 1305), 0xC, 0, "0");
                        SendText(GetDlgItem(Window.Properties.NativeWindowHandle, 1306), 0xC, 0, "0");
                        Element(1309).AsCheckBox().IsChecked = true;
                        NativeCombo(1302, 1); NativeButton(1316);
                        ShowWindow(Window.Properties.NativeWindowHandle, 0);
                        try
                        {
                            SetWindowPos(WidgetHandle(actual), new nint(-1), 0, 0, 0, 0, 0x13);
                            CaptureWidget("grid-eight-slim-150", actual);
                            using var bitmap = new Bitmap(Path.Combine(Data, "grid-eight-slim-150.png"));
                            int inset = Math.Max(2, (int)Math.Ceiling(2 * 1.5 * dpi / 96));
                            var points = new[] { new Point(inset, bitmap.Height / 2),
                                new Point(bitmap.Width - 1 - inset, bitmap.Height / 2),
                                new Point(bitmap.Width / 2, inset), new Point(bitmap.Width / 2, bitmap.Height - 1 - inset) };
                            Check(points.All(p => { var c = bitmap.GetPixel(p.X, p.Y); return c.B > c.R + 30 && c.B > c.G + 10; }),
                                  "Expanded Slim background fills left/right/top/bottom equally without an empty bottom strip");
                        }
                        finally { ShowWindow(Window.Properties.NativeWindowHandle, 5); }
                    }
                }
            var expected = Settings().DeepClone();
            Restart(ref app, exe);
            Check(JsonNode.DeepEquals(expected, Settings()), "Geometry last accepted size survives restart: " + kind);
            NativeButton(1203);
        }
        Quit(app!); app = null;
    }
}
