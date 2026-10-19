using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Media;
using Windows.UI;

namespace ZhongriSettingsV2;

// Native buttons/sliders: no hex fields and no GPU-dependent ColorSpectrum.
public sealed class ColorPalette : UserControl
{
    private readonly StackPanel _content = new() { Spacing = 12 };
    private UIElementCollection Children => _content.Children;
    private readonly Slider[] _channels = new Slider[3];
    private readonly Border _preview = new() { Height = 32, CornerRadius = new CornerRadius(6) };
    private Color _color;
    private bool _updating;
    internal event Action<Color>? ColorChanged;
    internal Color Color
    {
        get => _color;
        set
        {
            _color = Windows.UI.Color.FromArgb(255, value.R, value.G, value.B);
            _updating = true;
            for (var index = 0; index < 3; index++)
                if (_channels[index] is not null)
                    _channels[index].Value = index == 0 ? value.R : index == 1 ? value.G : value.B;
            _updating = false;
            _preview.Background = new SolidColorBrush(_color);
            ColorChanged?.Invoke(_color);
        }
    }

    internal ColorPalette()
    {
        Width = 280;
        Content = _content;
        Children.Add(new TextBlock { Text = "调色板", FontSize = 18 });
        var grid = new Grid { ColumnSpacing = 8, RowSpacing = 8 };
        for (var column = 0; column < 6; column++) grid.ColumnDefinitions.Add(new ColumnDefinition());
        for (var row = 0; row < 5; row++) grid.RowDefinitions.Add(new RowDefinition());
        uint[] colors = [0xFFFFFF, 0xD1D5DB, 0x9CA3AF, 0x6B7280, 0x374151, 0x000000,
            0xFEE2E2, 0xFED7AA, 0xFEF08A, 0xBBF7D0, 0xBAE6FD, 0xDDD6FE,
            0xF87171, 0xFB923C, 0xFFFF00, 0x4ADE80, 0x38BDF8, 0xA78BFA,
            0xEF4444, 0xF97316, 0xEAB308, 0x22C55E, 0x06B6D4, 0x8B5CF6,
            0xBE123C, 0xC2410C, 0xA16207, 0x15803D, 0x2563EB, 0xDB2777];
        for (var index = 0; index < colors.Length; index++)
        {
            var rgb = colors[index];
            var color = Windows.UI.Color.FromArgb(255, (byte)(rgb >> 16), (byte)(rgb >> 8), (byte)rgb);
            var button = new Button { Width = 40, Height = 36, Padding = new Thickness(3),
                Content = new Border { Width = 30, Height = 26, CornerRadius = new CornerRadius(4),
                    Background = new SolidColorBrush(color) } };
            AutomationProperties.SetName(button, $"选择颜色 {index + 1}");
            button.Click += (_, _) => Color = color;
            Grid.SetColumn(button, index % 6);
            Grid.SetRow(button, index / 6);
            grid.Children.Add(button);
        }
        Children.Add(grid);
        Children.Add(_preview);
        var fine = new StackPanel { Spacing = 8 };
        var names = new[] { "红色", "绿色", "蓝色" };
        for (var index = 0; index < 3; index++)
        {
            fine.Children.Add(new TextBlock { Text = names[index] });
            var slider = new Slider { Minimum = 0, Maximum = 255, StepFrequency = 1, IsThumbToolTipEnabled = false };
            AutomationProperties.SetName(slider, names[index] + "微调");
            _channels[index] = slider;
            slider.ValueChanged += (_, _) => {
                if (!_updating) Color = Windows.UI.Color.FromArgb(255,
                    (byte)_channels[0].Value, (byte)_channels[1].Value, (byte)_channels[2].Value);
            };
            fine.Children.Add(slider);
        }
        Children.Add(fine);
        Color = Windows.UI.Color.FromArgb(255, 255, 255, 255);
    }
}
