using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Input;
using Microsoft.Win32;
using RimeSettings;
using Windows.UI;
using DrawingColor = System.Drawing.Color;

namespace ZhongriSettingsV2;

public sealed partial class MainWindow : Window
{
    private readonly Grid Root = new() { RequestedTheme = ElementTheme.Dark };
    private readonly Dictionary<string, Button> _navButtons = [];
    private readonly List<TextBlock> _navLabels = [];
    private readonly ColumnDefinition _sidebarColumn = new() { Width = new GridLength(220) };
    private readonly StackPanel PageContent = new() { MaxWidth = 900, Spacing = 16,
        HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly Button _focusSink = new() { Width = 1, Height = 1, Opacity = 0,
        IsHitTestVisible = false, IsTabStop = true, TabIndex = 0,
        HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Top };
    private readonly SettingsStore _store;
    private PasswordBox? _credentialKeyBox;
    private ToggleSwitch? _sentenceTranslationSwitch;
    private ToggleSwitch? _preferNiuAnnotationsSwitch;
    private readonly ChineseCorrectionStore _chineseCorrections;
    private readonly bool _uiTestMode;
    private bool WheelDiagnosticsEnabled => Environment.GetCommandLineArgs().Contains("--wheel-diagnostics", StringComparer.Ordinal) ||
        File.Exists(Path.Combine(AppContext.BaseDirectory, "wheel-diagnostics.marker"));
    private ScrollViewer? _contentScroll;
    // The preview is a sibling of the settings scroller, never its child.
    private readonly StackPanel _appearancePinned = new() { Spacing = 12,
        MaxWidth = 900, HorizontalAlignment = HorizontalAlignment.Stretch,
        Margin = new Thickness(28, 16, 28, 12), Visibility = Visibility.Collapsed };
    private Grid? _contentBody;
    private Action<bool>? _selectAppearanceSection;
    private double? _wheelTarget;
    private int _wheelRetries;
    private Microsoft.UI.Dispatching.DispatcherQueueTimer? _wheelRetryTimer;
    private readonly WindowSubclassProc _subclassProc;
    private IntPtr _windowHandle;
    private readonly HashSet<IntPtr> _wheelInputSites = [];
    private readonly Dictionary<string, DrawingColor> _colors = [];
    private readonly Dictionary<string, ColorPalette> _colorPickers = [];
    private readonly Dictionary<string, Button> _colorChooseButtons = [];
    private readonly Dictionary<string, Action> _colorResets = [];
    private TextBox? _width;
    private TextBox? _commentSize;
    private TextBox? _spacing;
    private TextBox? _padding;
    private TextBlock? _saveStatus;
    private ToggleSwitch? _englishAnnotationSwitch;
    private ToggleSwitch? _japaneseAnnotationSwitch;
    private ToggleSwitch? _singleCharacterAnnotationSwitch;
    private Microsoft.UI.Dispatching.DispatcherQueueTimer? _focusResetTimer;
    private bool _explicitEditFocus;
    private int _blankFocusRevision;
    private string _currentPage = "input";
    private bool _blankFocusActive;
    internal string SmokeDiagnostic { get; private set; } = "not started";
    internal string AnnotationDiagnostic =>
        $"registryPath={_store.HotSettingsRegistryPath}; userDir={_store.RimeDirectory}; " +
        $"hot={_store.ReadAnnotationHotState()}; uiTest={_uiTestMode}";

    internal string LayoutDiagnostics()
    {
        var last = PageContent.Children.LastOrDefault() as FrameworkElement;
        var lastBottom = last is null ? -1 :
            last.TransformToVisual(PageContent).TransformPoint(new Windows.Foundation.Point()).Y + last.ActualHeight;
        var children = string.Join(",", PageContent.Children.OfType<FrameworkElement>()
            .Select((child, index) => $"{index}:{child.GetType().Name}@" +
                $"{child.TransformToVisual(PageContent).TransformPoint(new Windows.Foundation.Point()).Y:F0}+" +
                $"{child.ActualHeight:F0}"));
        return $"pageHeight={PageContent.ActualHeight:F1} lastBottom={lastBottom:F1} " +
            $"extent={_contentScroll?.ExtentHeight:F1} viewport={_contentScroll?.ViewportHeight:F1} " +
            $"max={_contentScroll?.ScrollableHeight:F1} offset={_contentScroll?.VerticalOffset:F1} children={children}";
    }

    public MainWindow(string? testUserDirectory = null, string? testRegistryPath = null)
    {
        _uiTestMode = Environment.GetCommandLineArgs().Contains("--ui-test", StringComparer.Ordinal) ||
            File.Exists(Path.Combine(AppContext.BaseDirectory, "ui-test.marker"));
        Title = _uiTestMode ? "中日输入法设置 · UI 测试模式（不保存）" : "中日输入法设置 · 新版预览";
        BuildShell();
        ConfigureWindowChrome();
        _subclassProc = HandleWindowMessage;
        _windowHandle = WinRT.Interop.WindowNative.GetWindowHandle(this);
        if (!SetWindowSubclass(_windowHandle, _subclassProc, (UIntPtr)1, UIntPtr.Zero))
            throw new InvalidOperationException("无法安装设置窗口的滚轮处理。 ");
        Closed += (_, _) => RemoveWindowSubclass(_windowHandle, _subclassProc, (UIntPtr)1);
        Root.Loaded += (_, _) => AttachWheelInputSites();
        Root.SizeChanged += (_, _) => { if (Root.IsLoaded) AttachWheelInputSites(); };
        Closed += (_, _) => {
            foreach (var site in _wheelInputSites) RemoveWindowSubclass(site, _subclassProc, (UIntPtr)2);
        };
        Closed += (_, _) => StopClipboardListener();
        Closed += (_, _) => FlushAutoSaves();
        FitWindowToWorkArea();
        _store = testUserDirectory is not null && testRegistryPath is not null
            ? new SettingsStore(testUserDirectory, testRegistryPath)
            : new SettingsStore(ResolveUserDirectory());
        if (testUserDirectory is not null && testRegistryPath is not null)
            _store.SaveAnnotationOptions(false, false, false, true);
        _chineseCorrections = new ChineseCorrectionStore(_store.RimeDirectory);
        AppWindow.Closing += async (_, args) => {
            FlushPendingAutoSaves();
            if (_automaticDeployment.IsCompleted && _keyEditorWrites.IsCompleted) return;
            args.Cancel = true;
            Root.IsHitTestVisible = false;
            if (_contentScroll is not null) _contentScroll.IsEnabled=false;
            foreach(var button in _navButtons.Values) button.IsEnabled=false;
            await _automaticDeployment;
            await _keyEditorWrites;
            DispatcherQueue.TryEnqueue(() => Close());
        };
        Activated += (_, args) => TraceWheel("activation=" + args.WindowActivationState);
        _contentScroll!.PointerEntered += (_, _) => TraceWheel("content-pointer-entered");
        _contentScroll.PointerExited += (_, _) => TraceWheel("content-pointer-exited");
        SelectPage("input");
        TraceWheel("startup native-scroll pid=" + Environment.ProcessId);
    }

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute,
        ref int value, int size);

    [DllImport("user32.dll")]
    private static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern bool GetWindowRect(IntPtr hwnd, out WindowRect rect);
    [DllImport("user32.dll")]
    private static extern IntPtr GetWindowDpiAwarenessContext(IntPtr hwnd);
    [DllImport("user32.dll")]
    private static extern int GetAwarenessFromDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")]
    private static extern uint GetDpiForWindow(IntPtr hwnd);

    [StructLayout(LayoutKind.Sequential)]
    private struct WindowPoint { public int X, Y; }
    [DllImport("user32.dll")]
    private static extern bool ScreenToClient(IntPtr hwnd, ref WindowPoint point);
    [DllImport("user32.dll")]
    private static extern bool ClientToScreen(IntPtr hwnd, ref WindowPoint point);
    private delegate bool ChildWindowVisitor(IntPtr hwnd, IntPtr data);
    [DllImport("user32.dll")]
    private static extern bool EnumChildWindows(IntPtr hwnd, ChildWindowVisitor visitor, IntPtr data);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetClassName(IntPtr hwnd, System.Text.StringBuilder name, int size);

    private delegate IntPtr WindowSubclassProc(IntPtr hwnd, uint message, IntPtr wParam,
        IntPtr lParam, UIntPtr id, UIntPtr data);

    [DllImport("comctl32.dll", SetLastError = true)]
    private static extern bool SetWindowSubclass(IntPtr hwnd, WindowSubclassProc callback,
        UIntPtr id, UIntPtr data);

    [DllImport("comctl32.dll")]
    private static extern bool RemoveWindowSubclass(IntPtr hwnd, WindowSubclassProc callback,
        UIntPtr id);

