using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.Storage.Pickers;

namespace ZhongriSettingsV2;

public sealed partial class MainWindow
{
    private ComboBox? _tableProfilePicker;
    private ToggleSwitch? _tableEnabledSwitch;
    private TextBox? _tablePreviewInput;
    private TextBlock? _tablePreviewResult;
    private ToggleSwitch? _tableMixedSwitch;
    private ToggleSwitch? _tableOverlapSwitch;
    private ToggleSwitch? _tableSingleTailSwitch;
    private ToggleSwitch? _tableReservedWholeSwitch;
    private ToggleSwitch? _tableLettersSwitch;
    private FrameworkElement? _reservedCard;
    private CheckBox? _reservedMaster;
    private TextBox? _reservedSearchInput;
    private ListView? _reservedSearchResults;
    private TextBlock? _reservedSearchStatus;
    private Func<Task>? _reservedSearchRun;
    private ComboBox? _reservedSearchEnabledFilter;
    private ComboBox? _reservedSearchGroupFilter;
    private readonly Dictionary<string, CheckBox> _reservedChecks = new();
    private readonly Dictionary<string, Action<bool>> _reservedGroupSelections = new();
    private readonly List<Grid> _reservedGrids = new();
    // Use the panel's standard Button style. The native Expander's generic.xaml
    // resource cannot be resolved by this self-contained, unpackaged deployment.
    private sealed class ReservedCategory : StackPanel
    {
        private readonly Button _header;
        private readonly FrameworkElement _body;
        private readonly string _title;
        private bool _expanded;
        public Border GroupBorder { get; }
        public FrameworkElement GroupBody => _body;
        public event Action<bool>? ExpandedChanged;
        public bool IsExpanded
        {
            get => _expanded;
            set
            {
                if (_expanded == value) return;
                _expanded = value;
                _body.Visibility = value ? Visibility.Visible : Visibility.Collapsed;
                _header.Content = (value ? "▾  " : "▸  ") + _title;
                ExpandedChanged?.Invoke(value);
            }
        }
        public ReservedCategory(string title, FrameworkElement body, bool expanded)
        {
            _title = title; _body = body;
            HorizontalAlignment = HorizontalAlignment.Stretch;
            GroupBorder = new Border { Background = Brush(32, 33, 37),
                BorderBrush = Brush(82, 83, 94), BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(16), Padding = new Thickness(8),
                HorizontalAlignment = HorizontalAlignment.Stretch };
            var content = new StackPanel { Spacing = 8 };
            _header = new Button { Content = "▸  " + title,
                HorizontalAlignment = HorizontalAlignment.Stretch,
                HorizontalContentAlignment = HorizontalAlignment.Left,
                Background = new Microsoft.UI.Xaml.Media.SolidColorBrush(Microsoft.UI.Colors.Transparent),
                BorderThickness = new Thickness(0), Padding = new Thickness(12, 10, 12, 10),
                FontSize = 16, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold };
            _body.Margin = new Thickness(12, 0, 12, 12);
            _body.Visibility = Visibility.Collapsed;
            _header.Click += (_, _) => IsExpanded = !IsExpanded;
            content.Children.Add(_header); content.Children.Add(_body);
            GroupBorder.Child = content;
            Children.Add(GroupBorder);
            IsExpanded = expanded;
        }
    }
    private readonly Dictionary<string, ReservedCategory> _reservedExpanders = new();
    internal async Task WriteAzikGroupPreviewAsync(string path)
    {
        var bitmap = new Microsoft.UI.Xaml.Media.Imaging.RenderTargetBitmap();
        await bitmap.RenderAsync(Environment.GetCommandLineArgs().Contains("--keyboard-diagram-preview")
            ? _japaneseKeyboardDiagram! : _reservedCard!);
        var pixels = await bitmap.GetPixelsAsync();
        var folder = await Windows.Storage.StorageFolder.GetFolderFromPathAsync(Path.GetDirectoryName(path)!);
        var file = await folder.CreateFileAsync(Path.GetFileName(path), Windows.Storage.CreationCollisionOption.ReplaceExisting);
        using var stream = await file.OpenAsync(Windows.Storage.FileAccessMode.ReadWrite);
        var encoder = await Windows.Graphics.Imaging.BitmapEncoder.CreateAsync(
            Windows.Graphics.Imaging.BitmapEncoder.PngEncoderId, stream);
        using var reader = Windows.Storage.Streams.DataReader.FromBuffer(pixels);
        var data = new byte[pixels.Length];
        reader.ReadBytes(data);
        encoder.SetPixelData(Windows.Graphics.Imaging.BitmapPixelFormat.Bgra8,
            Windows.Graphics.Imaging.BitmapAlphaMode.Premultiplied, (uint)bitmap.PixelWidth,
            (uint)bitmap.PixelHeight, 96, 96, data);
        await encoder.FlushAsync();
    }
    internal void RunAzikLoadedLayoutTestStep(int step)
    {
        if (step == 2 && Environment.GetCommandLineArgs().Contains("--keyboard-diagram-preview")) TestAllJapaneseKeyboardDiagrams();
        if (step is 1 or 8 or 16) SelectPage("japanese-table");
        if (step == 1 && _reservedExpanders.Count == 0)
            throw new InvalidOperationException("Loaded test requires an enabled AZIK profile");
        if (step is 3 or 12)
            foreach (var group in _reservedExpanders.Values) group.IsExpanded = false;
        if (step is 4 or 14)
            foreach (var group in _reservedExpanders.Values) group.IsExpanded = true;
        if (step == 5) AppWindow.Resize(new Windows.Graphics.SizeInt32(650, 800));
        if (step == 7) SelectPage("appearance");
        if (step == 10) AppWindow.Resize(new Windows.Graphics.SizeInt32(1300, 900));
    }

    private static void ArrangeReservedGrid(Grid grid, double width, bool original)
    {
        var columns = Math.Clamp((int)(width / (original ? 190 : 280)), 1, original ? 4 : 3);
        var rows = (grid.Children.Count + columns - 1) / columns;
        if (grid.ColumnDefinitions.Count == columns && grid.RowDefinitions.Count == rows) return;
        grid.ColumnDefinitions.Clear();
        grid.RowDefinitions.Clear();
        for (var column = 0; column < columns; column++)
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        for (var row = 0; row < rows; row++)
            grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        for (var index = 0; index < grid.Children.Count; index++)
        {
            Grid.SetRow((FrameworkElement)grid.Children[index], index / columns);
            Grid.SetColumn((FrameworkElement)grid.Children[index], index % columns);
        }
    }

