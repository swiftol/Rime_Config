using System.Globalization;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using RimeSettings;

namespace ZhongriSettingsV2;

public sealed partial class MainWindow
{
    private ToggleSwitch? _candidateSplitSwitch;
    private TextBox? _candidateSplitLeft,_candidateSplitRight;
    private ComboBox? _candidateSplitPreset;
    private void ShowOtherInputOptions(InputOptions options)
    {
        ToggleSwitch AddToggle(StackPanel group, string title, string description, bool value)
        {
            if (group.Children.Count > 0) group.Children.Add(Divider());
            group.Children.Add(TrackedSwitchRow(title, description, value, out var toggle));
            return toggle;
        }

        var appearance=_store.ReadAppearance();
        var splitPanel=new StackPanel();
        var split=AddToggle(splitPanel,"左中文、右日文（试用）","普通单行和展开候选区都分栏；关闭恢复混排，不删除候选。",appearance.CandidateLanguageSplit);
        var leftColumns=new TextBox { Width=112,Text=appearance.CandidateSplitChineseColumns.ToString(CultureInfo.InvariantCulture),MaxLength=1 };
        var rightColumns=new TextBox { Width=112,Text=appearance.CandidateSplitJapaneseColumns.ToString(CultureInfo.InvariantCulture),MaxLength=1 };
        WatchExplicitEdit(leftColumns);WatchExplicitEdit(rightColumns);
        _candidateSplitSwitch=split;_candidateSplitLeft=leftColumns;_candidateSplitRight=rightColumns;
        var preset=new ComboBox {Width=240};
        foreach(var title in new[]{"左 3 ／右 3","左 4 ／右 2","左 2 ／右 4","自定义列数"})preset.Items.Add(title);
        int PresetIndex()=>leftColumns.Text=="3"&&rightColumns.Text=="3"?0:leftColumns.Text=="4"&&rightColumns.Text=="2"?1:leftColumns.Text=="2"&&rightColumns.Text=="4"?2:3;
        preset.SelectedIndex=PresetIndex();_candidateSplitPreset=preset;
        splitPanel.Children.Add(ControlRow("布局比例","左侧中文，右侧日文；也可自由填写下面的列数。",preset));
        splitPanel.Children.Add(ControlRow("左侧中文列数","左右各至少 1 列，合计最多 9 列",leftColumns));
        splitPanel.Children.Add(ControlRow("右侧日文列数","3／3 时首行 5／4；4／2 时首行 6／3。编号跟随当前行。",rightColumns));
        PageContent.Children.Add(Card("中日候选分栏","语言内部保留原顺序；首行最多九项，按左右比例分配。",splitPanel));
        var splitChanged=AutoSave(()=> {
            int l=ValidNumber(leftColumns,1,8),r=ValidNumber(rightColumns,1,8);
            if(l+r>9)throw new ArgumentException("左右合计不能超过九列。");
            var current=_store.ReadAppearance();
            var next=current with { CandidateLanguageSplit=split.IsOn,CandidateSplitChineseColumns=l,CandidateSplitJapaneseColumns=r };
            if(next!=current)_store.SaveAppearanceHot(next);
        },values:()=>split.IsOn+"|"+leftColumns.Text+"|"+rightColumns.Text);
        split.Toggled+=(_,_)=>splitChanged();
        void EnableSplitControls(){preset.IsEnabled=leftColumns.IsEnabled=rightColumns.IsEnabled=split.IsOn;}
        EnableSplitControls();split.Toggled+=(_,_)=>EnableSplitControls();
        bool changingPreset=false;
        preset.SelectionChanged+=(_,_)=> {
            if(changingPreset||preset.SelectedIndex==3)return;
            changingPreset=true;
            var pair=preset.SelectedIndex==1?(4,2):preset.SelectedIndex==2?(2,4):(3,3);
            leftColumns.Text=pair.Item1.ToString();rightColumns.Text=pair.Item2.ToString();
            changingPreset=false;splitChanged();
        };
        void ColumnsChanged(){if(changingPreset)return;changingPreset=true;preset.SelectedIndex=PresetIndex();changingPreset=false;splitChanged();}
        leftColumns.TextChanged+=(_,_)=>ColumnsChanged();rightColumns.TextChanged+=(_,_)=>ColumnsChanged();
        var preedit = new StackPanel();
        var inline = AddToggle(preedit, "输入字母显示在应用输入框",
            "关闭后输入码显示在候选窗顶部", options.InlinePreedit);
        var inlineRaw = AddToggle(preedit, "输入码连续显示",
            "开启显示连续字母；关闭后保留音节间隔", options.InlinePreeditRawInput);
        PageContent.Children.Add(Card("输入码显示", "应用后即时更新，无需重开输入窗口。", preedit));

        var keys = new StackPanel();
        var enter = new ComboBox { Width=330 };
        foreach(var text in new[]{"上屏输入的英语原文", "选择第一候选", "选择第一候选并发送 Enter", "上屏英语原文并发送 Enter"}) enter.Items.Add(text);
        enter.SelectedIndex=options.EffectiveEnterMode;
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetAutomationId(enter,"EnterBehaviorMode");
        keys.Children.Add(ControlRow("回车行为", "四种模式互斥；没有正在输入的内容时，回车保持应用原有功能。", enter));
        keys.Children.Add(Divider());
        var space = new ComboBox { Width = 218, SelectedIndex = options.SpaceSelectFirst ? 1 :
            options.SpaceReadingPreview ? 2 : 0 };
        space.Items.Add("原文＋空格");
        space.Items.Add("选择第一候选");
        space.Items.Add("按住看读音");
        keys.Children.Add(ControlRow("空格行为", "三种模式互斥", space));
        var longPress = AddToggle(keys, "读音预览仅长按触发",
            "短按保留原功能，长按才显示读音", options.LongPressReadingPreview);
        var alt = AddToggle(keys, "按住 Alt 看读音",
            "松开后恢复，不改变中英文输入模式", options.AltReadingPreview);
        PageContent.Children.Add(Card("按键行为", "应用后即时更新，已打开的输入窗口也会使用新设置。", keys));

        var comments = new StackPanel();
        var expandedWidth = AddToggle(comments, "展开注释参与宽度",
            "长注释会扩展候选项宽度", options.ExpandedCommentWidth);
        var expandedAlign = AddToggle(comments, "展开注释对齐编号",
            "让展开注释从候选编号处开始", options.ExpandedCommentAlignLabel);
        var hoverScroll = AddToggle(comments, "长注释悬停滚动",
            "关闭后悬停显示完整注释弹窗", options.HoverCommentScroll);
        PageContent.Children.Add(Card("候选注释显示", "控制较长注释的布局和阅读方式。", comments));

        var filtering = new StackPanel();
        var prefix = AddToggle(filtering, "日语联想",
            "为尚未输完的罗马字显示补全候选", options.JapanesePrefixCompletion);
        var suffix = AddToggle(filtering, "日语联想显示剩余码",
            "标出补全该候选还需输入的编码", options.JapanesePrefixCompletionSuffix);
        var japaneseFirst = AddToggle(filtering, "日语联想优先",
            "仅在日语联想开启时生效", options.JapanesePrefixCompletionJapaneseFirst);
        var continuation = AddToggle(filtering, "日语续输锁定",
            "前段选择日语后，剩余编码只匹配日语", options.JapaneseContinuationLock);
        var emoji = AddToggle(filtering, "Emoji 固定第二候选",
            "即使已学习或高频，也不占用第一候选", options.EmojiSecondPosition);
        filtering.Children.Add(Divider());
        var rare = new TextBox { Width = 112, Text = _store.ReadRareSingleCharThreshold()
            .ToString(CultureInfo.InvariantCulture), MaxLength = 5 };
        WatchExplicitEdit(rare);
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetAutomationId(rare,"RareCharacterThreshold");
        filtering.Children.Add(ControlRow("生僻单字过滤门槛",
            "0 表示不过滤；建议 4000", rare));
        PageContent.Children.Add(Card("候选过滤与续输", "联想、Emoji 和单字门槛应用后即时生效。", filtering));

        AutoSaveStatus();
        var changed = AutoSave(() =>
        {
            var threshold = ValidNumber(rare, 0, 50000);
            var current = _store.ReadInputOptions();
            var updated = current with
            {
                InlinePreedit = inline.IsOn,
                InlinePreeditRawInput = inlineRaw.IsOn,
                EnterMode = enter.SelectedIndex,
                EnterSubmitsToApp = enter.SelectedIndex == 3,
                SpaceSelectFirst = space.SelectedIndex == 1,
                SpaceReadingPreview = space.SelectedIndex == 2,
                LongPressReadingPreview = longPress.IsOn,
                AltReadingPreview = alt.IsOn,
                ExpandedCommentWidth = expandedWidth.IsOn,
                ExpandedCommentAlignLabel = expandedAlign.IsOn,
                HoverCommentScroll = hoverScroll.IsOn,
                JapanesePrefixCompletion = prefix.IsOn,
                JapanesePrefixCompletionSuffix = suffix.IsOn,
                JapanesePrefixCompletionJapaneseFirst = japaneseFirst.IsOn,
                JapaneseContinuationLock = continuation.IsOn,
                EmojiSecondPosition = emoji.IsOn
            };
            var previousThreshold = _store.ReadRareSingleCharThreshold();
            if (updated == current && threshold == previousThreshold) return;
            _store.SaveInputOptions(updated);
            if (threshold != previousThreshold) _store.SaveRareSingleCharThreshold(threshold);
        }, values:()=>rare.Text+"|"+space.SelectedIndex+"|"+enter.SelectedIndex+"|"+string.Join("|",new[]{inline,inlineRaw,longPress,alt,expandedWidth,expandedAlign,hoverScroll,prefix,suffix,japaneseFirst,continuation,emoji}.Select(t=>t.IsOn)));
        foreach (var toggle in new[] { inline, inlineRaw, longPress, alt, expandedWidth,
            expandedAlign, hoverScroll, prefix, suffix, japaneseFirst, continuation, emoji })
            toggle.Toggled += (_, _) => changed();
        space.SelectionChanged += (_, _) => changed();
        enter.SelectionChanged += (_, _) => changed();
        rare.TextChanged += (_, _) => changed();
    }