    [DllImport("comctl32.dll")]
    private static extern IntPtr DefSubclassProc(IntPtr hwnd, uint message, IntPtr wParam,
        IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    private struct WindowRect { public int Left, Top, Right, Bottom; }

    private IntPtr HandleWindowMessage(IntPtr hwnd, uint message, IntPtr wParam,
        IntPtr lParam, UIntPtr id, UIntPtr data)
    {
        if (message == 0x020A || message == 0x024E) // mouse / pointer wheel: observe, never intercept
        {
            var delta = unchecked((short)((wParam.ToInt64() >> 16) & 0xffff));
            WheelEventCount++;
            TraceWheel($"native hwnd={hwnd} msg={message:X} delta={delta}");
        }
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    private void TraceWheel(string detail)
    {
        if (!WheelDiagnosticsEnabled) return;
        try {
            File.AppendAllText(Path.Combine(AppContext.BaseDirectory, "wheel-diagnostics.log"),
                $"{DateTime.Now:O} pid={Environment.ProcessId} {detail}; " +
                $"offset={_contentScroll?.VerticalOffset:F1} max={_contentScroll?.ScrollableHeight:F1} " +
                $"scale={Root.XamlRoot?.RasterizationScale:F2}\n");
        } catch { /* diagnostics must not affect input */ }
    }

    private void AttachWheelInputSites()
    {
        // WinUI delivers real mouse messages to its input-site child HWND,
        // rather than necessarily forwarding them to the top-level window.
        EnumChildWindows(_windowHandle, (site, _) => {
            var name = new System.Text.StringBuilder(256);
            GetClassName(site, name, name.Capacity);
            if (name.ToString().Contains("InputSite", StringComparison.Ordinal) && !_wheelInputSites.Contains(site) &&
                SetWindowSubclass(site, _subclassProc, (UIntPtr)2, UIntPtr.Zero))
                _wheelInputSites.Add(site);
            return true;
        }, IntPtr.Zero);
    }

    private bool IsWheelOverContent(IntPtr hwnd, IntPtr lParam)
    {
        if (_contentScroll is null || Root.XamlRoot is null) return false;
        var packed = lParam.ToInt64();
        var point = new WindowPoint { X = unchecked((short)(packed & 0xffff)),
            Y = unchecked((short)((packed >> 16) & 0xffff)) };
        if (!ScreenToClient(hwnd, ref point)) return false;
        var scale = Root.XamlRoot.RasterizationScale;
        var origin = _contentScroll.TransformToVisual(Root).TransformPoint(new Windows.Foundation.Point());
        var x = point.X / scale;
        var y = point.Y / scale;
        return x >= origin.X && x < origin.X + _contentScroll.ActualWidth &&
            y >= origin.Y && y < origin.Y + _contentScroll.ActualHeight;
    }

    private void ConfigureWindowChrome()
    {
        var icon = Path.Combine(AppContext.BaseDirectory, "zhongri.ico");
        if (File.Exists(icon)) AppWindow.SetIcon(icon);
        var darkMode = 1;
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        DwmSetWindowAttribute(hwnd, 20, ref darkMode, sizeof(int));
        if (Microsoft.UI.Windowing.AppWindowTitleBar.IsCustomizationSupported())
        {
            var bar = AppWindow.TitleBar;
            bar.BackgroundColor = Color.FromArgb(255, 29, 29, 32);
            bar.ForegroundColor = Color.FromArgb(255, 242, 242, 245);
            bar.InactiveBackgroundColor = Color.FromArgb(255, 29, 29, 32);
            bar.InactiveForegroundColor = Color.FromArgb(255, 170, 171, 182);
            bar.ButtonBackgroundColor = Color.FromArgb(255, 29, 29, 32);
            bar.ButtonForegroundColor = Color.FromArgb(255, 242, 242, 245);
            bar.ButtonHoverBackgroundColor = Color.FromArgb(255, 61, 58, 83);
            bar.ButtonHoverForegroundColor = Color.FromArgb(255, 255, 255, 255);
            bar.ButtonPressedBackgroundColor = Color.FromArgb(255, 79, 74, 112);
        }
    }

    private void FitWindowToWorkArea()
    {
        var work = Microsoft.UI.Windowing.DisplayArea.GetFromWindowId(AppWindow.Id,
            Microsoft.UI.Windowing.DisplayAreaFallback.Primary).WorkArea;
        var width = Math.Min(1120, Math.Max(320, work.Width - 24));
        var height = Math.Min(780, Math.Max(320, work.Height - 24));
        AppWindow.MoveAndResize(new Windows.Graphics.RectInt32(
            work.X + (work.Width - width) / 2,
            work.Y + (work.Height - height) / 2,
            width, height));
    }

    private void BuildShell()
    {
        Root.ColumnDefinitions.Add(_sidebarColumn);
        Root.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        var navItems = new StackPanel { Spacing = 3, Margin = new Thickness(10, 14, 10, 12) };
        var brand = new TextBlock { Text = "中日输入法", FontSize = 20,
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold, Margin = new Thickness(13, 12, 0, 23) };
        _navLabels.Add(brand);
        navItems.Children.Add(brand);
        AddNavigationItem(navItems, "输入", "input", Symbol.Keyboard);
        AddNavigationItem(navItems, "自定义配置", "global-profiles", Symbol.Library);
        AddNavigationItem(navItems, "日语按键表", "japanese-table", Symbol.Keyboard);
        AddNavigationItem(navItems, "外观", "appearance", Symbol.Pictures);
        AddNavigationItem(navItems, "日语模糊匹配", "japanese", Symbol.World);
        AddNavigationItem(navItems, "中文模糊纠错", "chinese", Symbol.Edit);
        AddNavigationItem(navItems, "自定义模糊匹配", "custom", Symbol.Filter);
        navItems.Children.Add(Divider());
        AddNavigationItem(navItems, "常用语", "phrases", Symbol.Favorite);
        AddNavigationItem(navItems, "剪贴板", "clipboard", Symbol.Paste);
        AddNavigationItem(navItems, "维护", "maintenance", Symbol.Repair);
        navItems.Children.Add(Divider());
        AddNavigationItem(navItems, "快捷键", "hotkeys", Symbol.Keyboard);
        AddNavigationItem(navItems, "实时句子翻译", "sentence", Symbol.Message);
        AddNavigationItem(navItems, "Google 云候选", "cloud", Symbol.World);
        var sidebar = new Border { Background = Brush(29, 29, 32), Child = new ScrollViewer
        {
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            Content = navItems
        } };
        Grid.SetColumn(sidebar, 0);
        Root.Children.Add(sidebar);

        // Leave room below page actions for IME floating toolbars and the
        // Windows taskbar; the final action must remain mouse-clickable.
        var body = new Grid { Padding = new Thickness(28, 24, 28, 96),
            Background = new SolidColorBrush(Color.FromArgb(0, 0, 0, 0)) };
        _contentBody = body;
        body.Children.Add(PageContent);
        _contentScroll = new ScrollViewer
        {
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            VerticalScrollMode = ScrollMode.Enabled,
            HorizontalScrollMode = ScrollMode.Disabled,
            IsTabStop = true,
            Background = new SolidColorBrush(Color.FromArgb(0, 0, 0, 0)),
            Content = body
        };
        var contentLayout = new Grid();
        contentLayout.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        contentLayout.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        contentLayout.Children.Add(_appearancePinned);
        Grid.SetRow(_contentScroll, 1);
        contentLayout.Children.Add(_contentScroll);
        Grid.SetColumn(contentLayout, 1);
        Root.Children.Add(contentLayout);
        _wheelRetryTimer = DispatcherQueue.CreateTimer();
        _wheelRetryTimer.Interval = TimeSpan.FromMilliseconds(24);
        _wheelRetryTimer.IsRepeating = true;
        _wheelRetryTimer.Tick += (_, _) =>
        {
            if (_wheelTarget is null)
            {
                _wheelRetryTimer.Stop();
                return;
            }
            if (Math.Abs(_contentScroll.VerticalOffset - _wheelTarget.Value) < 1 || ++_wheelRetries > 4)
            {
                _wheelTarget = null;
                _wheelRetryTimer.Stop();
                return;
            }
            _contentScroll.ChangeView(null, _wheelTarget.Value, null, true);
        };
        // Cover the entire viewport, including padding and scrollbar; listening
        // only on PageContent leaves blank margins without our wheel handler.
        _contentScroll.AddHandler(UIElement.PointerWheelChangedEvent,
            new PointerEventHandler(OnContentWheel), true);
        _contentScroll.ViewChanged += (_, _) => TraceWheel("view-changed");
        PageContent.BringIntoViewRequested += (_, e) =>
        {
            // Losing edit focus can enqueue a stale caret bring-into-view.
            // Cancel only that blank-click transition, not normal navigation,
            // explicit editors, palette requests or hardware wheel scrolling.
            if (_blankFocusActive && !_explicitEditFocus && Root.XamlRoot is not null &&
                ReferenceEquals(FocusManager.GetFocusedElement(Root.XamlRoot), _navButtons[_currentPage]))
                e.Handled = true;
        };
        PageContent.GettingFocus += (_, _) => _blankFocusActive = false;
        _contentScroll.AddHandler(UIElement.PointerPressedEvent,
            new PointerEventHandler(OnContentPressed), true);
        // Keep the focus target outside the scrollable content: focusing an
        // element inside it would trigger BringIntoView and jump the page.
        Root.Children.Add(_focusSink);
        Root.SizeChanged += (_, _) =>
        {
            var compact = Root.ActualWidth < 820;
            _sidebarColumn.Width = new GridLength(compact ? 64 : 220);
            foreach (var label in _navLabels)
                label.Visibility = compact ? Visibility.Collapsed : Visibility.Visible;
        };
        Content = Root;
    }

    private void OnContentWheel(object sender, PointerRoutedEventArgs e)
    {
        WheelEventCount++;
        var point = e.GetCurrentPoint(_contentScroll);
        var delta = point.Properties.MouseWheelDelta;
        TraceWheel($"xaml delta={delta} handled={e.Handled} x={point.Position.X:F1} y={point.Position.Y:F1} " +
            $"viewport={_contentScroll?.ActualWidth:F1}x{_contentScroll?.ActualHeight:F1} source={e.OriginalSource?.GetType().Name}");
        // The native ScrollViewer alone owns wheel scrolling. No focus changes,
        // ChangeView overrides, retries or Handled flags on the real input path.
    }

    internal int WheelEventCount { get; private set; }

    internal bool PostWheelForSmokeTest()
    {
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        if (!GetWindowRect(hwnd, out var rect)) return false;
        var x = rect.Left + 450;
        var y = rect.Top + 300;
        var lParam = (IntPtr)((y << 16) | (x & 0xffff));
        var wParam = (IntPtr)(unchecked((int)0xff880000)); // -120 wheel units.
        return PostMessage(hwnd, 0x020A, wParam, lParam);
    }

    private bool ScrollByWheelDelta(int delta)
    {
        if (_contentScroll is null || _contentScroll.ScrollableHeight <= 0 || delta == 0) return false;
        var next = Math.Clamp((_wheelTarget ?? _contentScroll.VerticalOffset) - delta / 120.0 * 88,
            0, _contentScroll.ScrollableHeight);
        // Scrolling the page ends an edit session. Keeping an off-screen
        // TextBox focused makes the next blank click bring it back into view.
        if (Root.XamlRoot is not null &&
            FocusManager.GetFocusedElement(Root.XamlRoot) is TextBox or PasswordBox)
        {
            _explicitEditFocus = false;
            _focusSink.Focus(FocusState.Programmatic);
        }
        _wheelTarget = next;
        _wheelRetries = 0;
        _contentScroll.ChangeView(null, next, null, true);
        _wheelRetryTimer?.Start();
        return true;
    }

    private void OnContentPressed(object sender, PointerRoutedEventArgs e)
    {
        if (IsInteractivePointerTarget(e.OriginalSource as DependencyObject)) return;
        e.Handled = true;
        FocusBlankContent();
    }

    private void FocusBlankContent()
    {
        _blankFocusActive = true;
        _explicitEditFocus = false;
        _focusResetTimer?.Stop();
        var revision = ++_blankFocusRevision;
        var target = _navButtons[_currentPage];
        target.Focus(FocusState.Pointer);
        // WinUI may promote the first tab stop after pointer bubbling. Keep
        // focus on the visible current-page navigation button, outside the
        // scroller; focusing ScrollViewer itself can revive a stale editor
        // focus rectangle and scroll back. Explicit edits cancel this callback.
        DispatcherQueue.TryEnqueue(() => {
            if (revision == _blankFocusRevision && !_explicitEditFocus)
                target.Focus(FocusState.Pointer);
        });
        TraceWheel("blank-pointer-press current-page-focus");
    }

    private bool IsInteractivePointerTarget(DependencyObject? source)
    {
        for (var element = source; element is not null;
             element = VisualTreeHelper.GetParent(element))
        {
            if (element is TextBox or PasswordBox or Microsoft.UI.Xaml.Controls.Primitives.ButtonBase or ToggleSwitch or ScrollBar or Slider or Expander or Thumb)
                return true;
            if (ReferenceEquals(element, PageContent)) break;
        }
        return false;
    }

    private void WatchExplicitEdit(TextBox input) => input.AddHandler(
        UIElement.PointerPressedEvent,
        new PointerEventHandler((_, _) =>
        {
            _explicitEditFocus = true;
            _blankFocusRevision++;
            _focusResetTimer?.Stop();
        }), true);

    private void WatchExplicitEdit(PasswordBox input) => input.AddHandler(
        UIElement.PointerPressedEvent,
        new PointerEventHandler((_, _) =>
        {
            _explicitEditFocus = true;
            _blankFocusRevision++;
            _focusResetTimer?.Stop();
        }), true);

    private void ScheduleFocusReset(Control target)
    {
        _focusResetTimer?.Stop();
        var timer = DispatcherQueue.CreateTimer();
        timer.Interval = TimeSpan.FromMilliseconds(150);
        timer.IsRepeating = false;
        _focusResetTimer = timer;
        timer.Tick += (_, _) =>
        {
            timer.Stop();
            if (!ReferenceEquals(_focusResetTimer, timer)) return;
            _focusResetTimer = null;
            if (!_explicitEditFocus &&
                FocusManager.GetFocusedElement(Root.XamlRoot) is TextBox or PasswordBox)
                target.Focus(FocusState.Programmatic);
        };
        timer.Start();
    }

    private void AddNavigationItem(StackPanel parent, string title, string key, Symbol icon)
    {
        var label = new TextBlock { Text = title, VerticalAlignment = VerticalAlignment.Center };
        _navLabels.Add(label);
        var contents = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 14 };
        contents.Children.Add(new SymbolIcon(icon) { Width = 20, Height = 20 });
        contents.Children.Add(label);
        var button = new Button
        {
            Content = contents, Height = 44, HorizontalAlignment = HorizontalAlignment.Stretch,
            HorizontalContentAlignment = HorizontalAlignment.Left, BorderThickness = new Thickness(0),
            Background = Brush(29, 29, 32), CornerRadius = new CornerRadius(8)
        };
        AutomationProperties.SetName(button, title);
        button.Click += (_, _) => SelectPage(key);
        _navButtons[key] = button;
        parent.Children.Add(button);
    }

    internal bool RunSelfTest()
    {
        try
        {
            if (PageContent.Children.Count < 3) return false;
            SelectPage("appearance");
            if (_width is null || _commentSize is null || _spacing is null ||
                _padding is null || _colors.Count != 11 || PageContent.Children.Count < 5)
                return false;
            AppWindow.Resize(new Windows.Graphics.SizeInt32(820, 600));
            AppWindow.Resize(new Windows.Graphics.SizeInt32(1480, 900));
            SelectPage("japanese");
            if (PageContent.Children.Count < 5) return false;
            SelectPage("chinese");
            if (PageContent.Children.Count < 5) return false;
            SelectPage("custom");
            if (PageContent.Children.Count < 5) return false;
            SelectPage("phrases");
            if (PageContent.Children.Count < 4) return false;
            SelectPage("clipboard");
            if (PageContent.Children.Count < 4) return false;
            SelectPage("maintenance");
            if (PageContent.Children.Count < 3) return false;
            SelectPage("hotkeys");
            if (PageContent.Children.Count < 3) return false;
            SelectPage("sentence");
            if (PageContent.Children.Count < 4 || _credentialKeyBox is null ||
                !IsInteractivePointerTarget(_credentialKeyBox)) return false;
            SelectPage("cloud");
            if (PageContent.Children.Count < 4) return false;
            SelectPage("japanese-table");
            if (PageContent.Children.Count < 5) return false;
            SelectPage("input");
            return PageContent.Children.Count >= 8;
        }
        catch
        {
            return false;
        }
    }

    internal async Task RunPaletteUiSelfTestAsync(string imagePath)
    {
        SelectPage("appearance");
        if (_colorPickers.Count != 11) throw new InvalidOperationException("Missing palettes");
        var expected = DrawingColor.FromArgb(255, 38, 170, 219);
        foreach (var (key, picker) in _colorPickers)
        {
            picker.Color = Color.FromArgb(expected.A, expected.R, expected.G, expected.B);
            if (_colors[key].ToArgb() != expected.ToArgb()) throw new InvalidOperationException("ColorChanged failed");
        }
        await Task.Delay(650);
        var saved = _store.ReadAppearance();
        var savedColors = new[] { saved.ChineseText, saved.ChineseBackground, saved.JapaneseText,
            saved.JapaneseBackground, saved.CommonPhraseText, saved.CommonPhraseBackground, saved.FuzzyCorrectionText,
            saved.SelectedText??DrawingColor.Empty,saved.SelectedBackground??DrawingColor.Empty,
            saved.AnnotationText??DrawingColor.Empty,saved.PanelBackground??DrawingColor.Empty };
        if (savedColors.Any(color => color.ToArgb() != expected.ToArgb()))
            throw new InvalidOperationException("Palette save/readback failed: " + _saveStatus?.Text);
        foreach (var reset in _colorResets.Values) reset();
        await Task.Delay(650);
        var restored = _store.ReadAppearance();
        if (new[] { restored.ChineseText, restored.ChineseBackground, restored.JapaneseText,
            restored.JapaneseBackground, restored.CommonPhraseText, restored.CommonPhraseBackground,
            restored.FuzzyCorrectionText }.Any(color => color.A != 0))
            throw new InvalidOperationException("Follow original did not persist transparency");
        SelectPage("appearance");
        if (_colors.Where(item=>item.Key is not ("selectedText" or "selectedBackground" or "annotationText" or "panelBackground")).Any(item => item.Value.A != 0)) throw new InvalidOperationException("Reopen lost defaults");
    }

    internal void ShowPalettePreview()
    {
        // Render the actual native palette without activation or mouse input.
        SelectPage("appearance");
    }

    internal async Task CapturePalettePreviewAsync(string imagePath)
    {
        Root.UpdateLayout();
        var choose = _colorChooseButtons["chineseText"];
        choose.StartBringIntoView();
        await Task.Delay(150);
        choose.Flyout.ShowAt(choose, new FlyoutShowOptions { ShowMode = FlyoutShowMode.Transient });
        await Task.Delay(200);
        var palette = _colorPickers["chineseText"];
        palette.Color = Color.FromArgb(255, 38, 170, 219);
        if (_colors["chineseText"].B != 219) throw new InvalidOperationException("Popup color did not update");
        var bitmap = new Microsoft.UI.Xaml.Media.Imaging.RenderTargetBitmap();
        await bitmap.RenderAsync(palette);
        var pixels = await bitmap.GetPixelsAsync();
        var folder = await Windows.Storage.StorageFolder.GetFolderFromPathAsync(Path.GetDirectoryName(imagePath)!);
        var file = await folder.CreateFileAsync(Path.GetFileName(imagePath), Windows.Storage.CreationCollisionOption.ReplaceExisting);
        using var stream = await file.OpenAsync(Windows.Storage.FileAccessMode.ReadWrite);
        var encoder = await Windows.Graphics.Imaging.BitmapEncoder.CreateAsync(
            Windows.Graphics.Imaging.BitmapEncoder.PngEncoderId, stream);
        using var reader = Windows.Storage.Streams.DataReader.FromBuffer(pixels);
        var data = new byte[pixels.Length];
        reader.ReadBytes(data);
        encoder.SetPixelData(Windows.Graphics.Imaging.BitmapPixelFormat.Bgra8,
            Windows.Graphics.Imaging.BitmapAlphaMode.Premultiplied,
            (uint)bitmap.PixelWidth, (uint)bitmap.PixelHeight, 96, 96, data);
        await encoder.FlushAsync();
    }

    internal (bool Passed, string Details) RunAnnotationUiSelfTest()
    {
        if (_englishAnnotationSwitch is null || _japaneseAnnotationSwitch is null ||
            _singleCharacterAnnotationSwitch is null)
            return (false, "输入页开关未创建");
        var initial = _store.ReadAnnotationHotState();
        _englishAnnotationSwitch.IsOn = true;
        var english = _store.ReadAnnotationHotState();
        _japaneseAnnotationSwitch.IsOn = true;
        var japanese = _store.ReadAnnotationHotState();
        _singleCharacterAnnotationSwitch.IsOn = false;
        var single = _store.ReadAnnotationHotState();
        var passed = initial is { English: false, Japanese: false, SingleCharacterAnnotations: true } &&
            english is { English: true, Japanese: false } &&
            japanese is { English: true, Japanese: true } &&
            single is { English: true, Japanese: true, SingleCharacterAnnotations: false } &&
            initial.Generation < english.Generation && english.Generation < japanese.Generation &&
            japanese.Generation < single.Generation;
        SelectPage("sentence");
        if (_preferNiuAnnotationsSwitch is null) return (false, "注释来源开关未创建");
        _preferNiuAnnotationsSwitch.IsOn = true;
        var preferNew = _store.ReadPreferNiuAnnotations();
        _preferNiuAnnotationsSwitch.IsOn = false;
        var preferOld = !_store.ReadPreferNiuAnnotations();
        return (passed && preferNew && preferOld, $"initial={initial}; english={english}; japanese={japanese}; single={single}; preferNew={preferNew}; preferOld={preferOld}; status={_saveStatus?.Text}");
    }

    internal void BeginSentenceToggleUiSelfTest()
    {
        _store.SaveNiuCredentials("test-app-id", "test-api-key");
        SelectPage("sentence");
        if (_sentenceTranslationSwitch is null)
            throw new InvalidOperationException("小牛翻译开关未创建。");
        _sentenceTranslationSwitch.IsOn = true;
    }

    internal bool SentenceToggleUiSelfTestState(bool expected)
    {
        using var key = Registry.CurrentUser.OpenSubKey(_store.HotSettingsRegistryPath);
        return _sentenceTranslationSwitch?.IsOn == expected &&
            _store.ReadInputOptions().Sentence == expected &&
            key?.GetValue("Option_sentence_translation") is int value &&
            (value != 0) == expected &&
            File.ReadAllText(_store.UserYaml).Contains(
                $"sentence_translation: {expected.ToString().ToLowerInvariant()}");
    }

    internal void SetSentenceToggleForUiSelfTest(bool enabled)
    {
        if (_sentenceTranslationSwitch is null)
            throw new InvalidOperationException("小牛翻译开关未创建。");
        _sentenceTranslationSwitch.IsOn = enabled;
    }

    internal string SentenceToggleUiSelfTestStatus => _saveStatus?.Text ?? "无状态提示";

    internal bool RunVisibleSmokeTest() => Root.IsLoaded && PageContent.Children.Count >= 3;

    internal bool ShowAppearanceForSmokeTest()
    {
        if (!Root.IsLoaded) return false;
        SelectPage("appearance");
        return PageContent.Children.Count >= 3;
    }

    internal async Task<string> RunBlankFocusUiSelfTestAsync()
    {
        SelectPage("appearance");
        await Task.Delay(180);
        Root.UpdateLayout();
        if (!IsInteractivePointerTarget(_width)) throw new InvalidOperationException("Editor is not interactive");
        var scroll = _contentScroll!;
        var log = new List<string>();
        foreach (var fraction in new[] { 0.0, 0.4, 0.8 })
        {
            _width!.Focus(FocusState.Programmatic);
            await Task.Delay(800);
            scroll.ChangeView(null, scroll.ScrollableHeight * fraction, null, true);
            await Task.Delay(800);
            var before = scroll.VerticalOffset;
            for (var index = 0; index < 4; index++)
            {
                FocusBlankContent();
                await Task.Delay(120);
                if (!ReferenceEquals(FocusManager.GetFocusedElement(Root.XamlRoot), _navButtons["appearance"]))
                    throw new InvalidOperationException("Blank focus returned to editor");
                if (Math.Abs(scroll.VerticalOffset - before) > 1)
                    throw new InvalidOperationException($"Blank focus changed viewport offset: fraction={fraction} before={before} after={scroll.VerticalOffset} iteration={index}");
            }
            log.Add($"offset={before:F1} repeated blank-handler: focus=visible navigation; no jump PASS");
        }
        FocusBlankContent();
        _explicitEditFocus = true;
        _blankFocusRevision++;
        _width!.Focus(FocusState.Programmatic);
        _width.Text = "640";
        await Task.Delay(160);
        if (!ReferenceEquals(FocusManager.GetFocusedElement(Root.XamlRoot), _width) || _width.Text != "640")
            throw new InvalidOperationException("Explicit editor focus was stolen");
        log.Add("explicit editor supersedes pending blank callback; edit works PASS");
        log.Add("UI-thread focus-handler regression only; real pointer/wheel still requires live verification");
        return string.Join("\n", log);
    }

    internal async Task<string> RunDpiHitUiSelfTestAsync()
    {
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var awareness = GetAwarenessFromDpiAwarenessContext(GetWindowDpiAwarenessContext(hwnd));
        if (awareness != 2) throw new InvalidOperationException($"Expected per-monitor awareness; actual={awareness}");
        var log = new List<string>();
        foreach (var size in new[] { new Windows.Graphics.SizeInt32(820, 600), new Windows.Graphics.SizeInt32(1120, 780) })
        {
            AppWindow.Resize(size);
            foreach (var page in new[] { "appearance", "input", "japanese" })
            {
                SelectPage(page);
                await Task.Delay(350);
                Root.UpdateLayout();
                var scale = Root.XamlRoot.RasterizationScale;
                var dpiScale = GetDpiForWindow(hwnd) / 96.0;
                if (Math.Abs(scale - dpiScale) > 0.01)
                    throw new InvalidOperationException($"DPI mismatch: XAML={scale} HWND={dpiScale}");
                var scroll = _contentScroll!;
                var origin = scroll.TransformToVisual(Root).TransformPoint(new Windows.Foundation.Point());
                foreach (var fraction in new[] { 0.1, 0.5, 0.9 })
                {
                    var point = new Windows.Foundation.Point(origin.X + scroll.ActualWidth * fraction,
                        origin.Y + scroll.ActualHeight * 0.5);
                    var hits = VisualTreeHelper.FindElementsInHostCoordinates(point, Root).ToList();
                    if (!hits.Contains(scroll))
                        throw new InvalidOperationException($"Content scroller missing from hit path: page={page} x={fraction}");
                    log.Add($"{size.Width}x{size.Height} page={page} x={fraction:P0}: scroller hit PASS; scale={scale:F2} dpi={GetDpiForWindow(hwnd)}");
                }
            }
        }
        log.Add("DPI and XAML geometric hit-path regression only; not hardware wheel injection");
        return string.Join("\n", log);
    }

    internal async Task<string> RunWheelRegressionAsync()
    {
        // PostMessage does not synthesize WinUI pointer input. The old test
        // merely exercised our intercepted scroll function, not a real wheel.
        if (Environment.GetCommandLineArgs().Contains("--wheel-ui-self-test", StringComparer.Ordinal))
            throw new InvalidOperationException("旧模拟滚轮验收已停用：必须核对真实滚轮产生的 xaml/view-changed 记录。");
        var lines = new List<string>();
        var inputSite = IntPtr.Zero;
        EnumChildWindows(_windowHandle, (hwnd, _) => {
            var name = new System.Text.StringBuilder(256);
            GetClassName(hwnd, name, name.Capacity);
            lines.Add("CHILD=" + name);
            if (name.ToString().Contains("InputSite", StringComparison.Ordinal)) inputSite = hwnd;
            return true;
        }, IntPtr.Zero);
        void PostWheel(double x, double y, int delta, IntPtr target = default)
        {
            var scale = Root.XamlRoot.RasterizationScale;
            var point = new WindowPoint { X = (int)(x * scale), Y = (int)(y * scale) };
            ClientToScreen(_windowHandle, ref point);
            var coordinates = (IntPtr)((point.Y << 16) | (point.X & 0xffff));
            if (!PostMessage(target == IntPtr.Zero ? _windowHandle : target, 0x020A, (IntPtr)(delta << 16), coordinates))
                throw new InvalidOperationException("Could not post wheel");
        }
        foreach (var size in new[] { new Windows.Graphics.SizeInt32(820, 600), new Windows.Graphics.SizeInt32(1120, 780) })
        {
            AppWindow.Resize(size);
            foreach (var page in new[] { "appearance", "input", "japanese", "chinese" })
            {
                SelectPage(page);
                await Task.Delay(160);
                Root.UpdateLayout();
                var scroll = _contentScroll!;
                if (scroll.ScrollableHeight < 88)
                {
                    lines.Add($"{size.Width}x{size.Height} {page} no-overflow; wheel ignored without retry PASS");
                    if (scroll.ScrollableHeight <= 0 && ScrollByWheelDelta(-120))
                        throw new InvalidOperationException("Empty page retained scroll target");
                    continue;
                }
                var origin = scroll.TransformToVisual(Root).TransformPoint(new Windows.Foundation.Point());
                foreach (var location in new[] { "center", "left-padding", "right-padding", "bottom-padding", "editor-focus" })
                {
                    scroll.ChangeView(null, 0, null, true);
                    await Task.Delay(80);
                    if (location == "editor-focus" && page == "appearance") _width!.Focus(FocusState.Programmatic);
                    var x = location == "left-padding" ? origin.X + 4 :
                        location == "right-padding" ? origin.X + scroll.ActualWidth - 20 : origin.X + scroll.ActualWidth / 2;
                    var y = origin.Y + (location == "bottom-padding" ? scroll.ActualHeight - 8 : scroll.ActualHeight / 2);
                    var before = scroll.VerticalOffset;
                    PostWheel(x, y, -120);
                    await Task.Delay(140);
                    var after = scroll.VerticalOffset;
                    if (after <= before + 1) throw new InvalidOperationException($"Wheel stuck: {page}/{location}");
                    PostWheel(x, y, 120);
                    await Task.Delay(140);
                    if (scroll.VerticalOffset >= after - 1) throw new InvalidOperationException($"Reverse stuck: {page}/{location}");
                    lines.Add($"{size.Width}x{size.Height} {page}/{location} {before:F0}->{after:F0}->{scroll.VerticalOffset:F0} PASS");
                }
                var previous = scroll.VerticalOffset;
                PostWheel(20, 180, -120);
                await Task.Delay(140);
                if (Math.Abs(scroll.VerticalOffset - previous) > 1) throw new InvalidOperationException("Sidebar wheel moved body");
                var centerX = origin.X + scroll.ActualWidth / 2;
                var centerY = origin.Y + scroll.ActualHeight / 2;
                for (var index = 0; index < 5; index++) PostWheel(centerX, centerY, -30);
                await Task.Delay(140);
                if (scroll.VerticalOffset < 40) throw new InvalidOperationException("High-resolution wheel stuck");
                for (var index = 0; index < 5; index++) PostWheel(centerX, centerY, 30);
                await Task.Delay(140);
                if (scroll.VerticalOffset > 1) throw new InvalidOperationException("Rapid reversal stuck");
                if (_wheelTarget is not null) throw new InvalidOperationException("Retry target did not release");
                lines.Add($"{page} sidebar/high-resolution/reversal/retry-release PASS");
                if (inputSite != IntPtr.Zero)
                {
                    var events = WheelEventCount;
                    PostWheel(centerX, centerY, -120, inputSite);
                    await Task.Delay(160);
                    if (scroll.VerticalOffset <= 1 || WheelEventCount <= events)
                        throw new InvalidOperationException("XAML input-site wheel did not scroll: " + page);
                    PostWheel(centerX, centerY, 120, inputSite);
                    await Task.Delay(160);
                    if (scroll.VerticalOffset > 1) throw new InvalidOperationException("XAML input-site reverse stuck");
                    lines.Add(page + " actual XAML input-site routed wheel PASS");
                }
            }
        }
        return string.Join("\n", lines);
    }

    internal bool ScrollAppearanceForSmokeTest()
    {
        if (_contentScroll is null || _contentScroll.ScrollableHeight < 100)
        {
            SmokeDiagnostic = $"height={_contentScroll?.ScrollableHeight}";
            return false;
        }
        var inputFocused = _width is not null && _width.Focus(FocusState.Programmatic);
        var wheel1 = ScrollByWheelDelta(-120);
        var cleared = ReferenceEquals(FocusManager.GetFocusedElement(Root.XamlRoot), _focusSink);
        var wheel2 = ScrollByWheelDelta(-120);
        var posted = PostWheelForSmokeTest();
        SmokeDiagnostic = $"programmatic-scroll-only(not hardware wheel) focusInput={inputFocused} focusSink={cleared} wheel1={wheel1} " +
            $"wheel2={wheel2} posted={posted} offset={_contentScroll.VerticalOffset} " +
            $"height={_contentScroll.ScrollableHeight}";
        return inputFocused && cleared && wheel1 && wheel2 && posted;
    }

    internal bool VerifyAppearanceScrollForSmokeTest()
    {
        var offset = _contentScroll?.VerticalOffset ?? -1;
        var focused = ReferenceEquals(FocusManager.GetFocusedElement(Root.XamlRoot), _focusSink);
        SmokeDiagnostic += $" finalOffset={offset} focusSinkStill={focused} wheelEvents={WheelEventCount}";
        return offset >= 175 && focused && WheelEventCount > 0;
    }

    internal bool ShowRulesForSmokeTest(bool japanese)
    {
        if (!Root.IsLoaded) return false;
        SelectPage(japanese ? "japanese" : "chinese");
        return PageContent.Children.Count >= 5;
    }

    internal static string ResolveUserDirectory()
    {
        using var key = Registry.CurrentUser.OpenSubKey(@"SOFTWARE\ZhongriInputMethod");
        var registered = key?.GetValue("ZhongriUserDir") as string;
        return string.IsNullOrWhiteSpace(registered)
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "ZhongriInputMethod")
            : Environment.ExpandEnvironmentVariables(registered);
    }

