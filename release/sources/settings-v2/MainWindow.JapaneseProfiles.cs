using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Automation;

namespace ZhongriSettingsV2;

public sealed partial class MainWindow
{
    private FrameworkElement BuildJapaneseProfileManager(JapaneseInputTableStore store)
    {
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        foreach (var (operation, title) in new[] { ("new", "新增配置"), ("copy", "复制配置"), ("rename", "重命名"), ("delete", "删除配置") })
        {
            var button = new Button { Content = title };
            AutomationProperties.SetAutomationId(button, "JapaneseProfile_" + operation);
            button.Click += async (_, _) =>
            {
                try
                {
                    var selected = _tableProfilePicker!.SelectedIndex;
                    var profiles = store.Profiles();
                    var profile = selected > 0 && selected <= profiles.Count ? profiles[selected - 1] : null;
                    if (operation != "new" && profile is null) throw new ArgumentException("请先选中一套配置；默认罗马字可用“新增配置”创建可编辑版本。");
                    if (operation == "delete")
                    {
                        var confirm = new ContentDialog { XamlRoot = Root.XamlRoot, Title = "删除“" + profile!.Name + "”？",
                            Content = "从配置列表移除；当前配置会切换到其他可用配置，没有其他配置时回到默认罗马字。原表和个人设置保留，可恢复。",
                            PrimaryButtonText = "删除", CloseButtonText = "取消" };
                        if (await confirm.ShowAsync() != ContentDialogResult.Primary) return;
                        if (!_uiTestMode) store.DeleteProfile(profile.Id);
                    }
                    else
                    {
                        var name = new TextBox { Text = operation == "new" ? "新日语配置" : operation == "copy" ? profile!.Name + " 副本" : profile!.Name,
                            PlaceholderText = "配置名称" };
                        WatchExplicitEdit(name);
                        var error = new TextBlock { TextWrapping = TextWrapping.Wrap, Foreground = Brush(245, 160, 150) };
                        var content = new StackPanel { Spacing = 12 };
                        content.Children.Add(name); content.Children.Add(error);
                        if (operation == "new") content.Children.Add(new TextBlock { Text = "以标准罗马字建立可编辑配置，然后点击键盘自定义。要沿用当前双拼方案，请选择“复制配置”。", TextWrapping = TextWrapping.Wrap });
                        var dialog = new ContentDialog { XamlRoot = Root.XamlRoot, Title = title, Content = content,
                            PrimaryButtonText = operation == "rename" ? "" : "创建", CloseButtonText = operation == "rename" ? "关闭" : "取消" };
                        var saved = false;
                        if (operation == "rename") AutoSaveDialog(dialog,content,()=> {
                            if (!_uiTestMode) store.RenameProfile(profile!.Id,name.Text);
                            saved=true;
                        },error);
                        dialog.PrimaryButtonClick += (_, args) =>
                        {
                            try
                            {
                                if (!_uiTestMode)
                                {
                                    if (operation == "new") store.CreateEditableRomaji(name.Text);
                                    else if (operation == "copy") { var copy = store.CopyProfile(profile!.Id, name.Text); store.SelectProfile(copy.Id, store.Enabled); }
                                    else store.RenameProfile(profile!.Id, name.Text);
                                }
                                saved = true;
                            }
                            catch (Exception ex) { args.Cancel = true; error.Text = ex.Message; }
                        };
                        await dialog.ShowAsync();
                        if (!saved) return;
                    }
                    SelectPage("japanese-table");
                    SetStatus(_uiTestMode ? "测试模式：未保存" : operation == "delete" ? "已从列表移除，原配置数据保留，可恢复。" : "配置已保存。", false);
                }
                catch (Exception ex) { SetStatus(ex.Message, true); }
            };
            buttons.Children.Add(button);
        }
        return buttons;
    }
}
