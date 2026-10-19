using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Automation;

namespace ZhongriSettingsV2;

public sealed partial class MainWindow
{
    private async Task OpenJapaneseKeyEditorAsync(string key, JapaneseInputTable table)
    {
        try { await EditJapaneseKeyAsync(key, table); }
        catch (Exception error) { SetStatus("打开按键编辑失败：" + error.Message, true); }
    }
    private async Task EditJapaneseKeyAsync(string key, JapaneseInputTable table, bool verifyLoadedDialog = false, string? layerScreenshotPrefix = null)
    {
        var store = new JapaneseInputTableStore(_store.RimeDirectory, _store.HotSettingsRegistryPath);
        var shiftedKey = ShiftedJapaneseKey(key);
        var hasShiftedOverride=shiftedKey is not null && store.KeyProfile(table.Id).Keys?.ContainsKey(shiftedKey)==true;
        var (normalBody, read) = BuildJapaneseKeyEditor(store, table, key);
        var body = new StackPanel { Spacing = 12 };
        body.Children.Add(normalBody);
        Func<JapaneseKeyDefinition>? readShifted = null;
        Action? verifyShiftLayer = null;
        Action<bool>? selectTestLayer=null;
        if (shiftedKey is not null)
        {
            var (shiftedBody, shiftedRead) = BuildJapaneseKeyEditor(store, table, shiftedKey);
            readShifted = shiftedRead;
            shiftedBody.Visibility = Visibility.Collapsed;
            var tabs = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
            var normalTab = new Button { Content = "普通 · " + key.ToUpperInvariant() };
            var shiftedTab = new Button { Content = "大写 / Shift · " + shiftedKey };
            var selectedBackground=Brush(26,190,204);
            var inactiveBackground=Brush(48,48,48);
            void PaintTab(Button tab,bool active)
            {
                var background=active?selectedBackground:inactiveBackground;
                var foreground=active?Brush(5,30,33):Brush(224,224,224);
                tab.Background=background;tab.Foreground=foreground;
                tab.BorderBrush=active?Brush(112,238,246):Brush(85,85,85);
                tab.BorderThickness=new Thickness(2);
                foreach(var state in new[]{"","PointerOver","Pressed"}) {
                    tab.Resources["ButtonBackground"+state]=background;
                    tab.Resources["ButtonForeground"+state]=foreground;
                    tab.Resources["ButtonBorderBrush"+state]=tab.BorderBrush;
                }
            }
            void SelectLayer(bool shifted)
            {
                normalBody.Visibility=shifted?Visibility.Collapsed:Visibility.Visible;
                shiftedBody.Visibility=shifted?Visibility.Visible:Visibility.Collapsed;
                PaintTab(normalTab,!shifted);
                PaintTab(shiftedTab,shifted);
                normalTab.Content=(shifted?"":"✓ ")+"普通 · "+key.ToUpperInvariant();
                shiftedTab.Content=(shifted?"✓ ":"")+"大写 / Shift · "+shiftedKey;
                normalTab.FontWeight=shifted?Microsoft.UI.Text.FontWeights.Normal:Microsoft.UI.Text.FontWeights.SemiBold;
                shiftedTab.FontWeight=shifted?Microsoft.UI.Text.FontWeights.SemiBold:Microsoft.UI.Text.FontWeights.Normal;
                AutomationProperties.SetHelpText(normalTab,shifted?"切换到普通键层":"当前正在编辑普通键层");
                AutomationProperties.SetHelpText(shiftedTab,shifted?"当前正在编辑大写 / Shift 键层":"切换到大写 / Shift 键层");
            }
            selectTestLayer=SelectLayer;
            SelectLayer(false);
            AutomationProperties.SetAutomationId(normalTab, "KeyNormalLayer");
            AutomationProperties.SetAutomationId(shiftedTab, "KeyShiftLayer");
            normalTab.Click += (_, _) => SelectLayer(false);
            shiftedTab.Click += (_, _) => SelectLayer(true);
            tabs.Children.Add(normalTab); tabs.Children.Add(shiftedTab);
            body.Children.Insert(0, tabs);
            body.Children.Add(shiftedBody);
            verifyShiftLayer = () =>
            {
                if(!normalTab.IsEnabled || !shiftedTab.IsEnabled || normalTab.Background!=selectedBackground || shiftedTab.Background!=inactiveBackground || !normalTab.Content.ToString()!.StartsWith("✓"))
                    throw new InvalidOperationException("Normal selected tab must be bright, marked and enabled");
                SelectLayer(true);
                if(shiftedTab.Background!=selectedBackground || normalTab.Background!=inactiveBackground || !shiftedTab.Content.ToString()!.StartsWith("✓"))
                    throw new InvalidOperationException("Shift selected tab highlight did not switch");
                Root.UpdateLayout();
                if (shiftedBody.ActualHeight <= 0 || shiftedBody.Children.OfType<TextBox>().Count() != 6)
                    throw new InvalidOperationException("Shift editor did not render all role and combination fields");
                SelectLayer(false);
            };
        }
        var errorLabel = new TextBlock { TextWrapping = TextWrapping.Wrap, Foreground = Brush(245, 160, 150) };
        body.Children.Add(errorLabel);
        var title = "编辑按键 " + key.ToUpperInvariant() + (shiftedKey is null ? "" : " · 普通 / Shift");
        var dialog = new ContentDialog { XamlRoot = Root.XamlRoot, Title = title,
            Content = new ScrollViewer { Content = body, MaxHeight = 540, VerticalScrollBarVisibility = ScrollBarVisibility.Auto },
            SecondaryButtonText = "恢复这个键", CloseButtonText = "关闭" };
        var saved = false;
        Action CaptureSave()
        {
                var changes = new Dictionary<string, JapaneseKeyDefinition?> { [key] = read() };
                if (shiftedKey is not null && readShifted is not null)
                {
                    var definition = readShifted();
                    // An untouched inherited uppercase layer must stay inherited,
                    // not become an explicit uppercase mapping merely by opening it.
                    if (definition.EmptyFirst || definition.First is not null || definition.Second is not null || definition.Single is not null ||
                        definition.Action != "default" || definition.Rules?.Count > 0 || definition.LongPress is not null || definition.LongPressCompose || hasShiftedOverride)
                        changes[shiftedKey] = definition;
                }
                return () => { if (!_uiTestMode) store.SaveKeys(table.Id, changes); };
        }
        AutoSaveDialog(dialog, body, () => { }, errorLabel, CaptureSave, () => saved=true);
        dialog.SecondaryButtonClick += async (_, args) =>
        {
            var deferral=args.GetDeferral();
            FlushPendingAutoSaves();
            try
            {
                await _keyEditorWrites;
                var changes = new Dictionary<string, JapaneseKeyDefinition?> { [key] = null };
                if (shiftedKey is not null) changes[shiftedKey] = null;
                if (!_uiTestMode) await Task.Run(()=>store.SaveKeys(table.Id, changes));
                saved = true;
            }
            catch (Exception error) { args.Cancel = true; errorLabel.Text = error.Message; }
            finally { deferral.Complete(); }
        };
        var closeTimer = DispatcherQueue.CreateTimer();
        Exception? dialogTestFailure=null;
        if (verifyLoadedDialog)
        {
            closeTimer.Interval = TimeSpan.FromMilliseconds(300);
            closeTimer.IsRepeating = false;
            closeTimer.Tick += async (_, _) =>
            {
                closeTimer.Stop();
                try {
                    verifyShiftLayer?.Invoke();
                    if(layerScreenshotPrefix is not null) {
                        await RenderAppearanceElementAsync(body,layerScreenshotPrefix+"-normal.png");
                        selectTestLayer?.Invoke(true);Root.UpdateLayout();
                        await RenderAppearanceElementAsync(body,layerScreenshotPrefix+"-shift.png");
                        selectTestLayer?.Invoke(false);
                    }
                } catch(Exception error) {dialogTestFailure=error;}
                finally {dialog.Hide();}
            };
            closeTimer.Start();
        }
        try { await dialog.ShowAsync(); }
        finally { closeTimer.Stop(); }
        if(dialogTestFailure is not null)throw dialogTestFailure;
        if (saved)
        {
            var offset = _contentScroll?.VerticalOffset ?? 0;
            SelectPage("japanese-table");
            _contentScroll?.ChangeView(null, offset, null, true);
            SetStatus(_uiTestMode ? "测试模式：未保存" : "按键已保存，当前输入窗口即时生效。", false);
        }
    }