    private void SelectPage(string page)
    {
        FlushAutoSaves();
        _appearanceChanged = null;
        _blankFocusActive = false;
        _currentPage = page;
        _blankFocusRevision++;
        StopClipboardListener();
        foreach (var (key, button) in _navButtons)
            button.Background = key == page ? Brush(61, 58, 83) : Brush(29, 29, 32);
        PageContent.Children.Clear();
        _appearancePinned.Children.Clear();
        _appearancePinned.Visibility = Visibility.Collapsed;
        _appearancePreviewScroll = null;
        _selectAppearanceSection = null;
        if (_contentBody is not null) _contentBody.Padding = new Thickness(28, 24, 28, 96);
        _wheelTarget = null;
        _wheelRetryTimer?.Stop();
        _explicitEditFocus = false;
        _saveStatus = null;
        switch (page)
        {
            case "input": ShowInput(); break;
            case "global-profiles": ShowGlobalProfiles(); break;
            case "japanese-table": ShowJapaneseInputTable(); break;
            case "appearance": ShowAppearance(); break;
            case "japanese": ShowJapaneseFuzzy(); break;
            case "chinese": ShowChineseCorrections(); break;
            case "custom": ShowCustomFuzzyRules(); break;
            case "phrases": ShowPhrases(); break;
            case "clipboard": ShowClipboard(); break;
            case "maintenance": ShowMaintenance(); break;
            case "hotkeys": ShowHotkeys(); break;
            case "sentence": ShowSentenceTranslation(); break;
            case "cloud": ShowGoogleCloudCandidates(); break;
            default: ShowPending(_navButtons[page].Content is StackPanel item
                ? item.Children.OfType<TextBlock>().FirstOrDefault()?.Text ?? "设置" : "设置"); break;
        }
        _contentScroll?.ChangeView(null, 0, null, true);
        // Keep navigation focus instead of letting WinUI pick the first edit box.
        DispatcherQueue.TryEnqueue(() =>
        {
            if (_navButtons.TryGetValue(page, out var button))
                button.Focus(FocusState.Programmatic);
        });
        if (Root.IsLoaded) ScheduleFocusReset(_navButtons[page]);
    }

