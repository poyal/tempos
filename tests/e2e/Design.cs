using App = FlaUI.Core.Application;
using FlaUI.Core.AutomationElements;

internal static partial class Program
{
    static void DesignAudit(ref App? app)
    {
        for (int i = 0; i < 5; i++) NativeButton(1203);
        SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1911, 0);
        for (int kind = 0; kind < 9; kind++)
        {
            if (kind > 0) NativeButton(1203);
            NativeCombo(1201, kind); NativeButton(1202);
            Element(1305).AsTextBox().Text = "0";
            for (int size = 0; size < (kind == 8 ? 5 : 4); size++)
            {
                NativeCombo(1300, size); NativeCombo(1301, 0); NativeCombo(1302, kind + 1); NativeButton(1316);
                Thread.Sleep(1100);
                var item = State()["windows"]![0]!;
                int dpi = item["dpi"]!.GetValue<int>();
                Check(Settings()["widgets"]![0]!["backgroundTransparency"]!.GetValue<int>() == 0, $"Design {kind}/{size}: opaque reference capture");
                Check(Settings()["widgets"]![0]!["kind"]!.GetValue<int>() == kind && Settings()["widgets"]![0]!["size"]!.GetValue<int>() == size, $"Design {kind}/{size}: selected layout saved");
                Check(Math.Abs(item["width"]!.GetValue<int>() - new[] {160, 336, 336, 336, 1008}[size] * dpi / 96.0) < 2 && Math.Abs(item["height"]!.GetValue<int>() - new[] {160, 72, 160, 336, 840}[size] * dpi / 96.0) < 2, $"Design {kind}/{size}: DIP geometry");
                VisibleWidgets($"design-kind-{kind}-size-{size}", 1, true);
            }
        }
        NativeButton(1203); NativeCombo(1201, 2); NativeButton(1202);
        NativeCombo(1300, 2); Element(1305).AsTextBox().Text = "0";
        for (int theme = 0; theme < 9; theme++)
        {
            NativeCombo(1302, theme + 1); NativeButton(1316);
            VisibleWidgets("design-theme-" + theme, 1, true);
        }
        for (int tab = 0; tab < 4; tab++)
        {
            Tab(tab);
            Screenshot("design-settings-" + tab, Window.BoundingRectangle);
            Check(Element(1000).AsTab().SelectedTabItemIndex == tab, "Design settings tab " + tab + " reachable");
        }
        Tab(0);
        for (int kind = 1; kind < 9; kind++)
        {
            NativeButton(1203); NativeCombo(1201, kind); NativeButton(1202);
            SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1911, 0);
            Element(1305).AsTextBox().Text = "0";
            NativeCombo(1300, kind == 8 ? 4 : 3); NativeButton(1316);
            foreach (int scenario in kind == 1 ? new[] {0, 1, 2, 3, 4, 5, 6, 7, 8} : kind == 8 ? new[] {9, 10} : new[] {9, 10, 11})
            {
                SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1900 + scenario, 0);
                VisibleWidgets($"design-state-{kind}-{scenario}", 1, true);
            }
            if (kind == 1)
            {
                Color Surface(int scenario)
                {
                    using var capture = new Bitmap(Path.Combine(Data, $"design-state-1-{scenario}.png"));
                    return capture.GetPixel(capture.Width - 45, capture.Height - 45);
                }
                Check(Surface(0) != Surface(4), "Weather clear day and night have distinct rendered surfaces");
                Check(Surface(2) != Surface(5), "Weather rain day and night have distinct rendered surfaces");
            }
        }
        SendMessage(Window.Properties.NativeWindowHandle, 0x111, 1912, 0);
        Quit(app!); app = null;
    }
}