    private (StackPanel Body, Func<JapaneseKeyDefinition> Read) BuildJapaneseKeyEditor(
        JapaneseInputTableStore store, JapaneseInputTable table, string key)
    {
        var original = store.BaseTable(table.Id);
        var inferred = JapaneseInputTableStore.InferKey(key, original);
        var current = store.KeyDefinition(table.Id, key);
        var body = new StackPanel { Spacing = 12, MinWidth = 330 };
        body.Children.Add(new TextBlock { Text = "第一键负责声母或拗音行，第二键负责韵尾；组合会按日语读音自动生成。单键输出可填假名、汉字或符号。",
            TextWrapping = TextWrapping.Wrap });
        TextBox Field(string label, string value, string hint, string id)
        {
            body.Children.Add(new TextBlock { Text = label });
            var edit = new TextBox { Text = value, PlaceholderText = hint, IsSpellCheckEnabled = false, IsTextPredictionEnabled = false };
            var scope = new Microsoft.UI.Xaml.Input.InputScope();
            scope.Names.Add(new Microsoft.UI.Xaml.Input.InputScopeName { NameValue = Microsoft.UI.Xaml.Input.InputScopeNameValue.Password });
            // Only the roman-letter role fields bypass the IME. Kana/text output
            // editors remain normal visible editors that accept Japanese input.
            if (id is "KeyFirst" or "KeySecond") edit.InputScope = scope;
            WatchExplicitEdit(edit);
            AutomationProperties.SetAutomationId(edit, id);
            body.Children.Add(edit);
            return edit;
        }
        var firstInitial = current.First ?? inferred.First;
        var secondInitial = current.Second ?? inferred.Second;
        var singleInitial = current.Single ?? inferred.Single;
        var first = Field("第一键（声母 / 拗音）", firstInitial, "例如 k、ky、sh、ry、f、v；留空取消", "KeyFirst");
        var second = Field("第二键（一个或多个韵尾）", secondInitial, "例如 a、ou、ati/iti；多个用 / 分隔", "KeySecond");
        var single = Field("单独按这个键的输出", singleInitial, "例如 っ、ー、，、＋；可留空", "KeySingle");
        first.IsEnabled = second.IsEnabled = single.IsEnabled = key.Length == 1;
        var emptyFirst = new CheckBox { Content = "空声母键：不输出文字，直接接后一个键的韵尾", IsChecked = current.EmptyFirst, IsEnabled = key.Length == 1 };
        AutomationProperties.SetAutomationId(emptyFirst, "KeyEmptyFirst");
        body.Children.Add(emptyFirst);
        body.Children.Add(new TextBlock { Text = "例如大写 A 设为空声母，小写 s=asu 时 As→あす。不是输入空白字符，也不是空格选词；普通输入不变。", TextWrapping = TextWrapping.Wrap, Foreground = Brush(165, 180, 198) });
        first.IsEnabled = single.IsEnabled = key.Length == 1 && !current.EmptyFirst;
        var longPress = Field("长按输出（留空关闭）", current.LongPress ?? "", "例如 a、A、ky 或文字；直接输出，不经过假名转换", "KeyLongPress");
        var longPressTime = Field("长按触发时间（毫秒）", current.LongPressMilliseconds.ToString(), "100～3000，默认 500", "KeyLongPressTime");
        longPress.IsEnabled = longPressTime.IsEnabled = key.Length == 1;
        var holdCompose=new CheckBox {Content="长按后进入候选，不直接上屏",IsChecked=current.LongPressCompose,IsEnabled=key.Length==1};
        AutomationProperties.SetAutomationId(holdCompose,"KeyLongPressCompose");body.Children.Add(holdCompose);
        body.Children.Add(new TextBlock { Text = "默认长按松手后直接上屏；勾选后可填写假名（如 あつ）或当前按键编码，按住达到时长即显示候选，松手后可继续输入。长按编码显示为字母加横线（如 F̄）。短按保持原行为。", TextWrapping = TextWrapping.Wrap, Foreground = Brush(165, 180, 198) });
        body.Children.Add(new TextBlock { Text = "按键功能（翻行只在有候选时生效）" });
        var actionNames = key.Length == 1 ? new[] { "沿用原行为", "参与日语按键表", "直接输出单键文字 / 原符号", "下一行候选", "上一行候选" }
            : new[] { "沿用原行为", "下一行候选", "上一行候选" };
        var actionValues = key.Length == 1 ? new[] { "default", "input", "literal", "next", "previous" } : new[] { "default", "next", "previous" };
        var action = new ComboBox { ItemsSource = actionNames, SelectedIndex = Math.Max(0, Array.IndexOf(actionValues, current.Action)), HorizontalAlignment = HorizontalAlignment.Stretch };
        AutomationProperties.SetAutomationId(action, "KeyAction");
        body.Children.Add(action);
        var combinations = new TextBox { AcceptsReturn = true, TextWrapping = TextWrapping.Wrap, MinHeight = 85,
            PlaceholderText = "例如：k; → かっ\n删除组合：ka →\n保留未完成字母：nn → ん | n",
            Text = string.Join("\n", (current.Rules ?? []).Select(p => p.Key + " → " +
                (p.Value is null ? "" : p.Value.Output + (p.Value.Pending == "" ? "" : " | " + p.Value.Pending)))) };
        WatchExplicitEdit(combinations);
        AutomationProperties.SetAutomationId(combinations, "KeyExactRules");
        body.Children.Add(new TextBlock { Text = "指定组合（优先于自动生成；一行一条，空输出表示移除该组合）", TextWrapping = TextWrapping.Wrap });
        body.Children.Add(combinations);
        var examples = table.Rules.Where(r => r.Input.StartsWith(key, StringComparison.Ordinal) || (r.Input.Length == 2 && r.Input.EndsWith(key, StringComparison.Ordinal)))
            .Take(35).Select(r => r.Input + " → " + r.Output + (r.Pending == "" ? "" : " | " + r.Pending));
        var preview = new TextBlock { Text = "当前组合：\n" + string.Join("　 /　", examples), TextWrapping = TextWrapping.Wrap, Foreground = Brush(165, 180, 198) };
        body.Children.Add(preview);
        body.Children.Add(new TextBlock { Text = "已启用的保留字仍优先显示；恢复这个键会清除该键自定义，返回原表。", TextWrapping = TextWrapping.Wrap });
        JapaneseKeyDefinition Read()
        {
            var rules = new Dictionary<string, JapaneseInputRule?>();
            foreach (var line in combinations.Text.Replace("\r", "").Split('\n').Where(s => !string.IsNullOrWhiteSpace(s)))
            {
                var separator = line.IndexOf('→');
                if (separator < 1) throw new ArgumentException("组合请使用“按键 → 输出”格式。");
                var code = line[..separator].Trim();
                if (!code.StartsWith(key, StringComparison.Ordinal) && !(code.Length == 2 && code.EndsWith(key, StringComparison.Ordinal)))
                    throw new ArgumentException("组合须以当前键开头，或是以当前键结尾的双键组合。");
                var value = line[(separator + 1)..].Trim();
                var pieces = value.Split('|', 2);
                rules[code] = value == "" ? null : new(code, pieces[0].Trim(), pieces.Length == 2 ? pieces[1].Trim() : "");
            }
            if (!int.TryParse(longPressTime.Text, out var milliseconds) || milliseconds is < 100 or > 3000)
                throw new ArgumentException("长按时间须为 100～3000 毫秒。");
            return new(first.Text == firstInitial ? current.First : first.Text,
                second.Text == secondInitial ? current.Second : second.Text,
                single.Text == singleInitial ? current.Single : single.Text,
                actionValues[Math.Max(0, action.SelectedIndex)], rules,
                longPress.Text == (current.LongPress ?? "") ? current.LongPress : longPress.Text, milliseconds, emptyFirst.IsChecked == true,holdCompose.IsChecked==true);
        }
        var timer = DispatcherQueue.CreateTimer();
        timer.Interval = TimeSpan.FromMilliseconds(180);
        timer.IsRepeating = false;
        var previewRevision=0;var previewRunning=false;var previewClosed=false;
        timer.Tick += async (_, _) =>
        {
            if(previewRunning){timer.Start();return;}
            var version=previewRevision;
            previewRunning=true;
            try
            {
                var definition=Read();
                var changed = await Task.Run(()=>store.PreviewKey(table.Id, key, definition));
                if(previewClosed || version!=previewRevision)return;
                preview.Text = "组合预览（有效修改自动保存）：\n" + string.Join("　 /　", changed.Rules
                    .Where(r => r.Input.StartsWith(key, StringComparison.Ordinal) || (r.Input.Length == 2 && r.Input.EndsWith(key, StringComparison.Ordinal)))
                    .Take(45).Select(r => r.Input + " → " + r.Output));
            }
            catch (Exception error) { if(!previewClosed && version==previewRevision)preview.Text = error.Message; }
            finally {previewRunning=false;}
        };
        foreach (var editor in new[] { first, second, single, combinations, longPress, longPressTime })
            editor.TextChanged += (_, _) => { previewRevision++;timer.Stop(); timer.Start(); };
        void EmptyChanged()
        {
            first.IsEnabled = single.IsEnabled = key.Length == 1 && emptyFirst.IsChecked != true;
            if (emptyFirst.IsChecked == true && actionValues[action.SelectedIndex] is "literal" or "next" or "previous")
                action.SelectedIndex = Array.IndexOf(actionValues, "input");
            previewRevision++;timer.Stop(); timer.Start();
        }
        emptyFirst.Checked += (_, _) => EmptyChanged();
        emptyFirst.Unchecked += (_, _) => EmptyChanged();
        holdCompose.Checked += (_,_)=>{previewRevision++;timer.Stop();timer.Start();};
        holdCompose.Unchecked += (_,_)=>{previewRevision++;timer.Stop();timer.Start();};
        body.Loaded += (_, _) => previewClosed=false;
        body.Unloaded += (_, _) => {previewClosed=true;previewRevision++;timer.Stop();};
        return (body, Read);
    }
}
