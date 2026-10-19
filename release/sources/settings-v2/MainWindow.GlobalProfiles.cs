using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Automation;

namespace ZhongriSettingsV2;

public sealed partial class MainWindow
{
    internal async Task RunGlobalProfilesSelfTestAsync(string screenshot)
    {
        var keys = new JapaneseInputTableStore(_store.RimeDirectory, _store.HotSettingsRegistryPath);
        var keyboard = keys.CreateEditableRomaji();
        keys.SaveKeys(keyboard.Id, new Dictionary<string, JapaneseKeyDefinition?> { ["s"] = new(Second:"aku"), ["t"] = new(Second:"itsu") });
        SelectPage("japanese-table");
        if (_tableOverlapSwitch is null || _tableOverlapSwitch.IsOn) throw new InvalidOperationException("Overlap control missing/default not OFF");
        _tablePreviewInput!.Text = "as";
        _tableOverlapSwitch.IsOn = true;
        if (!keys.VowelTailOverlap || _tablePreviewResult!.Text != "あく") throw new InvalidOperationException("Overlap switch did not hot-save/update preview");
        await RenderAppearanceElementAsync(Root,screenshot+".vowel-tail.png");
        _tableOverlapSwitch.IsOn = false;
        if (keys.VowelTailOverlap || _tablePreviewResult.Text == "あく") throw new InvalidOperationException("Overlap OFF did not restore preview");
        await EditJapaneseKeyAsync("s",keys.Read()!,true,screenshot+".key-layer");
        var store=new GlobalSettingsProfileStore(_store);
        var first=store.Add("日语＋中文");
        _store.SaveInputOptions(_store.ReadInputOptions() with { JapaneseLearningMode=true,Fuzzy=true });
        keys.SetVowelTailOverlap(true);
        var second=store.Add("日语学习");
        SelectPage("global-profiles");Root.UpdateLayout();await Task.Delay(200);
        if(!_navButtons.ContainsKey("global-profiles"))throw new InvalidOperationException("Missing sidebar entry");
        var picker=AutoSaveControls<ComboBox>(PageContent).Single(c=>AutomationProperties.GetAutomationId(c)=="GlobalProfilePicker");
        if(picker.Items.Count!=2)throw new InvalidOperationException("Profile list missing");
        picker.SelectedItem=((IEnumerable<GlobalSettingsProfile>)picker.ItemsSource).Single(p=>p.Id==first.Id);
        await Task.Delay(200);Root.UpdateLayout();
        if(_store.ReadInputOptions().JapaneseLearningMode || keys.VowelTailOverlap || store.Read().CurrentId!=first.Id)throw new InvalidOperationException("Picker did not hot-apply first profile");
        picker=AutoSaveControls<ComboBox>(PageContent).Single(c=>AutomationProperties.GetAutomationId(c)=="GlobalProfilePicker");
        picker.SelectedItem=((IEnumerable<GlobalSettingsProfile>)picker.ItemsSource).Single(p=>p.Id==second.Id);
        await Task.Delay(200);Root.UpdateLayout();
        if(!_store.ReadInputOptions().JapaneseLearningMode || !keys.VowelTailOverlap || store.Read().CurrentId!=second.Id)throw new InvalidOperationException("Picker did not hot-apply second profile");
        foreach(var action in new[]{"new","save","copy","rename","delete"})
            if(!AutoSaveControls<Button>(PageContent).Any(b=>AutomationProperties.GetAutomationId(b)=="GlobalProfile_"+action))throw new InvalidOperationException("Missing profile action: "+action);
        await RenderAppearanceElementAsync(Root,screenshot);
    }