    private void ShowInput()
    {
        AddHeading("输入", "候选词注释即时生效；其余输入设置按需刷新服务或配置。");
        var options = _store.ReadInputOptions();
        TraceAnnotationSetting("page", $"ui=en:{options.English},ja:{options.Japanese},single:{options.SingleCharacterAnnotations}");
        _saveStatus = new TextBlock { Text = _uiTestMode ? "测试模式：开关不会写入配置" : "显示的是当前实际生效的热设置",
            Foreground = Brush(143, 205, 177), TextWrapping = TextWrapping.Wrap };
        PageContent.Children.Add(_saveStatus);
        var rows = new StackPanel();
        rows.Children.Add(SwitchRow("显示英文注释", "候选词下方显示简短英文释义", options.English,
            value => SaveAnnotations(english: value), out _englishAnnotationSwitch));
        rows.Children.Add(Divider());
        rows.Children.Add(SwitchRow("显示日文注释", "候选词下方显示日语释义", options.Japanese,
            value => SaveAnnotations(japanese: value), out _japaneseAnnotationSwitch));
        rows.Children.Add(Divider());
        rows.Children.Add(SwitchRow("单字候选显示注释", "关闭后单个汉字、假名或字母只显示候选文字", options.SingleCharacterAnnotations,
            value => SaveAnnotations(singleCharacter: value), out _singleCharacterAnnotationSwitch));
        rows.Children.Add(Divider());
        rows.Children.Add(SwitchRow("显示日语直输读音", "只控制正常日语输入；中文翻译和模糊匹配仍显示假名", options.DirectJapaneseReading,
            value => SaveAnnotations(directReading: value), out _));
        rows.Children.Add(Divider());
        rows.Children.Add(SwitchRow("日语候选隐藏中英文注释", "只隐藏日语候选的翻译；纠错读音始终保留，中文候选不受影响。切换立即生效。",
            _store.ReadHideJapaneseTranslationAnnotations(), value =>
            {
                if (_uiTestMode) { SetStatus("测试模式：开关已响应，未写入配置", false); return; }
                try
                {
                    _store.SaveHideJapaneseTranslationAnnotations(value);
                    if (_store.ReadHideJapaneseTranslationAnnotations() != value)
                        throw new InvalidOperationException("日语注释热设置未保存");
                    SetStatus(value ? "日语翻译注释已隐藏；纠错读音保留" : "日语翻译注释已显示；纠错读音保留", false);
                }
                catch (Exception error) { SetStatus("未能保存：" + error.Message, true); }
            }, out _));
        PageContent.Children.Add(Card("翻译注释", "这些开关均即时保存，无需点击应用。", rows));
        ShowOtherInputOptions(options);
    }

