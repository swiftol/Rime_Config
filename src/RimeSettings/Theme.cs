using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace RimeSettings;

internal static class Theme
{
    public static readonly Color Window = Color.FromArgb(25, 28, 32);
    public static readonly Color Sidebar = Color.FromArgb(30, 34, 39);
    public static readonly Color Surface = Color.FromArgb(37, 41, 46);
    public static readonly Color SurfaceHover = Color.FromArgb(44, 49, 55);
    public static readonly Color Input = Color.FromArgb(46, 51, 57);
    public static readonly Color Text = Color.FromArgb(233, 236, 239);
    public static readonly Color Muted = Color.FromArgb(153, 161, 170);
    public static readonly Color Accent = Color.FromArgb(23, 196, 151);
    public static readonly Color AccentDark = Color.FromArgb(17, 119, 96);
    public static readonly Color Danger = Color.FromArgb(223, 82, 82);

    public static Font Font(float size, FontStyle style = FontStyle.Regular) =>
        new("Microsoft YaHei UI", size, style, GraphicsUnit.Point);
}

internal class RoundedPanel : Panel
{
    public int Radius { get; set; } = 12;

    public RoundedPanel()
    {
        DoubleBuffered = true;
        Resize += (_, _) => UpdateRegion();
    }

    private void UpdateRegion()
    {
        if (Width <= 0 || Height <= 0) return;
        using var path = RoundedRect(new Rectangle(0, 0, Width, Height), Radius);
        Region = new Region(path);
    }

    public static GraphicsPath RoundedRect(Rectangle bounds, int radius)
    {
        var diameter = radius * 2;
        var path = new GraphicsPath();
        path.AddArc(bounds.Left, bounds.Top, diameter, diameter, 180, 90);
        path.AddArc(bounds.Right - diameter, bounds.Top, diameter, diameter, 270, 90);
        path.AddArc(bounds.Right - diameter, bounds.Bottom - diameter, diameter, diameter, 0, 90);
        path.AddArc(bounds.Left, bounds.Bottom - diameter, diameter, diameter, 90, 90);
        path.CloseFigure();
        return path;
    }
}

internal class FlatButton : Button
{
    public int Radius { get; set; } = 9;

    public FlatButton()
    {
        FlatStyle = FlatStyle.Flat;
        FlatAppearance.BorderSize = 0;
        Cursor = Cursors.Hand;
        Font = Theme.Font(10);
        ForeColor = Theme.Text;
        BackColor = Theme.Surface;
        Resize += (_, _) => UpdateRegion();
    }

    private void UpdateRegion()
    {
        if (Width <= 0 || Height <= 0) return;
        using var path = RoundedPanel.RoundedRect(new Rectangle(0, 0, Width, Height), Radius);
        Region = new Region(path);
    }
}

internal sealed class ToggleSwitch : CheckBox
{
    public Color OnColor { get; set; } = Theme.Accent;
    public Color OffColor { get; set; } = Color.FromArgb(78, 84, 91);

    public ToggleSwitch()
    {
        Appearance = Appearance.Button;
        FlatStyle = FlatStyle.Flat;
        FlatAppearance.BorderSize = 0;
        Text = "";
        Width = 48;
        Height = 26;
        Cursor = Cursors.Hand;
        SetStyle(ControlStyles.AllPaintingInWmPaint |
                 ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.ResizeRedraw |
                 ControlStyles.UserPaint, true);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
        var track = new Rectangle(0, 2, Width - 1, Height - 4);
        using var trackPath = RoundedPanel.RoundedRect(track, track.Height / 2);
        using var trackBrush = new SolidBrush(Checked ? OnColor : OffColor);
        e.Graphics.FillPath(trackBrush, trackPath);

        var knobSize = Height - 8;
        var knobX = Checked ? Width - knobSize - 4 : 4;
        using var knobBrush = new SolidBrush(Color.White);
        e.Graphics.FillEllipse(knobBrush, knobX, 4, knobSize, knobSize);
    }

    protected override void OnCheckedChanged(System.EventArgs e)
    {
        base.OnCheckedChanged(e);
        Invalidate();
    }
}

internal sealed class Divider : Panel
{
    public Divider()
    {
        Height = 1;
        BackColor = Color.FromArgb(54, 59, 65);
    }
}