    private void ShowMaintenance()
    {
        AddHeading("维护", "查看当前配置目录，按需重启算法服务或重新部署输入法。");
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10 };
        var open = new Button { Content = "打开用户文件夹" };
        var restart = new Button { Content = "重启算法服务" };
        var deploy = new Button { Content = "完整重新部署" };
        buttons.Children.Add(open);
        buttons.Children.Add(restart);
        buttons.Children.Add(deploy);
        _saveStatus = new TextBlock { Text = "注释和外观开关不需要完整部署。",
            Foreground = Brush(165, 166, 177), TextWrapping = TextWrapping.Wrap };
        var body = new StackPanel { Spacing = 16 };
        body.Children.Add(new TextBlock { Text = _store.RimeDirectory,
            Foreground = Brush(165, 166, 177), TextWrapping = TextWrapping.Wrap });
        body.Children.Add(buttons);
        body.Children.Add(_saveStatus);
        PageContent.Children.Add(Card("当前输入法", "只操作正在使用的中日输入法用户配置。", body));

        open.Click += (_, _) =>
        {
            if (_uiTestMode) { SetStatus("测试模式：未打开文件夹", false); return; }
            try { RimeRuntime.OpenRimeDirectory(_store.RimeDirectory); }
            catch (Exception error) { SetStatus("打开失败：" + error.Message, true); }
        };
        restart.Click += async (_, _) => await RunMaintenanceAsync(restart,
            "正在重启算法服务…", RimeRuntime.RestartServerAsync);
        deploy.Click += async (_, _) =>
        {
            if (!_uiTestMode)
            {
                var dialog = new ContentDialog
                {
                    XamlRoot = Root.XamlRoot,
                    Title = "重新部署输入法？",
                    Content = "完整部署会暂时停止输入法服务，可能需要一些时间。",
                    PrimaryButtonText = "开始部署",
                    CloseButtonText = "取消",
                    DefaultButton = ContentDialogButton.Close
                };
                if (await dialog.ShowAsync() != ContentDialogResult.Primary) return;
            }
            await RunMaintenanceAsync(deploy, "正在完整部署…", RimeRuntime.DeployAsync);
        };
    }

    private async Task RunMaintenanceAsync(Button button, string message, Func<Task> action)
    {
        button.IsEnabled = false;
        SetStatus(message, false);
        try
        {
            if (_uiTestMode) SetStatus("测试模式：按钮已响应，未重启或部署", false);
            else
            {
                await action();
                SetStatus("已完成", false);
            }
        }
        catch (Exception error) { SetStatus("操作失败：" + error.Message, true); }
        finally { button.IsEnabled = true; }
    }
}
