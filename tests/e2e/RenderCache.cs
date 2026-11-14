using FlaUI.Core.AutomationElements;
using App = FlaUI.Core.Application;

internal static partial class Program
{
    static void RenderCache(ref App? app, string exe)
    {
        for (int i = 0; i < 5; i++) NativeButton(1203);
        NativeCombo(1201, 0); NativeButton(1202);
        NativeCombo(1300, 1); NativeCombo(1301, 1);
        Element(1309).AsCheckBox().IsChecked = true;
        void Opacity(int background, int whole = 0)
        {
            SendText(GetDlgItem(Window.Properties.NativeWindowHandle, 1305), 0xC, 0, background.ToString());
            SendText(GetDlgItem(Window.Properties.NativeWindowHandle, 1306), 0xC, 0, whole.ToString());
            NativeButton(1316);
        }
        Color Sample(string name)
        {
            var view = State()["windows"]![0]!;
            var management = Window.Properties.NativeWindowHandle.Value;
            ShowWindow(management, 0);
            try
            {
                SetWindowPos(WidgetHandle(view), new nint(-1), 0, 0, 0, 0, 0x13);
                CaptureWidget(name, view);
                using var bitmap = new Bitmap(Path.Combine(Data, name + ".png"));
                return bitmap.GetPixel(bitmap.Width / 2, bitmap.Height - 8);
            }
            finally { ShowWindow(management, 5); }
        }
        Opacity(0);
        var colors = new List<int>();
        for (int theme = 0; theme < 9; theme++)
        {
            NativeCombo(1302, theme + 1); NativeButton(1316);
            colors.Add(Sample("cache-theme-" + theme).ToArgb());
        }
        Check(colors.Distinct().Count() == 9, "Every theme updates actual background pixels");
        NativeCombo(1302, 1); NativeButton(1316);
        int blue = Sample("cache-return-blue").ToArgb();
        Check(blue == colors[0], "Returning after all themes restores exact background pixels");
        var widget = WidgetHandle(State()["windows"]![0]!);
        for (int i = 0; i < 20; i++) SendMessage(widget, 0xF, 0, 0);
        Check(Sample("cache-repeat").ToArgb() == blue, "Repeated paints preserve background pixels");
        Opacity(70);
        int translucent = Sample("cache-translucent").ToArgb();
        Check(translucent != blue && translucent != Color.FromArgb(237, 244, 250).ToArgb(),
              "Partial background transparency replaces the opaque background");
        Opacity(100);
        Check(Sample("cache-transparent").ToArgb() == Color.FromArgb(237, 244, 250).ToArgb(),
              "Fully transparent background exposes only the neutral test backdrop");
        Opacity(0, 40);
        Check(Sample("cache-whole-opacity").ToArgb() != blue,
              "Whole-widget transparency still applies independently of background reuse");
        Opacity(0);
        Check(Sample("cache-opacity-restored").ToArgb() == blue, "Restored opacity has no stale cached pixels");
        foreach (int scale in new[] { 0, 2, 1 })
        {
            NativeCombo(1301, scale); NativeButton(1316);
            var pixel = Sample("cache-scale-" + scale);
            Check(pixel.B > pixel.R + 30 && pixel.B > pixel.G + 10,
                  "Scale change fills the bottom edge with the current blue theme: " + scale);
        }
        Restart(ref app, exe);
        Check(Sample("cache-restarted").ToArgb() == blue, "Restart restores the same size, theme and background pixels");
        for (int kind = 2; kind <= 4; kind++)
        {
            NativeButton(1203); NativeCombo(1201, kind); NativeButton(1202);
            NativeCombo(1300, 2); NativeCombo(1301, 0); NativeCombo(1302, 1);
            Element(1309).AsCheckBox().IsChecked = true; Opacity(0);
            // Explicit design examples verify rendering/cache invalidation only.
            // Actual collectors and HTTP parsing are covered in their own modes.
            string prefix = "example-cache-metric-" + kind;
            SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1933, 0);
            Sample(prefix + "-80");
            SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1934, 0);
            Sample(prefix + "-95");
            var view = State()["windows"]![0]!;
            double factor = view["dpi"]!.GetValue<double>() / 96;
            int Changed(string a, string b, int left, int top, int right, int bottom)
            {
                using var first = new Bitmap(Path.Combine(Data, a + ".png"));
                using var second = new Bitmap(Path.Combine(Data, b + ".png"));
                int changed = 0;
                for (int y = (int)(top * factor); y < (int)(bottom * factor); y++)
                    for (int x = (int)(left * factor); x < (int)(right * factor); x++)
                        if (first.GetPixel(x, y).ToArgb() != second.GetPixel(x, y).ToArgb()) changed++;
                return changed;
            }
            using (var bitmap = new Bitmap(Path.Combine(Data, prefix + "-80.png")))
            {
                int bright = 0;
                for (int y = (int)(14 * factor); y < (int)(46 * factor); y++)
                    for (int x = (int)(20 * factor); x < (int)(174 * factor); x++)
                    {
                        var pixel = bitmap.GetPixel(x, y);
                        if (pixel.R > 200 && pixel.G > 200 && pixel.B > 200) bright++;
                    }
                Check(bright > 40, "Design example retains visible static heading and icon: " + kind);
            }
            Check(Changed(prefix + "-80", prefix + "-95", 20, 14, 174, 46) == 0,
                  "Design example static heading remains pixel-identical during metric updates: " + kind);
            Check(Changed(prefix + "-80", prefix + "-95", 20, 46, 174, 130) > 20,
                  "Design example dynamic percentage and color continue updating: " + kind);
            if (kind != 4)
            {
                SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1931, 0);
                Sample(prefix + "-new-model");
                Check(Changed(prefix + "-95", prefix + "-new-model", 20, 134, 316, 157) > 20,
                      "Design example model-name change invalidates static text: " + kind);
                SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1933, 0);
                Sample(prefix + "-model-restored");
                Check(Changed(prefix + "-80", prefix + "-model-restored", 20, 134, 316, 157) == 0,
                      "Design example model-name round trip restores identical pixels: " + kind);
            }
            else
                Check(Changed(prefix + "-80", prefix + "-95", 20, 134, 316, 157) > 20,
                      "Design example memory totals remain dynamic outside the static layer");
            // A normal metric update must match a fresh whole-card paint exactly,
            // including fractional physical coordinates at 150% scaling.
            Element(1311).AsCheckBox().IsChecked = false;
            foreach (int scale in new[] { 0, 1, 2 })
            {
                NativeCombo(1301, scale); NativeButton(1316);
                SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1934, 0);
                SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1933, 0);
                string partial = prefix + "-update-scale-" + scale;
                string complete = prefix + "-whole-scale-" + scale;
                Sample(partial);
                NativeButton(1204); NativeButton(1204);
                Sample(complete);
                using var update = new Bitmap(Path.Combine(Data, partial + ".png"));
                using var whole = new Bitmap(Path.Combine(Data, complete + ".png"));
                int differences = 0;
                for (int y = 0; y < update.Height; y++)
                    for (int x = 0; x < update.Width; x++)
                        if (update.GetPixel(x, y).ToArgb() != whole.GetPixel(x, y).ToArgb()) differences++;
                Check(differences == 0, "Design example metric update matches whole-card pixels: kind=" +
                      kind + " scale=" + scale + " differences=" + differences);
            }
        }
        Quit(app!); app = null;
    }
}