    private void ShowJapaneseFuzzy()
    {
        AddHeading("日语模糊匹配", "容错规则独立管理；关闭总开关不会清除下面的选择。");
        PageContent.Children.Add(Card("日语学习", "标准罗马字和自定义双拼均可使用。", BuildJapaneseLearningModeSwitch()));
        var values = _store.ReadInputOptions();
        var master = TrackedSwitchRow("日语模糊匹配总开关", "只启用下面选中的规则", values.Fuzzy, out var masterToggle);
        PageContent.Children.Add(Card("总体设置", "应用后立即作用于已打开的输入窗口。", master));
        var rules = new (string Title, string Description, bool Enabled)[]
        {
            ("促音省略", "kitte 输入 kite 也能匹配", values.FuzzySokuon),
            ("长音 い", "省略表示长音的 i 也能匹配", values.FuzzyLongI),
            ("长音 う", "省略表示长音的 u 也能匹配", values.FuzzyLongU),
            ("片假名长音 ー", "例如 kopii 也能匹配 コピー", values.FuzzyLongMark),
            ("ち / じ", "chi 与 ji 可互相容错匹配", values.FuzzyChiJi),
            ("ふ：hu / fu", "词首或词中 hu 与 fu 可互相容错", values.FuzzyHuFu),
            ("しゅ / しょ", "shu 与 sho 可互相容错匹配", values.FuzzyShuSho),
            ("け / かい：ke / kai", "例如 seke 也能匹配 世界（sekai）", values.FuzzyKeKai),
            ("ke / kae / gae", "例如 kikeru 也能匹配 着替える（kigaeru）", values.FuzzyKeKaeGae),
            ("せい / さい：sei / sai", "sei 与 sai 可互相容错匹配", values.FuzzySeiSai),
            ("浊音 / 半浊音", "t/d、p/b/h 容错，可与促音组合", values.FuzzyDakuten),
            ("wa → は（助词自动转换）", "关闭时请输入 ha；开启后句中 wa 可写作 は", values.ParticleWaHa),
            ("允许多条模糊规则同时生效", "只叠加已勾选的规则，最多连续纠正3步；关闭保留原有组合纠错。", values.FuzzyMultiple)
        };
        var toggles = new List<ToggleSwitch>();
        var list = new StackPanel();
        foreach (var rule in rules)
        {
            if (list.Children.Count > 0) list.Children.Add(Divider());
            list.Children.Add(TrackedSwitchRow(rule.Title, rule.Description, rule.Enabled, out var toggle));
            toggles.Add(toggle);
        }
        PageContent.Children.Add(Card("容错规则", "规则只在总开关开启时生效。", list));
        AutoSaveStatus();
        var changed = AutoSave(() =>
        {
            var current = _store.ReadInputOptions();
            var updated = current with
            {
                Fuzzy = masterToggle.IsOn,
                FuzzySokuon = toggles[0].IsOn,
                FuzzyLongI = toggles[1].IsOn,
                FuzzyLongU = toggles[2].IsOn,
                FuzzyLongMark = toggles[3].IsOn,
                FuzzyChiJi = toggles[4].IsOn,
                FuzzyHuFu = toggles[5].IsOn,
                FuzzyShuSho = toggles[6].IsOn,
                FuzzyKeKai = toggles[7].IsOn,
                FuzzyKeKaeGae = toggles[8].IsOn,
                FuzzySeiSai = toggles[9].IsOn,
                FuzzyDakuten = toggles[10].IsOn,
                ParticleWaHa = toggles[11].IsOn,
                FuzzyMultiple = toggles[12].IsOn
            };
            _store.SaveInputOptions(updated);
        });
        masterToggle.Toggled += (_, _) => changed();
        foreach (var toggle in toggles) toggle.Toggled += (_, _) => changed();
    }

