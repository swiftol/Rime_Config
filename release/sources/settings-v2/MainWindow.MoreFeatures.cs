using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using RimeSettings;

namespace ZhongriSettingsV2;

public sealed partial class MainWindow
{
    private ComboBox? _cloudCandidateCount;
    private ComboBox? _cloudLanguageMode;
    private ToggleSwitch? _candidateSelectionGuardToggle;
    private TextBox? _candidateSelectionGuardTime;
    internal void ShowCloudSettingsForUser() => SelectPage("cloud");
    internal void RunCloudLoadedPageTestStep(int step)
    {
        SelectPage(step % 3 == 0 ? "input" : "cloud");
        AppWindow.Resize(new Windows.Graphics.SizeInt32(step % 2 == 0 ? 820 : 1120, 780));
        Root.UpdateLayout();
        if (_currentPage != "cloud") return;
        if (_cloudCandidateCount!.ActualHeight <= 0 || _candidateSelectionGuardTime!.ActualHeight <= 0)
            throw new InvalidOperationException("Cloud page controls did not complete layout");
        _cloudCandidateCount.IsDropDownOpen = step % 2 == 0;
        _cloudCandidateCount.SelectedIndex = step % 10;
        if (_store.ReadCloudCandidateCount() != step % 10 + 1)
            throw new InvalidOperationException("Rendered cloud count selection did not save");
    }
    internal async Task RunCloudCountUiSelfTestAsync()
    {
        if (_store.ReadCloudCandidateCount() != 2) throw new InvalidOperationException("默认数量应为 2");
        ShowGoogleCloudCandidates();
        if (_store.ReadCloudLanguageMode() != 0) throw new InvalidOperationException("默认应自动判断语言");
        foreach (var value in new[] { 1, 2, 3, 0 })
        {
            _cloudLanguageMode!.SelectedIndex = value;
            if (_store.ReadCloudLanguageMode() != value) throw new InvalidOperationException("语言回调未保存");
        }
        foreach (var invalid in new[] { -1, 4 })
        {
            try { _store.SaveCloudLanguageMode(invalid); throw new InvalidOperationException("语言越界被接受"); }
            catch (ArgumentOutOfRangeException) { }
        }
        foreach (var value in new[] { 3, 1, 10, 2 })
        {
            _cloudCandidateCount!.SelectedItem = value;
            if (_store.ReadCloudCandidateCount() != value) throw new InvalidOperationException("数量回调未保存");
        }
        foreach (var invalid in new[] { 0, 11 })
        {
            try { _store.SaveCloudCandidateCount(invalid); throw new InvalidOperationException("越界被接受"); }
            catch (ArgumentOutOfRangeException) { }
        }
        if (!_candidateSelectionGuardToggle!.IsOn || _candidateSelectionGuardTime!.Text != "0.25")
            throw new InvalidOperationException("误选保护默认值不正确");
        _candidateSelectionGuardToggle.IsOn = false;
        if (_store.ReadCandidateSelectionGuardEnabled()) throw new InvalidOperationException("保护开关未保存");
        _candidateSelectionGuardTime.Text = "0.12";
        await Task.Delay(60);
        if (_store.ReadCandidateSelectionGuardMs() != 120) throw new InvalidOperationException("保护时间未保存");
        foreach (var invalid in new[] { "", "2", "abc", "NaN", "-0.1", "0.04" })
        {
            _candidateSelectionGuardTime.Text = invalid;
            await Task.Delay(60);
            if (_store.ReadCandidateSelectionGuardMs() != 120)
                throw new InvalidOperationException("无效保护时间覆盖了原设置");
        }
        _candidateSelectionGuardTime.Text = "0,25";
        await Task.Delay(60);
        if (_store.ReadCandidateSelectionGuardMs() != 250)
            throw new InvalidOperationException("小数保护时间未保存");
        _candidateSelectionGuardToggle.IsOn = true;
        if (!_store.ReadCandidateSelectionGuardEnabled()) throw new InvalidOperationException("保护未开启");
        foreach (var invalid in new[] { 49, 1001 })
        {
            try { _store.SaveCandidateSelectionGuard(true, invalid); throw new InvalidOperationException("保护时间越界被接受"); }
            catch (ArgumentOutOfRangeException) { }
        }
    }
    private void ShowHotkeys()
    {
        AddHeading("快捷键", "现有快捷键速查，以及学习模式和日语配置切换的自定义绑定。");
        var rows = new StackPanel();
        foreach (var (name, keys) in new (string, string)[]
        {
            ("展开候选 / 下一行", "Page Down / ] / ="),
            ("上一行", "Page Up / [ / -"),
            ("英文注释", "Ctrl + Alt + E"),
            ("日文注释", "Ctrl + Alt + J"),
            ("日语模糊匹配", "Ctrl + Alt + F"),
            ("实时句子翻译", "Ctrl + Alt + T")
        })
        {
            if (rows.Children.Count > 0) rows.Children.Add(Divider());
            rows.Children.Add(ControlRow(name, "", new TextBlock { Text = keys,
                VerticalAlignment = VerticalAlignment.Center }));
        }
        PageContent.Children.Add(Card("按键列表", "具体行为以当前输入方案为准。", rows));
        AutoSaveStatus();
        PageContent.Children.Add(Card("可自定义快捷键", "学习模式和配置切换，无需打开设置窗口。", BuildImeShortcuts()));
    }