    internal bool RunAzikReservedUiSelfTest(string path)
    {
        var store = new JapaneseInputTableStore(_store.RimeDirectory, _store.HotSettingsRegistryPath);
        var table = store.Import(path);
        SelectPage("japanese-table");
        if (_reservedCard?.Visibility != Visibility.Visible || _reservedChecks.Count != JapaneseInputTableStore.ReservedPhrases.Length)
            throw new InvalidOperationException("AZIK reserved panel not visible or incomplete");
        PageContent.Measure(new Windows.Foundation.Size(1000, double.PositiveInfinity));
        PageContent.Arrange(new Windows.Foundation.Rect(0, 0, 1000, PageContent.DesiredSize.Height));
        PageContent.UpdateLayout();
        foreach (var check in _reservedChecks.Values) { check.ApplyTemplate(); check.UpdateLayout(); }
        if (!IsInteractivePointerTarget(_reservedMaster))
            throw new InvalidOperationException("Blank-click handler would swallow master checkbox");
        foreach (var pair in _reservedChecks)
        {
            if (!IsInteractivePointerTarget(pair.Value)) throw new InvalidOperationException("Checkbox pointer guard: " + pair.Key);
            // Labels are non-hit-testable: pointer input goes to the native
            // checkbox chrome, which is covered by the ButtonBase guard.
            if (pair.Value.Content is not TextBlock label || label.IsHitTestVisible)
                throw new InvalidOperationException("Checkbox label must not intercept input: " + pair.Key);
            var cell = _reservedGrids.SelectMany(grid => grid.Children.OfType<Grid>())
                .Single(item => item.Children.Contains(pair.Value));
            if (cell.Children.Count != 1 ||
                cell.ContextFlyout is not MenuFlyout menu || menu.Items.Count != 2 ||
                ((MenuFlyoutItem)menu.Items[0]).Text != "编辑" || ((MenuFlyoutItem)menu.Items[1]).Text != "删除")
                throw new InvalidOperationException("Phrase must have a right-click menu without an ellipsis button: " + pair.Key);
        }
        foreach (var category in _reservedExpanders.Values)
        {
            if (category.GroupBorder.CornerRadius.TopLeft < 12 || category.GroupBorder.BorderThickness.Left != 1 ||
                category.GroupBorder.Background is null || category.GroupBorder.BorderBrush is null)
                throw new InvalidOperationException("Each category must have a rounded, bordered group container");
            var body = (StackPanel)category.GroupBody;
            var bulk = (StackPanel)body.Children[0];
            if (bulk.Children.Count != 2 || bulk.Children[0] is not Button selectAll ||
                (string)selectAll.Content != "全选" || bulk.Children[1] is not Button clearAll ||
                (string)clearAll.Content != "全不选" || !IsInteractivePointerTarget(selectAll))
                throw new InvalidOperationException("Each category needs working bulk-selection buttons");
            var actions = (StackPanel)body.Children[2];
            if (actions.Children[0] is not Button add || (string)add.Content != "＋ 添加保留字")
                throw new InvalidOperationException("Each category needs its own add-phrase button");
        }
        ArrangeReservedGrid(_reservedGrids[0], 1000, true);
        if (_reservedGrids[0].ColumnDefinitions.Count != 4 || _reservedGrids[0].RowDefinitions.Count != 7)
            throw new InvalidOperationException("Original phrase grid should be 4 columns / 7 rows");
        ArrangeReservedGrid(_reservedGrids[0], 380, true);
        if (_reservedGrids[0].ColumnDefinitions.Count != 2)
            throw new InvalidOperationException("Narrow phrase grid failed to adapt");
        var bulkGroup = "形容词变化";
        var otherBefore = store.ReservedSelection(table.Id).Items.ToDictionary(p => p.Key, p => p.Value);
        _reservedGroupSelections[bulkGroup](true);
        var bulkEntries = store.ReservedCatalog(table.Id).Where(p => p.Group == bulkGroup).ToList();
        if (bulkEntries.Any(p => !store.IsReservedSelected(store.ReservedSelection(table.Id), p) || _reservedChecks[p.Code].IsChecked != true))
            throw new InvalidOperationException("Group select-all did not persist and refresh every checkbox");
        if (otherBefore.Any(p => !bulkEntries.Any(entry => entry.Code == p.Key) && store.ReservedSelection(table.Id).Items[p.Key] != p.Value))
            throw new InvalidOperationException("Group select-all modified another category");
        _reservedGroupSelections[bulkGroup](false);
        if (bulkEntries.Any(p => store.IsReservedSelected(store.ReservedSelection(table.Id), p) || _reservedChecks[p.Code].IsChecked != false))
            throw new InvalidOperationException("Group clear-all failed");
        _reservedChecks["mst"].IsChecked = true;
        if (store.Read()!.Rules.Single(r => r.Input == "mst").Output != "ました")
            throw new InvalidOperationException("UI phrase checkbox did not save");
        _reservedChecks["ds"].IsChecked = false;
        if (store.Read()!.Rules.Single(r => r.Input == "ds").Output != "だい")
            throw new InvalidOperationException("UI uncheck did not restore reading");
        _reservedMaster!.IsChecked = false;
        if (store.ReservedSelection(table.Id).Enabled || _reservedChecks["mst"].IsEnabled)
            throw new InvalidOperationException("Master checkbox failed");
        _reservedMaster!.IsChecked = true;
        if (!store.ReservedSelection(table.Id).Enabled || _reservedChecks["ds"].IsChecked != false || _reservedChecks["mst"].IsChecked != true)
            throw new InvalidOperationException("Selections lost after master cycle");
        _tableEnabledSwitch!.IsOn = false;
        if (_reservedCard.Visibility != Visibility.Collapsed) throw new InvalidOperationException("Panel visible while table disabled");
        _tableEnabledSwitch.IsOn = true;
        if (_reservedCard.Visibility != Visibility.Visible) throw new InvalidOperationException("Panel failed to reappear");
        if (_reservedExpanders.Count != JapaneseInputTableStore.ReservedPhrases.Select(p => p.Group).Distinct().Count())
            throw new InvalidOperationException("Missing category expanders");
        _reservedExpanders["常用词尾"].IsExpanded = false;
        if (store.ReservedSelection(table.Id).Expanded?["常用词尾"] != false)
            throw new InvalidOperationException("Collapse did not persist");
        store.UpsertReservedPhrase(table.Id, "mxt", "ました", "自定义词尾");
        SelectPage("japanese-table");
        if (!_reservedChecks.ContainsKey("mxt") || !_reservedExpanders.ContainsKey("自定义词尾") || _reservedExpanders["常用词尾"].IsExpanded)
            throw new InvalidOperationException("Custom phrase/category or saved collapsed state missing after reopen");
        store.DeleteReservedPhrase(table.Id, "mxt");
        SelectPage("japanese-table");
        if (_reservedChecks.ContainsKey("mxt")) throw new InvalidOperationException("Deleted phrase still shown");
        return true;
    }