    private void ShowChineseCorrections()
    {
        AddHeading("中文模糊纠错", "调整中文拼音的键序、韵母等自动纠错规则。");
        try
        {
            var rules = _chineseCorrections.Load();
            if (rules.Count == 0)
            {
                PageContent.Children.Add(Info("没有从当前方案文件读到中文纠错规则。请确认已安装完整词库。"));
                return;
            }
            var master = TrackedSwitchRow("全部中文自动纠错", "统一开启或关闭下方全部规则",
                rules.All(rule => rule.Enabled), out var masterToggle);
            PageContent.Children.Add(Card("总体设置", "运行时切换，无需部署或重启。", master));
            var toggles = new List<ToggleSwitch>();
            var list = new StackPanel();
            var changingAll = false;
            foreach (var rule in rules)
            {
                if (list.Children.Count > 0) list.Children.Add(Divider());
                list.Children.Add(TrackedSwitchRow(rule.Title, rule.Description, rule.Enabled, out var toggle));
                toggles.Add(toggle);
            }
            masterToggle.Toggled += (_, _) =>
            {
                if (changingAll) return;
                changingAll = true;
                foreach (var toggle in toggles) toggle.IsOn = masterToggle.IsOn;
                changingAll = false;
            };
            foreach (var toggle in toggles)
                toggle.Toggled += (_, _) =>
                {
                    if (changingAll) return;
                    changingAll = true;
                    masterToggle.IsOn = toggles.All(item => item.IsOn);
                    changingAll = false;
                };
            PageContent.Children.Add(Card($"纠错规则 · {rules.Count} 条", "仅选中的规则会写入方案。", list));
            AutoSaveStatus("修改后自动保存并热生效，不重启服务。");
            var changed = AutoSave(() =>
            {
                var updated = rules.Select((rule, index) => rule with { Enabled = toggles[index].IsOn }).ToList();
                _chineseCorrections.Save(updated);
                _store.PublishChineseFuzzyReload();
            }, delay: 350);
            masterToggle.Toggled += (_, _) => { if (!changingAll) changed(); };
            foreach (var toggle in toggles) toggle.Toggled += (_, _) => { if (!changingAll) changed(); };
        }
        catch (Exception error)
        {
            PageContent.Children.Add(Info("无法读取中文纠错规则：" + error.Message));
        }
    }

    private static Grid TrackedSwitchRow(string title, string description, bool value, out ToggleSwitch toggle)
    {
        toggle = new ToggleSwitch { IsOn = value, OnContent = "", OffContent = "" };
        AutomationProperties.SetName(toggle, title);
        return ControlRow(title, description, toggle);
    }