    private void ShowSentenceTranslation()
    {
        AddHeading("小牛双语整句翻译", "中文显示日语和英语译文；日语显示中文和英语译文。候选下方不加语言前缀。");
        var options = _store.ReadInputOptions();
        var row = TrackedSwitchRow("启用小牛双语翻译",
            "成功译文自动保存到本地；再次遇到同一句时优先读取本地，仅缺少的语言才请求小牛。",
            options.Sentence, out var toggle);
        _sentenceTranslationSwitch = toggle;
        PageContent.Children.Add(Card("翻译设置", "只有开启后才会发送候选文字。", row));
        var sourceRow = TrackedSwitchRow("优先使用小牛注释",
            "关闭：保留旧词库已有注释，缺失时用小牛补充。开启：以小牛译文替换旧注释；已缓存的译文直接读取本地，未缓存才联网。始终只显示一套。",
            _store.ReadPreferNiuAnnotations(), out var sourceToggle);
        AutomationProperties.SetAutomationId(sourceToggle, "PreferNiuAnnotationsSwitch");
        _preferNiuAnnotationsSwitch = sourceToggle;
        PageContent.Children.Add(Card("注释来源", "旧词库注释与小牛缓存分开保存；切换即时生效，不删除任何译文。", sourceRow));
        sourceToggle.Toggled += (_, _) =>
        {
            if (_uiTestMode) { SetStatus("测试模式：注释来源开关已响应，未保存", false); return; }
            try
            {
                _store.SavePreferNiuAnnotations(sourceToggle.IsOn);
                SetStatus(sourceToggle.IsOn ? "已优先使用小牛注释" : "已优先使用旧词库注释", false);
            }
            catch (Exception error) { SetStatus("保存失败：" + error.Message, true); }
        };

        var appId = new TextBox { Header = "App ID", PlaceholderText = "小牛 App ID", Width = 250 };
        var apiKey = new PasswordBox { Header = "API Key", PlaceholderText =
            _store.HasNiuCredentials() ? "已保存；输入新凭据可替换" : "小牛 API Key", Width = 330 };
        AutomationProperties.SetAutomationId(appId, "NiuAppIdInput");
        AutomationProperties.SetAutomationId(apiKey, "NiuApiKeyInput");
        if (_uiTestMode)
            apiKey.PasswordChanged += (_, _) => File.WriteAllText(
                Path.Combine(Path.GetTempPath(), "ZhongriSettingsV2-credential-input-test.txt"),
                $"passwordLength={apiKey.Password.Length}");
        _credentialKeyBox = apiKey;
        WatchExplicitEdit(appId);
        WatchExplicitEdit(apiKey);
        var fields = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 12 };
        fields.Children.Add(appId);
        fields.Children.Add(apiKey);
        var test = new Button { Content = "测试双语连接" };
        var clear = new Button { Content = "删除凭据" };
        var actions = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10 };
        actions.Children.Add(test);
        actions.Children.Add(clear);
        var body = new StackPanel { Spacing = 16 };
        body.Children.Add(fields);
        body.Children.Add(actions);
        PageContent.Children.Add(Card("小牛翻译凭据", "API Key 仅加密保存于当前 Windows 用户；开启后输入文字会发送给小牛翻译。", body));
        _saveStatus = new TextBlock { Text = options.Sentence ? "当前已开启" : "当前已关闭（默认）",
            Foreground = Brush(165, 166, 177), TextWrapping = TextWrapping.Wrap };
        PageContent.Children.Add(_saveStatus);

        var saving = false;
        toggle.Toggled += async (_, _) =>
        {
            if (saving) return;
            FlushPendingAutoSaves();
            // Keep hot writes serialized on the UI thread, including when the
            // user immediately navigates to another settings page.
            var requested = toggle.IsOn;
            saving = true;
            toggle.IsEnabled = false;
            try
            {
                if (_uiTestMode) { SetStatus("测试模式：开关已响应，未保存", false); return; }
                if (requested && !_store.HasNiuCredentials())
                {
                    SetStatus("请先保存小牛 App ID 和 API Key。", true);
                    toggle.IsOn = false;
                    return;
                }
                var current = _store.ReadInputOptions();
                if (current.Sentence == requested) return;
                SetStatus("正在应用热设置…", false);
                _store.SaveInputOptions(current with { Sentence = requested });
                await Task.CompletedTask;
                SetStatus(requested ? "已开启双语翻译" : "已关闭双语翻译", false);
            }
            catch (Exception error)
            {
                toggle.IsOn = _store.ReadInputOptions().Sentence;
                SetStatus("未能生效：" + error.Message, true);
            }
            finally { toggle.IsEnabled = true; saving = false; }
        };
        var credentialsChanged = AutoSave(() =>
        {
            if (string.IsNullOrWhiteSpace(appId.Text) || string.IsNullOrWhiteSpace(apiKey.Password))
                throw new ArgumentException("请填写完整 App ID 和 API Key；原凭据未改变。");
            _store.SaveNiuCredentials(appId.Text, apiKey.Password);
            apiKey.PlaceholderText = "已自动加密保存";
        }, delay: 800, values:()=>appId.Text+Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(apiKey.Password))));
        appId.TextChanged += (_, _) => { if (appId.Text.Length > 0 && apiKey.Password.Length > 0) credentialsChanged(); };
        apiKey.PasswordChanged += (_, _) => { if (appId.Text.Length > 0 && apiKey.Password.Length > 0) credentialsChanged(); };
        test.Click += async (_, _) =>
        {
            FlushPendingAutoSaves();
            if (_uiTestMode) { SetStatus("测试模式：未发送翻译请求", false); return; }
            test.IsEnabled = false;
            SetStatus("正在测试日语和英语翻译…", false);
            try { SetStatus("连接成功：" + await _store.TestNiuConnectionAsync(), false); }
            catch (Exception error) { SetStatus("测试失败：" + error.Message, true); }
            finally { test.IsEnabled = true; }
        };
        clear.Click += async (_, _) =>
        {
            FlushPendingAutoSaves();
            if (_uiTestMode) { SetStatus("测试模式：未删除凭据", false); return; }
            clear.IsEnabled = false;
            try
            {
                if (toggle.IsOn)
                {
                    toggle.IsOn = false;
                    while (saving) await Task.Delay(50);
                }
                _store.ClearNiuCredentials();
                appId.Text = "";
                apiKey.Password = "";
                apiKey.PlaceholderText = "小牛 API Key";
                SetStatus("凭据已删除，翻译已关闭", false);
            }
            catch (Exception error) { SetStatus("删除失败：" + error.Message, true); }
            finally { clear.IsEnabled = true; }
        };
    }

    private void ShowGoogleCloudCandidates()
    {
        ShowOptionalFeature(
        "Google 云候选", "本地候选立即显示；云端结果异步补入，成功结果缓存在本地。",
        "启用 Google 云候选", "会将中文拼写或日语假名读音发送给 Google，默认关闭。",
        options => options.GoogleCloudCandidates,
        (options, enabled) => options with { GoogleCloudCandidates = enabled }, true);
        var language = new ComboBox { Width = 190,
            ItemsSource = new[] { "自动判断", "仅中文", "仅日语", "中日双路" },
            SelectedIndex = _store.ReadCloudLanguageMode() };
        _cloudLanguageMode = language;
        AutomationProperties.SetAutomationId(language, "CloudLanguageMode");
        PageContent.Children.Add(Card("云匹配语言", "自动综合拼法、完整日语结果与近期选词判断；无法确定时保留两路。日语双拼按当前按键表解析，不把原始字母当普通罗马字。",
            ControlRow("查询方式", "选择后即时生效；中日双路按优先语言交错显示。", language)));
        language.SelectionChanged += (_, _) =>
        {
            if (language.SelectedIndex < 0) return;
            try
            {
                if (!_uiTestMode) _store.SaveCloudLanguageMode(language.SelectedIndex);
                SetStatus(_uiTestMode ? "测试模式：语言已响应，未保存" : "云匹配语言已即时生效", false);
            }
            catch (Exception error) { SetStatus("保存失败：" + error.Message, true); }
        };
        var count = new ComboBox { Width = 140, ItemsSource = Enumerable.Range(1, 10).ToArray(),
            SelectedItem = _store.ReadCloudCandidateCount() };
        _cloudCandidateCount = count;
        AutomationProperties.SetAutomationId(count, "CloudCandidateCount");
        PageContent.Children.Add(Card("云候选数量", "云端优先，同名本地候选去重；中日双路共用此总数量，去重后可能少于设定值。选择后即时生效。",
            ControlRow("最多显示", "可选 1–10 个云候选", count)));
        count.SelectionChanged += (_, _) =>
        {
            if (count.SelectedItem is not int value) return;
            try
            {
                if (!_uiTestMode) _store.SaveCloudCandidateCount(value);
                SetStatus(_uiTestMode ? "测试模式：数量已响应，未保存" : $"云候选最多 {value} 个，已即时生效", false);
            }
            catch (Exception error) { SetStatus("保存失败：" + error.Message, true); }
        };
        var protectionRow = TrackedSwitchRow("候选变动误选保护",
            "同一串输入的候选刚换序时，保护时间内按数字选词仍选择变动前对应的词；不等待、不延迟上屏。继续输入或主动翻行后重新计时。",
            _store.ReadCandidateSelectionGuardEnabled(), out var protection);
        // NumberBox's native number formatter can terminate XAML during load.
        // Use the same plain-text editing path as the other settings pages.
        var protectionTime = new TextBox { Width = 160,
            Text = (_store.ReadCandidateSelectionGuardMs() / 1000.0).ToString("0.00", System.Globalization.CultureInfo.InvariantCulture) };
        _candidateSelectionGuardToggle = protection;
        _candidateSelectionGuardTime = protectionTime;
        AutomationProperties.SetAutomationId(protection, "CandidateSelectionGuardEnabled");
        AutomationProperties.SetAutomationId(protectionTime, "CandidateSelectionGuardSeconds");
        var protectionControls = new StackPanel { Spacing = 12 };
        protectionControls.Children.Add(protectionRow);
        protectionControls.Children.Add(ControlRow("保护时间（秒）", "0.05–1.00 秒，默认 0.25 秒。关闭开关后按新顺序正常选词。", protectionTime));
        PageContent.Children.Add(Card("防止云候选突然换序导致误选", "保护的是原来的词，不是原来的序号；旧词已消失时本次按键不选其他词。", protectionControls));
        void SaveProtection()
        {
            if (!double.TryParse(protectionTime.Text.Replace(',', '.'), System.Globalization.NumberStyles.AllowDecimalPoint,
                    System.Globalization.CultureInfo.InvariantCulture, out var seconds) ||
                !double.IsFinite(seconds) || seconds < 0.05 || seconds > 1.0) return;
            try
            {
                if (!_uiTestMode) _store.SaveCandidateSelectionGuard(protection.IsOn,
                    (int)Math.Round(seconds * 1000, MidpointRounding.AwayFromZero));
                SetStatus(_uiTestMode ? "测试模式：误选保护已响应，未保存" : "候选误选保护已即时生效", false);
            }
            catch (Exception error) { SetStatus("保存失败：" + error.Message, true); }
        }
        protection.Toggled += (_, _) => SaveProtection();
        protectionTime.TextChanged += (_, _) => SaveProtection();
        protectionTime.LostFocus += (_, _) =>
        {
            if (!double.TryParse(protectionTime.Text.Replace(',', '.'), System.Globalization.NumberStyles.AllowDecimalPoint,
                    System.Globalization.CultureInfo.InvariantCulture, out var seconds) ||
                !double.IsFinite(seconds) || seconds < 0.05 || seconds > 1.0)
            {
                protectionTime.Text = (_store.ReadCandidateSelectionGuardMs() / 1000.0)
                    .ToString("0.00", System.Globalization.CultureInfo.InvariantCulture);
                SetStatus("保护时间须为 0.05–1.00 秒，已恢复原值", true);
            }
        };
    }

    private void ShowOptionalFeature(string title, string subtitle, string switchTitle,
        string switchDescription, Func<InputOptions, bool> read,
        Func<InputOptions, bool, InputOptions> update, bool confirmExternal = false)
    {
        AddHeading(title, subtitle);
        var options = _store.ReadInputOptions();
        var row = TrackedSwitchRow(switchTitle, switchDescription, read(options), out var toggle);
        PageContent.Children.Add(Card("功能设置", "开关变化立即作用于已打开的输入窗口。", row));
        _saveStatus = new TextBlock { Text = read(options) ? "当前已开启" : "当前已关闭",
            Foreground = Brush(165, 166, 177), TextWrapping = TextWrapping.Wrap };
        PageContent.Children.Add(_saveStatus);
        var saving = false;
        toggle.Toggled += async (_, _) =>
        {
            if (saving) return;
            saving = true;
            toggle.IsEnabled = false;
            try
            {
                var current = _store.ReadInputOptions();
                if (read(current) == toggle.IsOn)
                {
                    SetStatus(toggle.IsOn ? "当前已开启" : "当前已关闭", false);
                    return;
                }
                if (confirmExternal && toggle.IsOn && !_uiTestMode)
                {
                    var dialog = new ContentDialog
                    {
                        XamlRoot = Root.XamlRoot,
                        Title = "启用 Google 云候选？",
                        Content = "中文拼写或日语假名读音会发送给 Google，以取得云端候选；中日双路会查询两种语言。",
                        PrimaryButtonText = "同意并开启",
                        CloseButtonText = "取消",
                        DefaultButton = ContentDialogButton.Close
                    };
                    if (await dialog.ShowAsync() != ContentDialogResult.Primary)
                    {
                        toggle.IsOn = false;
                        return;
                    }
                }
                if (_uiTestMode)
                {
                    SetStatus("测试模式：开关已响应，未保存", false);
                    return;
                }
                SetStatus("正在应用热设置…", false);
                var changed = update(_store.ReadInputOptions(), toggle.IsOn);
                _store.SaveInputOptions(changed);
                await Task.CompletedTask;
                SetStatus(toggle.IsOn ? "已开启" : "已关闭", false);
            }
            catch (Exception error)
            {
                toggle.IsOn = read(_store.ReadInputOptions());
                SetStatus("未能生效：" + error.Message, true);
            }
            finally
            {
                toggle.IsEnabled = true;
                saving = false;
            }
        };
    }
}