    internal bool RunJapaneseTableUiSelfTest(string fixtures)
    {
        var store = new JapaneseInputTableStore(_store.RimeDirectory, _store.HotSettingsRegistryPath);
        var azit = store.Import(Path.Combine(fixtures, "mozc_azit.txt"), false);
        var kyuuri = store.Import(Path.Combine(fixtures, "kyuuri2p-mozc.txt"), false);
        var rgtPath = Path.Combine(fixtures, "RGT_HPK_2018.txt");
        var rgt = File.Exists(rgtPath) ? store.Import(rgtPath, false) : null;
        SelectPage("japanese-table");
        var profiles = store.Profiles();
        if (_tablePreviewInput!.InputScope.Names.Single().NameValue != InputScopeNameValue.Password ||
            _tablePreviewInput.IsSpellCheckEnabled || _tablePreviewInput.IsTextPredictionEnabled)
            throw new InvalidOperationException("Preview editor must disable IME composition and prediction");
        _tableLettersSwitch!.IsOn = true;
        if (!store.ShowLetters) throw new InvalidOperationException("Letter display did not persist");
        _tableLettersSwitch.IsOn = false;
        if (store.ShowLetters) throw new InvalidOperationException("Kana display did not persist");
        _tableMixedSwitch!.IsOn = true;
        if (!store.Mixed) throw new InvalidOperationException("Mixed toggle failed to persist");
        _tableMixedSwitch.IsOn = false;
        if (store.Mixed) throw new InvalidOperationException("Mixed toggle failed to turn off");
        _tablePreviewInput!.Text = "hh";
        _tableProfilePicker!.SelectedIndex = profiles.FindIndex(profile => profile.Id == azit.Id) + 1;
        if (!store.Enabled || store.Read()?.Id != azit.Id || _tablePreviewResult!.Text != "ほう")
            throw new InvalidOperationException($"AZIT selection: enabled={store.Enabled}; id={store.Read()?.Id}; preview={_tablePreviewResult!.Text}; status={_saveStatus?.Text}");
        _tableProfilePicker.SelectedIndex = profiles.FindIndex(profile => profile.Id == kyuuri.Id) + 1;
        if (store.Read()?.Id != kyuuri.Id || _tablePreviewResult.Text != "ああ")
            throw new InvalidOperationException($"Kyuuri selection: preview={_tablePreviewResult.Text}; status={_saveStatus?.Text}");
        _tableEnabledSwitch!.IsOn = false;
        if (store.Enabled || _tableProfilePicker.SelectedIndex != 0)
            throw new InvalidOperationException($"Disable: enabled={store.Enabled}; selection={_tableProfilePicker.SelectedIndex}; status={_saveStatus?.Text}");
        _tableEnabledSwitch.IsOn = true;
        if (!store.Enabled || store.Read()?.Id != kyuuri.Id)
            throw new InvalidOperationException($"Re-enable: enabled={store.Enabled}; status={_saveStatus?.Text}");
        if (rgt is not null)
        {
            _tableProfilePicker.SelectedIndex = profiles.FindIndex(profile => profile.Id == rgt.Id) + 1;
            if (store.Read()?.Rgt != true) throw new InvalidOperationException("RGT mode lost during hot selection");
            foreach (var (code, expected) in new[] { ("k", "か"), ("ki", "き"), ("k i", "かい"), ("kk", "かか"), ("kw", "けい") })
            {
                _tablePreviewInput.Text = code;
                // This startup self-test has not entered the dispatcher loop;
                // selection synchronously refreshes the real preview callback.
                _tableProfilePicker.SelectedIndex = profiles.FindIndex(profile => profile.Id == azit.Id) + 1;
                _tableProfilePicker.SelectedIndex = profiles.FindIndex(profile => profile.Id == rgt.Id) + 1;
                if (_tablePreviewResult.Text != expected) throw new InvalidOperationException("RGT preview: " + code + " -> " + _tablePreviewResult.Text);
            }
        }
        _tableProfilePicker.SelectedIndex = 0;
        return !store.Enabled && store.Profiles().Count == (rgt is null ? 2 : 3);
    }

    internal void RunBundledJapaneseUiSelfTest(string bundled)
    {
        var store = new JapaneseInputTableStore(_store.RimeDirectory, _store.HotSettingsRegistryPath, bundled);
        if (store.Read() is not null || store.Enabled) throw new InvalidOperationException("Fresh fixture must not have a selected table");
        var profiles = store.Profiles();
        if (profiles.Count != 7) throw new InvalidOperationException("Fresh installation requires all 7 bundled profiles");
        if (store.Read() is not null || store.Enabled) throw new InvalidOperationException("Seeding changed active input mode");
        SelectPage("japanese-table");
        if (_tableProfilePicker!.Items.Count != 8 || _tableProfilePicker.SelectedIndex != 0)
            throw new InvalidOperationException("Fresh UI must show default romaji plus 7 choices");
        foreach (var profile in profiles)
        {
            _tableProfilePicker.SelectedIndex = profiles.FindIndex(p => p.Id == profile.Id) + 1;
            if (store.Read()?.Id != profile.Id || _japaneseKeyboardDiagramName != profile.Name || !_tableEnabledSwitch!.IsEnabled)
                throw new InvalidOperationException("Bundled UI switch failed: " + profile.Name);
        }
        var azik = profiles.Single(p => p.Name == "AZIK Test");
        var path = Path.Combine(_store.RimeDirectory, "japanese_input_tables", azik.Id + ".tsv");
        var changed = File.ReadAllText(path) + "\ncustomfixture\t自定义\t\n";
        File.WriteAllText(path, changed);
        _ = store.Profiles();
        if (File.ReadAllText(path) != changed) throw new InvalidOperationException("Bundled seeding overwrote a personal table");
        if (store.Profiles().Count != 7) throw new InvalidOperationException("Repeated seeding duplicated profiles");
        var migrated = azik with { Id = Guid.NewGuid().ToString("N") };
        var library = Path.GetDirectoryName(path)!;
        File.WriteAllText(Path.Combine(library, migrated.Id + ".tsv"), changed);
        File.WriteAllText(Path.Combine(library, migrated.Id + ".json"), System.Text.Json.JsonSerializer.Serialize(migrated));
        var merged = store.Profiles();
        if (merged.Count != 7 || merged.Single(p => p.Name == "AZIK Test").Id != migrated.Id)
            throw new InvalidOperationException("Migrated personal profile did not outrank the duplicate bundled profile");
        store.SelectProfile(migrated.Id);
        if (store.Read()?.Id != migrated.Id || !File.Exists(path))
            throw new InvalidOperationException("Migrated profile failed to switch or a profile was deleted");
    }