    private void SaveAnnotations(bool? english = null, bool? japanese = null,
        bool? directReading = null, bool? singleCharacter = null)
    {
        var changed = english.HasValue ? "英文注释" : japanese.HasValue ? "日文注释" :
            singleCharacter.HasValue ? "单字注释" : "日语直输读音";
        TraceAnnotationSetting("toggle", $"item={changed}; en={english}; ja={japanese}; single={singleCharacter}");
        if (_uiTestMode)
        {
            SetStatus("测试模式：开关已响应，未写入配置", false);
            return;
        }
        try
        {
            var current = _store.ReadInputOptions();
            var expectedEnglish = english ?? current.English;
            var expectedJapanese = japanese ?? current.Japanese;
            var expectedDirectReading = directReading ?? current.DirectJapaneseReading;
            var expectedSingleCharacter = singleCharacter ?? current.SingleCharacterAnnotations;
            var before = _store.ReadAnnotationHotState();
            SetStatus("正在更新当前输入窗口…", false);
            _store.SaveAnnotationOptions(expectedEnglish, expectedJapanese,
                expectedDirectReading, expectedSingleCharacter);
            var after = _store.ReadAnnotationHotState();
            if (after is null || after.English != expectedEnglish ||
                after.Japanese != expectedJapanese ||
                after.DirectJapaneseReading != expectedDirectReading ||
                after.SingleCharacterAnnotations != expectedSingleCharacter ||
                (before is not null && after.Generation == before.Generation))
                throw new InvalidOperationException("热设置未写入当前用户，候选窗不会更新。 ");
            TraceAnnotationSetting("saved", $"item={changed}; revision={after.Generation}");
            SetStatus($"{changed}已{(english ?? japanese ?? singleCharacter ?? directReading ?? false ? "开" : "关")}；热设置版本 {after.Generation}", false);
        }
        catch (Exception error)
        {
            TraceAnnotationSetting("error", $"item={changed}; {error}");
            SetStatus("未能保存：" + error.Message, true);
            PageContent.Children.Add(Info("注释设置未保存：" + error.Message));
        }
    }

    private void TraceAnnotationSetting(string phase, string detail)
    {
        try
        {
            var sid = System.Security.Principal.WindowsIdentity.GetCurrent().User?.Value;
            using var usersKey = sid is null ? null : Registry.Users.OpenSubKey(
                sid + "\\" + _store.HotSettingsRegistryPath);
            var line = $"{DateTime.Now:O} pid={Environment.ProcessId} {phase} {detail}; " +
                $"hkcu={_store.ReadAnnotationHotState()}; " +
                $"hku=en:{usersKey?.GetValue("ShowEnglishAnnotation")}," +
                $"ja:{usersKey?.GetValue("ShowJapaneseAnnotation")}," +
                $"gen:{usersKey?.GetValue("AnnotationSettingsGeneration")}; " +
                $"path={_store.HotSettingsRegistryPath}\n";
            File.AppendAllText(Path.Combine(Path.GetTempPath(),
                "ZhongriSettingsV2-hot-annotation.log"), line);
        }
        catch { /* diagnostics must never interrupt typing or saving */ }
    }

    private void ShowAppearance()
    {
        AppearanceTestStep("appearance begin");
        var heading = new TextBlock { Text="外观", FontSize=28 };
        ToolTipService.SetToolTip(heading,"拖动、选择颜色或修改数值后自动保存，即时更新候选窗口。");
        _appearancePinned.Children.Add(heading);
        _appearancePinned.Visibility = Visibility.Visible;
        if (_contentBody is not null) _contentBody.Padding = new Thickness(28, 0, 28, 96);
        var value = _store.ReadAppearance();
        _width = Number(520, 1200, 20, value.Width);
        _commentSize = Number(8, 18, 1, value.CommentSize);
        _spacing = Number(4, 36, 1, value.CandidateSpacing);
        _padding = Number(2, 18, 1, value.HighlightPadding);
        _candidateSize = Number(12,40,1,value.CandidateSize);
        _candidateMinHeight=Number(0,500,1,value.MinHeight);
        var layout = new StackPanel();
        layout.Children.Add(ControlRow("候选框宽度", "普通单行与展开状态的基准宽度", _width));
        layout.Children.Add(ControlRow("候选框最小高度","下边框拖动调整；不会裁掉内容，0 表示自动",_candidateMinHeight));
        layout.Children.Add(Divider());
        layout.Children.Add(ControlRow("注释字号", "英文和日语注释字号", _commentSize));
        layout.Children.Add(ControlRow("候选字号","候选词正文大小",_candidateSize));
        layout.Children.Add(Divider());
        layout.Children.Add(ControlRow("候选间距", "候选词之间的横向空隙", _spacing));
        layout.Children.Add(Divider());
        layout.Children.Add(ControlRow("选中项留白", "高亮背景与文字之间的内边距", _padding));
        var dimensionCard=Card("尺寸与间距", "精确数字微调；拖动预览也会同步这些值。", layout);

        _colors.Clear();
        _colors["chineseText"] = value.ChineseText;
        _colors["chineseBackground"] = value.ChineseBackground;
        _colors["japaneseText"] = value.JapaneseText;
        _colors["japaneseBackground"] = value.JapaneseBackground;
        _colors["phraseText"] = value.CommonPhraseText;
        _colors["phraseBackground"] = value.CommonPhraseBackground;
        _colors["fuzzyCorrectionText"] = value.FuzzyCorrectionText;
        _colors["reservedText"] = value.ReservedText??DrawingColor.FromArgb(255,74,222,128);
        _colors["selectedText"]=value.SelectedText??DrawingColor.White;
        _colors["selectedBackground"]=value.SelectedBackground??DrawingColor.FromArgb(255,20,184,196);
        _colors["annotationText"]=value.AnnotationText??DrawingColor.LightGray;
        _colors["panelBackground"]=value.PanelBackground??DrawingColor.FromArgb(255,64,64,64);
        _colorPickers.Clear();
        _colorChooseButtons.Clear();
        _colorResets.Clear();
        AppearanceTestStep("palettes begin");
        var colors = new StackPanel();
        AddColorRow(colors, "chineseText", "中文文字颜色", "中文候选词使用的颜色");
        AddColorRow(colors, "chineseBackground", "中文底色", "透明表示跟随原版主题");
        AddColorRow(colors, "japaneseText", "日语文字颜色", "包括由汉字组成的日语词");
        AddColorRow(colors, "japaneseBackground", "日语底色", "透明表示跟随原版主题");
        AddColorRow(colors, "phraseText", "常用语文字颜色", "常用语候选独立配色");
        AddColorRow(colors, "phraseBackground", "常用语底色", "透明表示跟随原版主题");
        AddColorRow(colors, "fuzzyCorrectionText", "日语纠错字颜色", "只给模糊匹配中与原输入不同的假名上色，默认黄色");
        AddColorRow(colors, "reservedText", "保留字片段颜色", "只给候选中来自保留字的片段上色，默认绿色；保存后热更新");
        AddColorRow(colors,"selectedText","选中项文字","当前高亮候选的文字颜色");
        AddColorRow(colors,"selectedBackground","选中项背景","当前高亮候选的底色");
        AddColorRow(colors,"annotationText","注释文字","翻译注释的颜色");
        AddColorRow(colors,"panelBackground","候选框背景","整个候选框的底色");
        AppearanceTestStep("palettes finished");
        _appearancePinned.Children.Add(BuildAppearanceDesigner());
        AppearanceTestStep("preview attached");
        var colorCard=Card("颜色","点击色块选择颜色，改动自动保存。",colors);
        var sections=new Grid { ColumnSpacing=12 };
        sections.ColumnDefinitions.Add(new ColumnDefinition { Width=new GridLength(1,GridUnitType.Star) });
        sections.ColumnDefinitions.Add(new ColumnDefinition { Width=new GridLength(1,GridUnitType.Star) });
        var dimensions=new ToggleButton { Content="尺寸与间距",HorizontalAlignment=HorizontalAlignment.Stretch };
        var palette=new ToggleButton { Content="颜色",HorizontalAlignment=HorizontalAlignment.Stretch };
        AutomationProperties.SetAutomationId(dimensions,"AppearanceDimensionsTab");
        AutomationProperties.SetAutomationId(palette,"AppearanceColorsTab");
        void SelectSection(bool showColors) {
            dimensions.IsChecked=!showColors; palette.IsChecked=showColors;
            dimensions.Background=showColors?Brush(55,56,61):Brush(61,58,83);
            palette.Background=showColors?Brush(61,58,83):Brush(55,56,61);
            dimensions.Foreground=palette.Foreground=Brush(242,242,245);
            dimensions.BorderBrush=palette.BorderBrush=Brush(88,88,100);
            dimensionCard.Visibility=showColors?Visibility.Collapsed:Visibility.Visible;
            colorCard.Visibility=showColors?Visibility.Visible:Visibility.Collapsed;
            _contentScroll?.ChangeView(null,0,null,true);
        }
        dimensions.Click+=(_,_)=>SelectSection(false);
        palette.Click+=(_,_)=>SelectSection(true);
        _selectAppearanceSection=SelectSection;
        sections.Children.Add(dimensions); Grid.SetColumn(palette,1); sections.Children.Add(palette);
        _appearancePinned.Children.Add(sections);
        PageContent.Children.Add(dimensionCard); PageContent.Children.Add(colorCard);
        SelectSection(false);
        AppearanceTestStep("advanced attached");

        AutoSaveStatus();
        var appearanceChanged = AutoSave(() => _store.SaveAppearanceHot(CaptureAppearance()),
            values:()=>string.Join("|",new[]{_width!.Text,_commentSize!.Text,_spacing!.Text,_padding!.Text,_candidateSize!.Text,_candidateMinHeight!.Text})+string.Join("|",_colors.Values.Select(c=>c.ToArgb())));
        _appearanceChanged=appearanceChanged;
        foreach (var field in new[] { _width, _commentSize, _spacing, _padding, _candidateSize, _candidateMinHeight })
            field!.TextChanged += (_, _) => appearanceChanged();
        AppearanceTestStep("appearance finished");
    }