    private void ShowGlobalProfiles()
    {
        AddHeading("自定义配置","整套功能开关与外观方案；与日语双拼按键表配置分开管理。");
        var store=new GlobalSettingsProfileStore(_store);
        GlobalSettingsDocument doc;
        try { doc=store.Read(); } catch(Exception error) { PageContent.Children.Add(Info(error.Message));return; }
        var profiles=doc.Profiles.Where(p=>!p.Deleted).ToList();
        var picker=new ComboBox { ItemsSource=profiles,DisplayMemberPath="Name",HorizontalAlignment=HorizontalAlignment.Stretch,
            PlaceholderText="先新建一套配置，例如：日语＋中文" };
        AutomationProperties.SetAutomationId(picker,"GlobalProfilePicker");
        picker.SelectedItem=profiles.FirstOrDefault(p=>p.Id==doc.CurrentId);
        var status=new TextBlock { TextWrapping=TextWrapping.Wrap,Foreground=Brush(143,205,177) };
        void Status() {
            var active=profiles.FirstOrDefault(p=>p.Id==doc.CurrentId);
            status.Text=active is null?"当前设置尚未保存为方案。新建会保存当前设置，不会重置开关。":
                "当前方案："+active.Name+(store.Matches(active.State)?" · 与保存内容一致":" · 当前有修改，可保存更新或另存为新方案");
        }
        Status();
        var body=new StackPanel { Spacing=12 };body.Children.Add(picker);body.Children.Add(status);
        foreach(var row in new[]{new[]{("new","新建配置"),("save","保存当前配置")},new[]{("copy","复制"),("rename","重命名"),("delete","删除")}}) {
            var buttons=new StackPanel { Orientation=Orientation.Horizontal,Spacing=8 };
            foreach(var (action,title) in row) {
                var button=new Button { Content=title };
                AutomationProperties.SetAutomationId(button,"GlobalProfile_"+action);
                button.Click+=async(_,_)=> {
                    try {
                        FlushPendingAutoSaves();
                        var selected=picker.SelectedItem as GlobalSettingsProfile;
                        if(action!="new" && selected is null) throw new ArgumentException("请先选择一套配置。");
                        if(action=="save") {
                            if(!_uiTestMode)store.SaveCurrent(selected!.Id);
                        } else if(action=="delete") {
                            var confirm=new ContentDialog { XamlRoot=Root.XamlRoot,Title="删除“"+selected!.Name+"”？",
                                Content="只从总配置列表移除。当前生效的设置、词库、学习数据和日语按键表不变；配置保留回收记录。",
                                PrimaryButtonText="删除",CloseButtonText="取消",DefaultButton=ContentDialogButton.Close };
                            if(await confirm.ShowAsync()!=ContentDialogResult.Primary)return;
                            if(!_uiTestMode)store.Delete(selected.Id);
                        } else {
                            var name=new TextBox { Text=action=="new"?"日语＋中文":action=="copy"?selected!.Name+" 副本":selected!.Name,MaxLength=64 };
                            WatchExplicitEdit(name);
                            var error=new TextBlock { TextWrapping=TextWrapping.Wrap,Foreground=Brush(239,139,139) };
                            var content=new StackPanel { Spacing=12 };content.Children.Add(name);content.Children.Add(error);
                            var dialog=new ContentDialog { XamlRoot=Root.XamlRoot,Title=title,Content=content,PrimaryButtonText=action=="rename"?"重命名":"创建",CloseButtonText="取消" };
                            dialog.PrimaryButtonClick+=(_,args)=> {
                                try {
                                    if(!_uiTestMode) {
                                        if(action=="new")store.Add(name.Text);
                                        else if(action=="copy")store.Add(name.Text,selected!.Id);
                                        else store.Rename(selected!.Id,name.Text);
                                    }
                                } catch(Exception failure) { error.Text=failure.Message;args.Cancel=true; }
                            };
                            if(await dialog.ShowAsync()!=ContentDialogResult.Primary)return;
                        }
                        SelectPage("global-profiles");
                        SetStatus(_uiTestMode?"测试模式：未修改真实配置":"总配置已更新。",false);
                    } catch(Exception error) { status.Text="未完成："+error.Message;status.Foreground=Brush(239,139,139); }
                };
                buttons.Children.Add(button);
            }
            body.Children.Add(buttons);
        }
        PageContent.Children.Add(Card("功能配置方案","选择一套配置即热切换。修改其他设置后，点“保存当前配置”更新方案，或“新建配置”另存。",body));
        PageContent.Children.Add(Info("保存范围：日语/中文功能开关、翻译与学习模式、模糊规则启用状态、按键行为、云候选、误选保护、快捷键、外观及日语按键表的启用状态。\n不复制双拼表定义、词典、模型、学习词库或 API 密钥；删除总配置不会删除这些数据。"));
        AutoSaveStatus("功能设置仍自动保存；这里的“保存”用于给整套状态建立可反复切换的命名快照。");
        picker.SelectionChanged+=async(_,_)=> {
            if(picker.SelectedItem is not GlobalSettingsProfile selected || selected.Id==doc.CurrentId)return;
            picker.IsEnabled=false;
            try {
                FlushPendingAutoSaves();
                var previous=profiles.FirstOrDefault(p=>p.Id==doc.CurrentId);
                if(previous is not null && !store.Matches(previous.State)) {
                    var confirm=new ContentDialog { XamlRoot=Root.XamlRoot,Title="当前方案有修改",
                        Content="是否先保存当前改动到“"+previous.Name+"”，再切换到“"+selected.Name+"”？",
                        PrimaryButtonText="保存后切换",SecondaryButtonText="不保存方案，直接切换",CloseButtonText="取消" };
                    var choice=await confirm.ShowAsync();
                    if(choice==ContentDialogResult.None) { picker.SelectedItem=previous;return; }
                    if(choice==ContentDialogResult.Primary && !_uiTestMode)store.SaveCurrent(previous.Id);
                }
                if(!_uiTestMode)store.Apply(selected.Id);
                SelectPage("global-profiles");SetStatus("已热切换到“"+selected.Name+"”，无需部署或重启。",false);
            } catch(Exception error) { status.Text="切换失败："+error.Message;picker.SelectedItem=profiles.FirstOrDefault(p=>p.Id==doc.CurrentId); }
            finally { picker.IsEnabled=true; }
        };
    }
}