    private void ShowJapaneseInputTable()
    {
        AddHeading("日语按键表", "导入 Google 日本語入力 / Mozc 的 TXT 按键表，使用 AZIT、AKR、RGT 等日语输入方式。");
        var store = new JapaneseInputTableStore(_store.RimeDirectory, _store.HotSettingsRegistryPath);
        JapaneseInputTable? table = null;
        try { table = store.Read(); }
        catch (Exception error) { SetStatus("按键表读取失败：" + error.Message, true); }
        var info = new TextBlock { Text = table is null ? "尚未导入按键表" :
            $"{table.Name} · {table.Rules.Count} 条规则", TextWrapping = TextWrapping.Wrap };
        var profiles = store.Profiles();
        var profilePicker = new ComboBox { HorizontalAlignment = HorizontalAlignment.Stretch };
        _tableProfilePicker = profilePicker;
        AutomationProperties.SetAutomationId(profilePicker, "JapaneseInputTableProfile");
        void RefreshProfiles()
        {
            profiles = store.Profiles();
            profilePicker.Items.Clear();
            profilePicker.Items.Add("默认罗马字（中日混输）");
            foreach (var profile in profiles) profilePicker.Items.Add($"{profile.Name} · {profile.Count} 条规则");
            profilePicker.SelectedIndex = store.Enabled && table is not null
                ? profiles.FindIndex(profile => profile.Id == table.Id) + 1 : 0;
        }
        RefreshProfiles();
        var toggleRow = TrackedSwitchRow("使用自定义日语按键表",
            "开启后按此表输入日语；关闭后恢复原来的中日混输。已打开的输入窗口也会切换。",
            table is not null && store.Enabled, out var enabled);
        AutomationProperties.SetAutomationId(enabled, "JapaneseInputTableEnabled");
        _tableEnabledSwitch = enabled;
        enabled.IsEnabled = table is not null;
        var import = new Button { Content = "导入 TXT 并启用…" };
        AutomationProperties.SetAutomationId(import, "JapaneseInputTableImport");
        var card = new StackPanel { Spacing = 16 };
        card.Children.Add(ControlRow("输入方式", "已导入的配置会保留，选中后立即切换。", profilePicker));
        card.Children.Add(BuildJapaneseProfileManager(store));
        var fuzzyOptions = _store.ReadInputOptions();
        var fuzzyRow = TrackedSwitchRow("日语模糊匹配", "适用于 AZIK Test 等全部按键表：按当前表解码日语读音，再应用已选模糊规则。", fuzzyOptions.Fuzzy, out var tableFuzzy);
        AutomationProperties.SetAutomationId(tableFuzzy, "JapaneseTableFuzzyMaster");
        tableFuzzy.Toggled += (_, _) =>
        {
            try { if (!_uiTestMode) _store.SaveInputOptions(_store.ReadInputOptions() with { Fuzzy = tableFuzzy.IsOn }); }
            catch (Exception error) { SetStatus(error.Message, true); }
        };
        card.Children.Add(fuzzyRow);
        var fuzzyDetails = new Button { Content = "设置日语模糊规则…" };
        fuzzyDetails.Click += (_, _) => SelectPage("japanese");
        card.Children.Add(fuzzyDetails);
        card.Children.Add(info);
        card.Children.Add(toggleRow);
        var mixedRow = TrackedSwitchRow("与中文混输",
            "开启后同时显示中文拼音与当前日语按键表候选；关闭后只输入日语。切换立即生效。",
            store.Mixed, out var mixed);
        AutomationProperties.SetAutomationId(mixed, "JapaneseInputTableMixed");
        _tableMixedSwitch = mixed;
        card.Children.Add(mixedRow);
        mixed.Toggled += (_, _) =>
        {
            try
            {
                if (!_uiTestMode) store.SetMixed(mixed.IsOn);
                SetStatus(_uiTestMode ? "测试模式：未保存" : mixed.IsOn ?
                    "已开启中文与日语按键表混输。" : "已切回日语按键表专用模式。", false);
            }
            catch (Exception error) { SetStatus("混输切换失败：" + error.Message, true); }
        };
        Action refreshOverlapPreview = () => { };
        var overlapRow = TrackedSwitchRow("元音起始韵尾简拼（实验）",
            "默认关闭。按第二键定义合并起始元音：s=aku 时 as→あく；t=itsu 时 it→いつ。已有组合、手动规则与保留词优先。即时保存、热切换。",
            store.VowelTailOverlap, out var overlap);
        AutomationProperties.SetAutomationId(overlap, "JapaneseVowelTailOverlap");
        _tableOverlapSwitch = overlap;
        card.Children.Add(overlapRow);
        overlap.Toggled += (_, _) =>
        {
            try
            {
                if (!_uiTestMode) store.SetVowelTailOverlap(overlap.IsOn);
                refreshOverlapPreview();
                SetStatus(_uiTestMode ? "测试模式：未保存" : overlap.IsOn ?
                    "元音起始韵尾简拼已开启；已有组合保持优先。" : "元音起始韵尾简拼已关闭；恢复原按键组合。", false);
            }
            catch (Exception error) { SetStatus("元音简拼切换失败：" + error.Message, true); }
        };
        card.Children.Add(import);
        var wholeRow=TrackedSwitchRow("保留字整串交给 Mozc（实验）",
            "开启：展开保留字后整串转换，保留指定片段含义；不再逐段查词后拼接。关闭：恢复原分段组合方式。普通读音和中文保留，即时保存、热切换。默认关闭。",
            store.ReservedWhole,out var reservedWhole);
        _tableReservedWholeSwitch=reservedWhole;
        AutomationProperties.SetAutomationId(reservedWhole,"JapaneseReservedWhole");
        card.Children.Add(wholeRow);
        reservedWhole.Toggled+=(_,_)=> {
            try {if(!_uiTestMode)store.SetReservedWhole(reservedWhole.IsOn);SetStatus(reservedWhole.IsOn?"保留字整串转换已开启；普通读音仍保留。":"已恢复原保留字分段组合方式。",false);}
            catch(Exception error){SetStatus("保留字转换切换失败："+error.Message,true);}
        };
        var singleRow=TrackedSwitchRow("日语单键按韵尾查词",
            "开启：单按 q 等键按当前第二键韵尾查词。关闭：显示以该键开头的组合提示。混输时两种模式都保留中文；空声母键仍等待下一键。即时保存、热切换。",
            store.SingleKeyTail,out var singleTail);
        _tableSingleTailSwitch=singleTail;
        AutomationProperties.SetAutomationId(singleTail,"JapaneseSingleKeyTail");
        card.Children.Add(singleRow);
        singleTail.Toggled+=(_,_)=> {
            try {if(!_uiTestMode)store.SetSingleKeyTail(singleTail.IsOn);SetStatus("日语单键匹配模式已保存；中文候选保持可用。",false);}
            catch(Exception error){SetStatus("单键匹配切换失败："+error.Message,true);}
        };
        var lettersRow = TrackedSwitchRow("上方显示原始字母",
            "开启显示输入码（如 qw）；关闭显示转换后的假名（如 きゅう）。只改变显示，不改变候选。",
            store.ShowLetters, out var letters);
        _tableLettersSwitch = letters;
        AutomationProperties.SetAutomationId(letters, "JapaneseInputTableShowLetters");
        card.Children.Add(lettersRow);
        letters.Toggled += (_, _) =>
        {
            try
            {
                if (!_uiTestMode) store.SetShowLetters(letters.IsOn);
                SetStatus(_uiTestMode ? "测试模式：未保存" : letters.IsOn ?
                    "上方已切换为原始字母，候选保持不变。" : "上方已恢复假名显示。", false);
            }
            catch (Exception error) { SetStatus("显示切换失败：" + error.Message, true); }
        };
        PageContent.Children.Add(Card("当前按键表", "导入和切换即时生效，不需要重新部署。", card));
        PageContent.Children.Add(Card("日语学习", "假名格式、词间空格与逐词换行；仅影响学习模式上屏，不改变词库学习。", BuildJapaneseLearningModeSwitch()));
        _saveStatus = new TextBlock { Foreground = Brush(143, 205, 177), TextWrapping = TextWrapping.Wrap };
        PageContent.Children.Add(_saveStatus);

        var diagramHost = new StackPanel();
        _japaneseKeyboardDiagram = diagramHost;
        JapaneseInputTable? diagramTable = null;
        var diagramEnabled = false;
        void RefreshDiagram()
        {
            var active = enabled.IsOn ? table : null;
            if (diagramHost.Children.Count > 0 && ReferenceEquals(diagramTable, active) && diagramEnabled == enabled.IsOn) return;
            diagramTable = active;
            diagramEnabled = enabled.IsOn;
            diagramHost.Children.Clear();
            diagramHost.Children.Add(BuildJapaneseKeyboardDiagram(active));
        }
        RefreshDiagram();
        PageContent.Children.Add(Card("键盘记忆图", "每套方案都有对应的速查图，自定义表也会按实际规则更新。", diagramHost));

        var rawInputScope = new InputScope();
        // Password scope disables native IME composition for this editor.
        // This is still a visible TextBox, not a masked PasswordBox; it does
        // not switch the active input language of any window.
        rawInputScope.Names.Add(new InputScopeName { NameValue = InputScopeNameValue.Password });
        var sample = new TextBox { PlaceholderText = "直接输入原始按键，例如 qwlh、xach、ds.",
            InputScope = rawInputScope, IsSpellCheckEnabled = false, IsTextPredictionEnabled = false,
            HorizontalAlignment = HorizontalAlignment.Stretch };
        WatchExplicitEdit(sample);
        AutomationProperties.SetAutomationId(sample, "JapaneseInputTablePreviewInput");
        var preview = new TextBlock { Text = "这里检查按键到假名的映射；汉字转换请在输入法测试台试打。",
            TextWrapping = TextWrapping.Wrap, FontSize = 18 };
        _tablePreviewInput = sample;
        _tablePreviewResult = preview;
        void UpdatePreview()
        {
            RefreshDiagram();
            if (table is null || sample.Text.Length == 0)
            { preview.Text = "这里检查按键到假名的映射；汉字转换请在输入法测试台试打。"; return; }
            try
            {
                var result = JapaneseInputTableStore.Convert(sample.Text, table.Rules, table.Rgt);
                preview.Text = result.Text + (result.Pending.Length > 0 ? $"  （待输入：{result.Pending}）" : "");
            }
            catch (Exception error) { preview.Text = error.Message; }
        }
        refreshOverlapPreview = () => { table = store.Read(); info.Text = table is null ? "尚未导入按键表" : $"{table.Name} · {table.Rules.Count} 条规则"; UpdatePreview(); };
        sample.TextChanged += (_, _) =>
        {
            UpdatePreview();
            if (Environment.GetCommandLineArgs().Contains("--raw-preview-keyboard-test", StringComparer.Ordinal))
                File.WriteAllText(Path.Combine(Path.GetTempPath(), "ZhongriSettingsV2-raw-preview-keyboard-test.txt"), sample.Text);
        };
        var previewBody = new StackPanel { Spacing = 12 };
        previewBody.Children.Add(sample);
        previewBody.Children.Add(preview);
        PageContent.Children.Add(Card("按键预览", "直接接收原始字母和符号，不经过输入法组词；只解析按键表，不调用在线服务。", previewBody));
        var reservedBody = new StackPanel { Spacing = 12 };
        var reservedCard = Card("AZIK 保留字与常用短语",
            "勾选后短语优先，冲突读音保留为备选；每套 AZIK 独立保存，立即生效。新增项默认不勾选。", reservedBody);
        AutomationProperties.SetAutomationId(reservedCard, "AzikReservedPhrasePanel");
        _reservedCard = reservedCard;
        PageContent.Children.Add(reservedCard);
        var codeSearchText = "";
        var codeSearchRevision = 0;
        var codeSearchEnabled = 0;
        var codeSearchGroup = "全部分组";
        var codeSearchVirtual = true;
        var codeSearchFuzzy = true;
        void RefreshReserved()
        {
            ++codeSearchRevision;
            reservedBody.Children.Clear();
            _reservedChecks.Clear();
            _reservedGroupSelections.Clear();
            _reservedGrids.Clear();
            _reservedExpanders.Clear();
            reservedCard.Visibility = store.Enabled && table is not null && store.SupportsReserved(table.Id)
                ? Visibility.Visible : Visibility.Collapsed;
            if (reservedCard.Visibility != Visibility.Visible || table is null) return;
            var profileId = table.Id;
            var selection = store.ReservedSelection(profileId);
            var master = new CheckBox { Content = "启用本方案的保留字与常用短语", IsChecked = selection.Enabled };
            _reservedMaster = master;
            AutomationProperties.SetAutomationId(master, "AzikReservedEnabled");
            reservedBody.Children.Add(master);
            var search = new TextBox { Header = "短码查询", PlaceholderText = "输入短码或假名，例如 sms、します", Text = codeSearchText };
            var searchScope = new InputScope();searchScope.Names.Add(new InputScopeName { NameValue = InputScopeNameValue.Password });
            search.InputScope = searchScope;search.IsSpellCheckEnabled = false;search.IsTextPredictionEnabled = false;
            AutomationProperties.SetAutomationId(search,"ReservedCodeSearchInput");
            var virtualSearch = new CheckBox { Content = "包含虚拟解析", IsChecked = codeSearchVirtual };
            var fuzzySearch = new CheckBox { Content = "包含模糊查找", IsChecked = codeSearchFuzzy };
            var enabledFilter = new ComboBox { Header = "启用状态", ItemsSource = new[] { "全部状态", "已开启", "未开启" }, SelectedIndex = codeSearchEnabled, MinWidth = 140 };
            var groupFilter = new ComboBox { Header = "分组", ItemsSource = new[] { "全部分组" }.Concat(store.ReservedGroups(profileId)).ToList(), MinWidth = 200 };
            groupFilter.SelectedItem = codeSearchGroup;
            if (groupFilter.SelectedIndex < 0) groupFilter.SelectedIndex = 0;
            AutomationProperties.SetAutomationId(enabledFilter,"ReservedCodeSearchEnabledFilter");
            AutomationProperties.SetAutomationId(groupFilter,"ReservedCodeSearchGroupFilter");
            _reservedSearchEnabledFilter=enabledFilter;_reservedSearchGroupFilter=groupFilter;
            var filters = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 12 };
            filters.Children.Add(enabledFilter);filters.Children.Add(groupFilter);
            var runSearch = new Button { Content = "搜索" };
            AutomationProperties.SetAutomationId(runSearch,"ReservedCodeSearchButton");
            var searchOptions = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 12 };
            searchOptions.Children.Add(runSearch);searchOptions.Children.Add(virtualSearch);searchOptions.Children.Add(fuzzySearch);
            var searchStatus = new TextBlock { TextWrapping = TextWrapping.Wrap };
            var searchResults = new ListView { MaxHeight = 300, SelectionMode = ListViewSelectionMode.None };
            AutomationProperties.SetAutomationId(searchResults,"ReservedCodeSearchResults");
            async Task SearchCodeAsync()
            {
                var query=search.Text;var includeVirtual=virtualSearch.IsChecked==true;var includeFuzzy=fuzzySearch.IsChecked==true;
                var enabled=enabledFilter.SelectedIndex;var group=(string)groupFilter.SelectedItem;
                var revision=++codeSearchRevision;runSearch.IsEnabled=false;searchStatus.Text="查询中…";searchResults.ItemsSource=null;
                searchResults.Items.Clear();
                try {
                    var matches=await Task.Run(()=>store.SearchCode(profileId,query,includeVirtual,includeFuzzy));
                    if(revision!=codeSearchRevision)return;
                    if(search.Text!=query || (virtualSearch.IsChecked==true)!=includeVirtual || (fuzzySearch.IsChecked==true)!=includeFuzzy){searchStatus.Text="输入或筛选已更改，请重新搜索。";return;}
                    matches=matches.Where(m=>(enabled==0 || m.ReservedKey is not null && m.Enabled==(enabled==1)) && (group=="全部分组" || m.ReservedKey is not null && m.Group==group)).ToList();
                    foreach(var match in matches) {
                        if(match.ReservedKey is null) { searchResults.Items.Add(new TextBlock { Text=match.Display, TextWrapping=TextWrapping.Wrap, Tag=match });continue; }
                        var check=new CheckBox { Content=match.Display, IsChecked=match.Enabled, Tag=match, HorizontalAlignment=HorizontalAlignment.Stretch };
                        async void ToggleMatch(object sender, RoutedEventArgs args) {
                            Save(code:match.ReservedKey,chosen:check.IsChecked==true);
                            RefreshReserved();
                            if(_reservedSearchRun is not null)await _reservedSearchRun();
                        }
                        check.Checked+=ToggleMatch;check.Unchecked+=ToggleMatch;
                        searchResults.Items.Add(check);
                    }
                    searchStatus.Text=$"{matches.Count} 项。保留字可直接勾选；状态是单项勾选状态，总开关关闭时暂不参与输入。虚拟/表内规则仅供查看，不可切换。";
                } catch(Exception error) { if(revision==codeSearchRevision) {searchResults.ItemsSource=null;searchStatus.Text="查询失败："+error.Message;} }
                finally {if(revision==codeSearchRevision)runSearch.IsEnabled=true;}
            }
            search.TextChanged+=(_,_)=>{codeSearchText=search.Text;};
            runSearch.Click+=async(_,_)=>await SearchCodeAsync();
            search.KeyDown+=async(_,args)=>{if(args.Key==Windows.System.VirtualKey.Enter){args.Handled=true;await SearchCodeAsync();}};
            _reservedSearchInput=search;_reservedSearchResults=searchResults;_reservedSearchStatus=searchStatus;_reservedSearchRun=SearchCodeAsync;
            enabledFilter.SelectionChanged+=async(_,_)=>{codeSearchEnabled=enabledFilter.SelectedIndex;await SearchCodeAsync();};
            groupFilter.SelectionChanged+=async(_,_)=>{codeSearchGroup=(string)groupFilter.SelectedItem;await SearchCodeAsync();};
            virtualSearch.Checked+=async(_,_)=>{codeSearchVirtual=true;await SearchCodeAsync();};
            virtualSearch.Unchecked+=async(_,_)=>{codeSearchVirtual=false;await SearchCodeAsync();};
            fuzzySearch.Checked+=async(_,_)=>{codeSearchFuzzy=true;await SearchCodeAsync();};
            fuzzySearch.Unchecked+=async(_,_)=>{codeSearchFuzzy=false;await SearchCodeAsync();};
            reservedBody.Children.Add(search);reservedBody.Children.Add(searchOptions);reservedBody.Children.Add(filters);reservedBody.Children.Add(searchStatus);reservedBody.Children.Add(searchResults);
            void Save(bool? all = null, string? code = null, bool chosen = false)
            {
                try
                {
                    if (_uiTestMode) { SetStatus("测试模式：未保存", false); return; }
                    store.SetReservedSelection(profileId, all, code, chosen);
                    table = store.Read();
                    UpdatePreview();
                    info.Text = $"{table!.Name} · {table.Rules.Count} 条规则";
                    SetStatus("AZIK 常用短语已更新，当前输入窗口即时生效。", false);
                }
                catch (Exception error) { SetStatus("常用短语保存失败：" + error.Message, true); RefreshReserved(); }
            }
            master.Checked += (_, _) => { Save(true); RefreshReserved(); };
            master.Unchecked += (_, _) => { Save(false); RefreshReserved(); };
            void RefreshAfterEdit()
            {
                table = store.Read(); UpdatePreview(); RefreshReserved();
                SetStatus("保留字已保存，当前输入窗口即时生效。", false);
            }
            async Task EditPhrase(string group, JapaneseReservedPhrase? phrase = null)
            {
                var code = new TextBox { Header = "短码", Text = phrase?.Code ?? "", PlaceholderText = "例如 mxt" };
                var rawScope = new InputScope();
                rawScope.Names.Add(new InputScopeName { NameValue = InputScopeNameValue.Password });
                code.InputScope = rawScope; code.IsSpellCheckEnabled = false; code.IsTextPredictionEnabled = false;
                var text = new TextBox { Header = "日语假名读音", Text = phrase?.Text ?? "", PlaceholderText = "例如 ました" };
                var category = new TextBox { Header = "分类（已有分类或新分类）", Text = group };
                WatchExplicitEdit(code); WatchExplicitEdit(text); WatchExplicitEdit(category);
                var error = new TextBlock { TextWrapping = TextWrapping.Wrap, Foreground = Brush(235, 120, 120) };
                var body = new StackPanel { Spacing = 12 };
                body.Children.Add(code); body.Children.Add(text); body.Children.Add(category);
                body.Children.Add(new TextBlock { Text = "同一短码可添加多个不同读音，各项独立启用。完整短码列出所有已启用保留字，同时保留普通读音候选；组合时仍优先完整词典匹配。", TextWrapping = TextWrapping.Wrap });
                body.Children.Add(error);
                var dialog = new ContentDialog { XamlRoot = Root.XamlRoot, Title = phrase is null ? "添加保留字" : "编辑保留字",
                    Content = body, PrimaryButtonText = phrase is null ? "添加" : "", CloseButtonText = phrase is null ? "取消" : "关闭" };
                var edited=false;
                var previousCode=phrase?.Code;
                var previousText=phrase?.Text;
                var previousKey=phrase?.Key;
                if (phrase is not null) AutoSaveDialog(dialog,body,()=> {
                    if (!_uiTestMode) store.UpsertReservedPhrase(profileId,code.Text,text.Text,category.Text,previousCode,previousText,previousKey);
                    previousCode=code.Text; previousText=text.Text; edited=true;
                },error);
                dialog.PrimaryButtonClick += (_, args) =>
                {
                    try { if (!_uiTestMode) store.UpsertReservedPhrase(profileId, code.Text, text.Text, category.Text, phrase?.Code,phrase?.Text,phrase?.Key); }
                    catch (Exception failure) { error.Text = failure.Message; args.Cancel = true; }
                };
                if (await dialog.ShowAsync() == ContentDialogResult.Primary || edited) RefreshAfterEdit();
            }
            async Task EditGroup(string? oldName = null)
            {
                var name = new TextBox { Header = "分类名称", Text = oldName ?? "" };
                WatchExplicitEdit(name);
                var error = new TextBlock { TextWrapping = TextWrapping.Wrap };
                var body = new StackPanel { Spacing = 12 }; body.Children.Add(name); body.Children.Add(error);
                var dialog = new ContentDialog { XamlRoot = Root.XamlRoot, Title = oldName is null ? "添加分类" : "重命名分类",
                    Content = body, PrimaryButtonText = oldName is null ? "添加" : "", CloseButtonText = oldName is null ? "取消" : "关闭" };
                var edited=false;
                var previousName=oldName;
                if(oldName is not null) AutoSaveDialog(dialog,body,()=> {
                    if(!_uiTestMode) store.ChangeReservedGroup(profileId,name.Text,previousName);
                    previousName=name.Text; edited=true;
                },error);
                dialog.PrimaryButtonClick += (_, args) =>
                { try { if (!_uiTestMode) store.ChangeReservedGroup(profileId, name.Text, oldName); }
                  catch (Exception failure) { error.Text = failure.Message; args.Cancel = true; } };
                if (await dialog.ShowAsync() == ContentDialogResult.Primary || edited) RefreshAfterEdit();
            }
            async Task ConfirmDelete(string title, string description, Action change)
            {
                var dialog = new ContentDialog { XamlRoot = Root.XamlRoot, Title = title, Content = description,
                    PrimaryButtonText = "删除", CloseButtonText = "取消", DefaultButton = ContentDialogButton.Close };
                if (await dialog.ShowAsync() != ContentDialogResult.Primary) return;
                try { if (!_uiTestMode) change(); RefreshAfterEdit(); }
                catch (Exception failure) { SetStatus(failure.Message, true); }
            }
            var actions = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
            var addGroup = new Button { Content = "＋ 添加分类" };
            AutomationProperties.SetAutomationId(addGroup, "AzikReservedAddGroup");
            addGroup.Click += async (_, _) => await EditGroup();
            actions.Children.Add(addGroup); reservedBody.Children.Add(actions);
            reservedBody.Children.Add(new TextBlock { Text = "在分类内添加保留字；右键点击保留字可编辑或删除。",
                TextWrapping = TextWrapping.Wrap });
            reservedBody.Children.Add(new TextBlock { Text = "这些短码只输入对应的假名，不会自动改变前面的动词词干。比如「書く」变「書いて」需要正确的词干；不要直接把「て」接在「書く」后面。新增内置项默认关闭，可按需勾选。",
                TextWrapping = TextWrapping.Wrap, Foreground = Brush(157, 159, 171) });
            var catalog = store.ReservedCatalog(profileId);
            foreach (var groupName in store.ReservedGroups(profileId))
            {
                var entries = catalog.Where(phrase => phrase.Group == groupName).ToList();
                var original = groupName == "AZIK 原有保留字";
                var grid = new Grid { ColumnSpacing = 12, RowSpacing = 4, HorizontalAlignment = HorizontalAlignment.Stretch };
                _reservedGrids.Add(grid);
                foreach (var phrase in entries)
                {
                    var check = new CheckBox { Content = new TextBlock { Text = $"{phrase.Code}  →  {phrase.Text}",
                            TextWrapping = TextWrapping.Wrap, FontSize = 14, IsHitTestVisible = false },
                        HorizontalAlignment = HorizontalAlignment.Stretch, MinHeight = 32,
                        IsChecked = store.IsReservedSelected(selection, phrase), IsEnabled = selection.Enabled };
                    AutomationProperties.SetAutomationId(check, "AzikReserved_" + phrase.Code);
                    if (!_reservedChecks.TryAdd(phrase.Code, check)) {
                        _reservedChecks[phrase.Key] = check;
                        AutomationProperties.SetAutomationId(check,"AzikReserved_"+phrase.Code+"_"+Uri.EscapeDataString(phrase.Key));
                    }
                    check.Checked += (_, _) => Save(code: phrase.Key, chosen: true);
                    check.Unchecked += (_, _) => Save(code: phrase.Key, chosen: false);
                    var cell = new Grid { Background = new Microsoft.UI.Xaml.Media.SolidColorBrush(Microsoft.UI.Colors.Transparent) };
                    cell.Children.Add(check);
                    var flyout = new MenuFlyout();
                    var edit = new MenuFlyoutItem { Text = "编辑" };
                    var delete = new MenuFlyoutItem { Text = "删除" };
                    edit.Click += async (_, _) => await EditPhrase(groupName, phrase);
                    delete.Click += async (_, _) => await ConfirmDelete("删除保留字", $"删除 {phrase.Code} → {phrase.Text}？原始按键表不会被修改。",
                        () => store.DeleteReservedPhrase(profileId, phrase.Code,phrase.Text,phrase.Key));
                    flyout.Items.Add(edit); flyout.Items.Add(delete);
                    cell.ContextFlyout = flyout;
                    grid.Children.Add(cell);
                }
                ArrangeReservedGrid(grid, 900, original);
                grid.SizeChanged += (_, args) => ArrangeReservedGrid(grid, args.NewSize.Width, original);
                var groupBody = new StackPanel { Spacing = 10 };
                void SelectGroup(bool chosen)
                {
                    try
                    {
                        if (_uiTestMode) { SetStatus("测试模式：未保存", false); return; }
                        store.SetReservedGroupSelection(profileId, groupName, chosen);
                        RefreshAfterEdit();
                        SetStatus($"{groupName}：已" + (chosen ? "全选。" : "全不选。"), false);
                    }
                    catch (Exception error) { SetStatus("分类选择失败：" + error.Message, true); RefreshReserved(); }
                }
                _reservedGroupSelections[groupName] = SelectGroup;
                var bulkActions = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
                var selectAll = new Button { Content = "全选", IsEnabled = selection.Enabled && entries.Count > 0 };
                var clearAll = new Button { Content = "全不选", IsEnabled = selection.Enabled && entries.Count > 0 };
                AutomationProperties.SetAutomationId(selectAll, "AzikReservedSelectAll_" + groupName);
                AutomationProperties.SetAutomationId(clearAll, "AzikReservedClearAll_" + groupName);
                selectAll.Click += (_, _) => SelectGroup(true);
                clearAll.Click += (_, _) => SelectGroup(false);
                bulkActions.Children.Add(selectAll); bulkActions.Children.Add(clearAll);
                groupBody.Children.Add(bulkActions); groupBody.Children.Add(grid);
                var groupActions = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
                var add = new Button { Content = "＋ 添加保留字" };
                AutomationProperties.SetAutomationId(add, "AzikReservedAddPhrase_" + groupName);
                var rename = new Button { Content = "重命名分类" };
                var remove = new Button { Content = "删除分类" };
                add.Click += async (_, _) => await EditPhrase(groupName);
                rename.Click += async (_, _) => await EditGroup(groupName);
                remove.Click += async (_, _) => await ConfirmDelete("删除分类", "分类中的保留字将移到“未分类”，不会一起删除。",
                    () => store.ChangeReservedGroup(profileId, groupName, groupName, true));
                groupActions.Children.Add(add); groupActions.Children.Add(rename); groupActions.Children.Add(remove); groupBody.Children.Add(groupActions);
                var expander = new ReservedCategory($"{groupName} · {entries.Count} 项", groupBody,
                    selection.Expanded is not null && selection.Expanded.TryGetValue(groupName, out var open) ? open :
                    groupName is "AZIK 原有保留字" or "常用词尾" or "常用语法" or "日常表达");
                AutomationProperties.SetAutomationId(expander, "AzikReservedGroup_" + groupName);
                expander.ExpandedChanged += open => { if (!_uiTestMode) store.SetReservedGroupExpanded(profileId, groupName, open); };
                _reservedExpanders[groupName] = expander;
                reservedBody.Children.Add(expander);
            }
            reservedBody.Children.Add(new TextBlock { Text = "取消原有保留字：有冲突读音时恢复读音（如 Test 的 ds→だい）；没有冲突读音时停止该短码映射。完整输入仍可正常组词。",
                TextWrapping = TextWrapping.Wrap });
        }
        RefreshReserved();
        PageContent.Children.Add(Card("RGT（HPK 2018）用法", "按作者工具包说明适配，不需要另外运行键盘工具。",
            new TextBlock { Text = "k → か；连续 ki → き；停顿 300 毫秒后再打 i → かい。\n" +
                "q → ん，x → っ，l → ー；kk → かか，不是促音。\n" +
                "kq → かい，kw → けい，kp → こう；dst → でした，pfy → こすと。\n" +
                "空格可固定末尾单键；再次空格或 Enter 选词上屏。预览按连续输入解析，可用空格模拟停顿。\n" +
                "这是后期键位：f=す、j=て、z=き、b=の、y=と，与 2017 年文章不同。",
                TextWrapping = TextWrapping.Wrap }));
        PageContent.Children.Add(Card("文件格式", "每行用 Tab 分隔，支持 UTF-8、UTF-16 和 Shift-JIS。",
            new TextBlock { Text = "按键 → 输出 → 保留输入（第三列可留空）\n" +
                "qw → きゅう\nkk → っ → k\n\n第三列会继续参与下一次组合，支持促音、缩写和符号键。" +
                "请导入罗马字表，例如 mozc_azit.txt；快捷键表和用户词典不能作为按键表。",
                TextWrapping = TextWrapping.Wrap }));

        var saving = false;
        profilePicker.SelectionChanged += (_, _) =>
        {
            if (saving || profilePicker.SelectedIndex < 0) return;
            try
            {
                saving = true;
                if (!_uiTestMode)
                {
                    if (profilePicker.SelectedIndex == 0) store.SetEnabled(false);
                    else table = store.SelectProfile(profiles[profilePicker.SelectedIndex - 1].Id);
                }
                enabled.IsOn = profilePicker.SelectedIndex > 0;
                enabled.IsEnabled = table is not null;
                info.Text = table is null ? "尚未导入按键表" : $"{table.Name} · {table.Rules.Count} 条规则";
                UpdatePreview();
                RefreshReserved();
                SetStatus(_uiTestMode ? "测试模式：未保存" : enabled.IsOn ?
                    $"已切换到 {table?.Name}；当前输入窗口即时生效。" : "已恢复原来的中日混输。", false);
            }
            catch (Exception error) { SetStatus("切换失败：" + error.Message, true); }
            finally { saving = false; }
        };
        enabled.Toggled += (_, _) =>
        {
            if (saving) return;
            try
            {
                if (!_uiTestMode) store.SetEnabled(enabled.IsOn);
                saving = true;
                if (!_uiTestMode) RefreshProfiles();
                saving = false;
                RefreshReserved();
                RefreshDiagram();
                SetStatus(_uiTestMode ? "测试模式：未保存" : enabled.IsOn ?
                    "自定义日语按键表已启用；当前输入窗口即时生效。" : "已恢复原来的中日混输。", false);
            }
            catch (Exception error) { SetStatus(error.Message, true); }
            finally { saving = false; }
        };
        import.Click += async (_, _) =>
        {
            import.IsEnabled = false;
            try
            {
                var picker = new FileOpenPicker();
                WinRT.Interop.InitializeWithWindow.Initialize(picker, _windowHandle);
                picker.FileTypeFilter.Add(".txt");
                picker.FileTypeFilter.Add(".tsv");
                var file = await picker.PickSingleFileAsync();
                if (file is null) return;
                if (_uiTestMode) { SetStatus("测试模式：已选择文件，未保存", false); return; }
                table = await Task.Run(() => store.Import(file.Path));
                info.Text = $"{table.Name} · {table.Rules.Count} 条规则";
                saving = true;
                RefreshProfiles();
                enabled.IsEnabled = true;
                enabled.IsOn = true;
                saving = false;
                UpdatePreview();
                RefreshReserved();
                SetStatus($"已导入并启用 {table.Name}，无需部署或重新打开输入窗口。", false);
            }
            catch (Exception error) { SetStatus("导入失败：" + error.Message, true); }
            finally { saving = false; import.IsEnabled = true; }
        };
    }
}