    private Action? _appearanceChanged;
    private AppearanceOptions CaptureAppearance() => new(
        ValidNumber(_width!,520,1200), ValidNumber(_commentSize!,8,18),
        ValidNumber(_spacing!,4,36), ValidNumber(_padding!,2,18),
        _colors["chineseText"], _colors["chineseBackground"],
        _colors["japaneseText"], _colors["japaneseBackground"],
        _colors["phraseText"], _colors["phraseBackground"],
        _colors["fuzzyCorrectionText"], ValidNumber(_candidateSize!,12,40),
        _colors["selectedText"], _colors["selectedBackground"], _colors["annotationText"],
        _colors["panelBackground"], ValidNumber(_candidateMinHeight!,0,500),_colors["reservedText"]);

    private void AddColorRow(StackPanel parent, string key, string title, string description)
    {
        if (parent.Children.Count > 0) parent.Children.Add(Divider());
        var initial = _colors[key];
        var picker = new ColorPalette {
            Color = Color.FromArgb(255, initial.A == 0 ? (byte)255 : initial.R,
                initial.A == 0 ? (byte)255 : initial.G, initial.A == 0 ? (byte)255 : initial.B)
        };
        AutomationProperties.SetAutomationId(picker, "AppearanceColorPicker_" + key);
        AutomationProperties.SetName(picker, title + "调色板");
        _colorPickers[key] = picker;
        var swatch = new Border { Width = 28, Height = 28, CornerRadius = new CornerRadius(5),
            BorderBrush = Brush(140, 141, 150), BorderThickness = new Thickness(1) };
        var globalDefault=key switch { "reservedText"=>DrawingColor.FromArgb(255,74,222,128),"selectedText"=>DrawingColor.White,"selectedBackground"=>DrawingColor.FromArgb(255,20,184,196),"annotationText"=>DrawingColor.FromArgb(255,210,210,210),"panelBackground"=>DrawingColor.FromArgb(255,64,64,64),_=>DrawingColor.FromArgb(0,0,0,0) };
        var reset = new Button { Content = globalDefault.A==0?"跟随原版":"恢复默认" };
        var label = new TextBlock { VerticalAlignment = VerticalAlignment.Center };
        var buttonContent = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10 };
        buttonContent.Children.Add(swatch);
        buttonContent.Children.Add(label);
        var choose = new Button { Content = buttonContent, MinWidth = 140 };
        _colorChooseButtons[key] = choose;
        AutomationProperties.SetAutomationId(choose, "AppearanceChooseColor_" + key);
        AutomationProperties.SetName(choose, title + "，选择颜色");
        var palette = new StackPanel { Spacing = 12 };
        palette.Children.Add(picker);
        var useColor = new Button { Content = "关闭调色板", HorizontalAlignment = HorizontalAlignment.Stretch };
        palette.Children.Add(useColor);
        choose.Flyout = new Flyout { Content = palette, Placement = FlyoutPlacementMode.Bottom,
            FlyoutPresenterStyle = new Style(typeof(FlyoutPresenter)) {
                Setters = { new Setter(FrameworkElement.RequestedThemeProperty, ElementTheme.Dark) }
            } };
        void UpdateSwatch()
        {
            var color = _colors[key];
            swatch.Background = color.A == 0 ? Brush(55, 56, 62) : Brush(color.R, color.G, color.B);
            label.Text = color.A == 0 ? "选择颜色" : "更改颜色";
        }
        UpdateSwatch();
        // Palette choices are applied automatically; this button only closes it.
        useColor.Click += (_, _) => {
            choose.Flyout.Hide();
        };
        picker.ColorChanged += color =>
        {
            _colors[key] = DrawingColor.FromArgb(255, color.R, color.G, color.B);
            UpdateSwatch();
            _appearanceChanged?.Invoke();
            RefreshAppearanceDesigner();
        };
        _colorResets[key] = () => {
            _colors[key] = globalDefault;
            UpdateSwatch();
            _appearanceChanged?.Invoke();
            RefreshAppearanceDesigner();
        };
        reset.Click += (_, _) => _colorResets[key]();
        var controls = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        controls.Children.Add(reset);
        controls.Children.Add(choose);
        parent.Children.Add(ControlRow(title, description, controls));
    }

    private void SetStatus(string message, bool error)
    {
        if (_saveStatus is not null)
        {
            _saveStatus.Text = message;
            _saveStatus.Foreground = error ? Brush(239, 139, 139) : Brush(143, 205, 177);
        }
    }

    private void ShowPending(string title)
    {
        AddHeading(title, "新版界面正在迁移这一页。");
        PageContent.Children.Add(Info("目前请继续在旧版设置面板使用此功能。新面板接完全部功能并通过缩放、键盘及数据回归测试后才会替换旧版。"));
    }

    private void AddHeading(string title, string subtitle)
    {
        PageContent.Children.Add(new TextBlock { Text = title, Style = (Style)Microsoft.UI.Xaml.Application.Current.Resources["TitleTextBlockStyle"],
            Margin = new Thickness(0, 0, 0, 0) });
        PageContent.Children.Add(new TextBlock { Text = subtitle, TextWrapping = TextWrapping.Wrap,
            Foreground = Brush(170, 171, 182), Margin = new Thickness(0, -7, 0, 11) });
    }

    private static Border Card(string title, string subtitle, UIElement content)
    {
        var stack = new StackPanel { Spacing = 10 };
        if (!string.IsNullOrEmpty(title))
            stack.Children.Add(new TextBlock { Text = title, FontSize = 17,
                FontWeight = Microsoft.UI.Text.FontWeights.SemiBold });
        if (!string.IsNullOrEmpty(subtitle))
            stack.Children.Add(new TextBlock { Text = subtitle,
                Foreground = Brush(157, 159, 171), TextWrapping = TextWrapping.Wrap });
        stack.Children.Add(content);
        return new Border { Background = Brush(39, 40, 44), BorderBrush = Brush(67, 68, 76),
            BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(13),
            Padding = new Thickness(24, 20, 24, 16), Child = stack };
    }

    private static Border Info(string message) => new()
    {
        Background = Brush(65, 60, 92), CornerRadius = new CornerRadius(9),
        Padding = new Thickness(16),
        Child = new TextBlock { Text = message, TextWrapping = TextWrapping.Wrap, Foreground = Brush(213, 208, 243) }
    };

    private static Border Divider() => new() { Height = 1, Background = Brush(56, 57, 63), Margin = new Thickness(0, 2, 0, 2) };

    private static Grid ControlRow(string title, string description, FrameworkElement control)
    {
        var row = new Grid { MinHeight = 78, ColumnSpacing = 24 };
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var text = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Spacing = 3 };
        text.Children.Add(new TextBlock { Text = title, FontSize = 14, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
            TextWrapping = TextWrapping.Wrap });
        text.Children.Add(new TextBlock { Text = description, FontSize = 12, Foreground = Brush(165, 166, 177),
            TextWrapping = TextWrapping.Wrap });
        Grid.SetColumn(text, 0);
        row.Children.Add(text);
        control.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(control, 1);
        row.Children.Add(control);
        return row;
    }

    private static Grid SwitchRow(string title, string description, bool value,
        Action<bool> onChange, out ToggleSwitch toggle)
    {
        var created = new ToggleSwitch { IsOn = value, OnContent = "", OffContent = "" };
        toggle = created;
        AutomationProperties.SetName(created, title);
        created.Toggled += (_, _) => onChange(created.IsOn);
        return ControlRow(title, description, created);
    }

    private TextBox Number(double min, double max, double step, double value)
    {
        var input = new TextBox { Text = value.ToString(CultureInfo.InvariantCulture), Width = 112,
            MaxLength = 5, HorizontalTextAlignment = TextAlignment.Right };
        WatchExplicitEdit(input);
        return input;
    }

    private static int SafeNumber(TextBox input, int minimum, int maximum)
    {
        if (!int.TryParse(input.Text, NumberStyles.Integer, CultureInfo.InvariantCulture, out var number) ||
            number < minimum || number > maximum)
            throw new FormatException($"请输入 {minimum} 到 {maximum} 之间的整数。");
        return number;
    }

    private static SolidColorBrush Brush(byte red, byte green, byte blue)
        => new(Color.FromArgb(255, red, green, blue));

}
