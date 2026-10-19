using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Automation;

namespace ZhongriSettingsV2;

public sealed partial class MainWindow
{
    private FrameworkElement BuildJapaneseLearningModeSwitch()
    {
        var row = TrackedSwitchRow("日语学习模式", "日语候选上屏后附带假名，如 好きです（すきです）。中文、英文和直接输出的符号保持原样；切换立即生效。",
            _store.ReadInputOptions().JapaneseLearningMode, out var toggle);
        AutomationProperties.SetAutomationId(toggle, "JapaneseLearningMode");
        toggle.Toggled += (_, _) =>
        {
            try
            {
                if (!_uiTestMode) _store.SaveInputOptions(_store.ReadInputOptions() with { JapaneseLearningMode = toggle.IsOn });
                SetStatus(toggle.IsOn ? "学习模式已开启：日语上屏附带括号假名。" : "学习模式已关闭：恢复普通上屏。", false);
            }
            catch (Exception error) { SetStatus(error.Message, true); }
        };
        var body = new StackPanel { Spacing = 12 };
        body.Children.Add(row);
        body.Children.Add(new TextBlock { Text = "注音格式", TextWrapping = TextWrapping.Wrap });
        var format = new ComboBox { HorizontalAlignment = HorizontalAlignment.Stretch,
            ItemsSource = new[] { "整句后附假名 · 秋葉原へ行きたい（あきはばらへいきたい）", "汉字后附假名 · 秋葉原（あきはばら）へ行（い）きたい" },
            SelectedIndex = _store.ReadInputOptions().JapaneseLearningKanji ? 1 : 0 };
        AutomationProperties.SetAutomationId(format, "JapaneseLearningFormat");
        format.SelectionChanged += (_, _) =>
        {
            try
            {
                if (!_uiTestMode) _store.SaveInputOptions(_store.ReadInputOptions() with { JapaneseLearningKanji = format.SelectedIndex == 1 });
                SetStatus("学习模式注音格式已保存，立即生效。", false);
            }
            catch (Exception error) { SetStatus(error.Message, true); }
        };
        body.Children.Add(format);
        body.Children.Add(new TextBlock { Text = "汉字注音按连续汉字词组显示，假名部分保持原样；读音无法唯一对齐时，回退到整句注音，避免标错。", TextWrapping = TextWrapping.Wrap });
        var spaceRow = TrackedSwitchRow("学习上屏后加空格", "日语词语与假名后追加一个半角空格，连续输入时分隔词语；仅在学习模式开启时生效。",
            _store.ReadInputOptions().JapaneseLearningSpace, out var space);
        AutomationProperties.SetAutomationId(space, "JapaneseLearningSpace");
        space.Toggled += (_, _) =>
        {
            try
            {
                if (!_uiTestMode) _store.SaveInputOptions(_store.ReadInputOptions() with { JapaneseLearningSpace = space.IsOn });
                SetStatus("学习模式空格选项已保存，立即生效。", false);
            }
            catch (Exception error) { SetStatus(error.Message, true); }
        };
        body.Children.Add(spaceRow);
        var newlineRow = TrackedSwitchRow("学习上屏后自动换行", "每个日语词语与假名后追加换行，不模拟 Enter、不触发聊天发送。与空格同时开启时，先空格再换行；仅在学习模式开启时生效。",
            _store.ReadInputOptions().JapaneseLearningNewline, out var newline);
        AutomationProperties.SetAutomationId(newline, "JapaneseLearningNewline");
        newline.Toggled += (_, _) =>
        {
            try
            {
                if (!_uiTestMode) _store.SaveInputOptions(_store.ReadInputOptions() with { JapaneseLearningNewline = newline.IsOn });
                SetStatus("学习模式换行选项已保存，立即生效。", false);
            }
            catch (Exception error) { SetStatus(error.Message, true); }
        };
        body.Children.Add(newlineRow);
        return body;
    }
}
