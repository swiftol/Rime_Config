using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Media;
using Microsoft.Win32;
using RimeSettings;

namespace ZhongriSettingsV2;

public sealed partial class MainWindow
{
    private static IEnumerable<T> AutoSaveControls<T>(DependencyObject root) where T : DependencyObject
    {
        if (root is T value) yield return value;
        for (var i=0;i<VisualTreeHelper.GetChildrenCount(root);i++)
            foreach(var child in AutoSaveControls<T>(VisualTreeHelper.GetChild(root,i))) yield return child;
    }

    internal async Task RunAutoSaveSelfTestAsync()
    {
        void Check(bool condition,string message) { if(!condition) throw new InvalidOperationException(message); }
        void Step(string name)=>File.AppendAllText(Path.Combine(Path.GetTempPath(),"ZhongriSettingsV2-autosave-test.txt"),"\nSTEP "+name);
        void Page(string name) { Step(name); SelectPage(name); Root.UpdateLayout(); }
        ToggleSwitch Toggle(string name) => AutoSaveControls<ToggleSwitch>(PageContent)
            .Single(t=>AutomationProperties.GetName(t)==name);
        void Invoke(string label) {
            var button=AutoSaveControls<Button>(PageContent).First(b=>b.Content is string text && text==label);
            var peer=new Microsoft.UI.Xaml.Automation.Peers.ButtonAutomationPeer(button);
            ((Microsoft.UI.Xaml.Automation.Provider.IInvokeProvider)peer.GetPattern(Microsoft.UI.Xaml.Automation.Peers.PatternInterface.Invoke)).Invoke();
        }
        var deployments=0;
        _deploySettings=async ()=> { deployments++; await Task.Delay(80); };
        Page("input");
        Page("japanese");
        foreach (var state in new[] { true, false, true }) {
            Toggle("学习上屏后加空格").IsOn=state;
            Toggle("学习上屏后自动换行").IsOn=state;
            Check(_store.ReadInputOptions().JapaneseLearningSpace==state && _store.ReadInputOptions().JapaneseLearningNewline==state,"Learning separators did not hot autosave");
            Page("input");Page("japanese");
            Check(Toggle("学习上屏后加空格").IsOn==state && Toggle("学习上屏后自动换行").IsOn==state,"Learning separators lost after page reopen");
        }
        Page("input");
        var enterMode=AutoSaveControls<ComboBox>(PageContent).Single(c=>AutomationProperties.GetAutomationId(c)=="EnterBehaviorMode");
        Check(enterMode.Items.Count==4,"Enter modes incomplete");
        foreach(var mode in new[]{1,2,3,0,1}) {
            enterMode.SelectedIndex=mode;await Task.Delay(900);
            Check(_store.ReadInputOptions().EffectiveEnterMode==mode,"Enter dropdown did not hot autosave");
            using var enterKey=Registry.CurrentUser.OpenSubKey(_store.HotSettingsRegistryPath);
            Check(enterKey?.GetValue("EnterMode") is int savedMode && savedMode==mode,"Enter hot scalar missing");
            Page("cloud");Page("input");
            enterMode=AutoSaveControls<ComboBox>(PageContent).Single(c=>AutomationProperties.GetAutomationId(c)=="EnterBehaviorMode");
            Check(enterMode.SelectedIndex==mode,"Enter mode did not survive reopening page");
        }
        var before=_store.ReadInputOptions();
        Toggle("输入字母显示在应用输入框").IsOn=!before.InlinePreedit;
        Toggle("Emoji 固定第二候选").IsOn=!before.EmojiSecondPosition;
        await Task.Delay(650);
        var input=_store.ReadInputOptions();
        Check(input.InlinePreedit!=before.InlinePreedit && input.EmojiSecondPosition!=before.EmojiSecondPosition,"Input toggles did not autosave");
        var rare=AutoSaveControls<TextBox>(PageContent).Single(t=>AutomationProperties.GetAutomationId(t)=="RareCharacterThreshold");
        rare.Text="4321";
        Page("appearance");
        Check(_store.ReadRareSingleCharThreshold()==4321,"Navigation dropped pending numeric edit");
        _width!.Text="700"; _spacing!.Text="20";
        _colorPickers["japaneseText"].Color=Windows.UI.Color.FromArgb(255,12,34,56);
        await Task.Delay(650);
        Check(_store.ReadAppearance().Width==700 && _store.ReadAppearance().CandidateSpacing==20 && _store.ReadAppearance().JapaneseText.R==12,"Appearance autosave failed");
        _width.Text=""; await Task.Delay(550);
        Check(_store.ReadAppearance().Width==700 && _saveStatus!.Text.StartsWith("未保存"),"Invalid numeric edit persisted or false success shown");
        _width.Text="900"; _width.Text="760";
        Page("japanese");
        Check(_store.ReadAppearance().Width==760,"Rapid changes/navigation did not retain final value");
        var fuzzy=_store.ReadInputOptions();
        Toggle("促音省略").IsOn=!fuzzy.FuzzySokuon;
        await Task.Delay(600);
        Check(_store.ReadInputOptions().FuzzySokuon!=fuzzy.FuzzySokuon,"Japanese fuzzy did not autosave");
        Toggle("允许多条模糊规则同时生效").IsOn=true;
        await Task.Delay(600);
        Check(_store.ReadInputOptions().FuzzyMultiple && File.ReadAllText(Path.Combine(_store.RimeDirectory,"japanese_fuzzy_multiple.flag")).Trim()=="1","Multiple fuzzy did not hot-save");
        Page("appearance");Page("japanese");
        Check(Toggle("允许多条模糊规则同时生效").IsOn,"Multiple fuzzy switch not restored on return");
        Toggle("允许多条模糊规则同时生效").IsOn=false;
        await Task.Delay(600);
        Check(!_store.ReadInputOptions().FuzzyMultiple,"Multiple fuzzy off not saved");
        Page("hotkeys");
        var shortcut=AutoSaveControls<TextBox>(PageContent).First();
        shortcut.Text="Ctrl+Alt+K"; await Task.Delay(900);
        using(var key=Registry.CurrentUser.OpenSubKey(_store.HotSettingsRegistryPath))
            Check(key?.GetValue("ShortcutText_Learning") as string=="Ctrl+Alt+K","Shortcut autosave failed");
        shortcut.Text="Ctrl+"; await Task.Delay(900);
        Check(_saveStatus!.Text.StartsWith("未保存"),"Invalid shortcut reported success");
        using(var key=Registry.CurrentUser.OpenSubKey(_store.HotSettingsRegistryPath))
            Check(key?.GetValue("ShortcutText_Learning") as string=="Ctrl+Alt+K","Incomplete shortcut overwritten");
        File.WriteAllText(_store.JapaneseSchemaYaml,"speller:\n  algebra:\n    ### 自动纠错\n    - derive/ab/ac/\n    - derive/bc/bd/\nmenu:\n");
        Page("chinese"); Toggle("全部中文自动纠错").IsOn=false;
        await Task.Delay(1000); await _automaticDeployment;
        Check(_chineseCorrections.Load().All(r=>!r.Enabled) && deployments==0,"Chinese master toggle must save hot without deployment");
        var custom=new CustomFuzzyRuleStore(_store.RimeDirectory);
        custom.Save(FuzzyLanguage.Japanese,[new(true,"hu","fu")]);
        Page("custom"); AutoSaveControls<ToggleSwitch>(PageContent).Single().IsOn=false;
        Page("cloud"); Check(!custom.Load(FuzzyLanguage.Japanese).Single().Enabled,"Custom fuzzy navigation lost edit");
        Page("custom"); Invoke("编辑"); await Task.Delay(160); Root.UpdateLayout();
        AutoSaveControls<TextBox>(PageContent).Single(t=>t.Header is string label && label=="第一组罗马字").Text="ha";
        Page("cloud"); Check(custom.Load(FuzzyLanguage.Japanese).Single().Left=="ha","Custom rule editor did not autosave on navigation");
        var phraseStore=new PhraseStore(_store.RimeDirectory);
        phraseStore.Save([new("旧短语","testcode",1000000)]);
        Page("phrases"); Invoke("编辑"); await Task.Delay(160); Root.UpdateLayout();
        AutoSaveControls<TextBox>(PageContent).Single(t=>t.Header is string label && label=="内容").Text="新短语";
        Page("cloud"); Check(phraseStore.Load().Single().Content=="新短语","Phrase editor did not autosave on navigation");
        Page("sentence");
        var appId=AutoSaveControls<TextBox>(PageContent).Single(t=>AutomationProperties.GetAutomationId(t)=="NiuAppIdInput");
        appId.Text="isolated-test-app";
        await Task.Delay(900); Check(!_store.HasNiuCredentials(),"Incomplete credentials persisted");
        _credentialKeyBox!.Password="isolated-test-key";
        await Task.Delay(1100); Check(_store.HasNiuCredentials(),"Complete credentials did not autosave encrypted");
        var tableStore=new JapaneseInputTableStore(_store.RimeDirectory,_store.HotSettingsRegistryPath);
        var table=tableStore.CreateEditableRomaji();
        var (body,read)=BuildJapaneseKeyEditor(tableStore,table,"q");
        var errorLabel=new TextBlock(); body.Children.Add(errorLabel);
        var dialog=new ContentDialog { XamlRoot=Root.XamlRoot,Title="隔离自动保存测试",Content=new ScrollViewer { Content=body,MaxHeight=540 },CloseButtonText="关闭" };
        Step("key dialog");
        AutoSaveDialog(dialog,body,()=>{},errorLabel,()=> {
            var snapshot=read();
            return ()=> {System.Threading.Thread.Sleep(350);tableStore.SaveKey(table.Id,"q",snapshot);};
        });
        var pulse=DispatcherQueue.CreateTimer();pulse.Interval=TimeSpan.FromMilliseconds(16);
        var pulseClock=System.Diagnostics.Stopwatch.StartNew();var lastPulse=0d;var longestPulse=0d;
        pulse.Tick+=(_,_)=>{var now=pulseClock.Elapsed.TotalMilliseconds;longestPulse=Math.Max(longestPulse,now-lastPulse);lastPulse=now;};
        var dialogTimer=DispatcherQueue.CreateTimer(); dialogTimer.Interval=TimeSpan.FromMilliseconds(600);
        var stage=0; var savedWhileOpen=false;
        dialogTimer.Tick+=(_,_)=> {
            if(stage==0) body.Children.OfType<TextBox>().Single(t=>AutomationProperties.GetAutomationId(t)=="KeyFirst").Text="ky";
            if(stage==2) {
                savedWhileOpen=tableStore.KeyDefinition(table.Id,"q").First=="ky";
                body.Children.OfType<CheckBox>().Single(t=>AutomationProperties.GetAutomationId(t)=="KeyEmptyFirst").IsChecked=true;
            }
            if(stage==4) {
                savedWhileOpen &= tableStore.KeyDefinition(table.Id,"q").EmptyFirst;
                var tail=body.Children.OfType<TextBox>().Single(t=>AutomationProperties.GetAutomationId(t)=="KeySecond");
                foreach(var text in new[]{"a","ar","aru"})tail.Text=text;
                body.Children.OfType<TextBox>().Single(t=>AutomationProperties.GetAutomationId(t)=="KeyLongPress").Text="abc";
                dialogTimer.Stop(); dialog.Hide();
            }
            stage++;
        };
        dialog.Opened+=(_,_)=>{pulseClock.Restart();pulse.Start();dialogTimer.Start();};
        await dialog.ShowAsync(); dialogTimer.Stop();pulse.Stop();
        Check(savedWhileOpen && tableStore.KeyDefinition(table.Id,"q").LongPress=="abc" && tableStore.KeyDefinition(table.Id,"q").Second=="aru","Key dialog background autosave/rapid final edit/close flush failed");
        Check(longestPulse<250,"UI blocked during 350ms injected background write: "+longestPulse);
        Step("key background write heartbeat max_ms="+longestPulse.ToString("F2")+"; 350ms injected IO; final edit persisted before dialog closes");
        foreach(var name in new[]{"input","appearance","japanese","chinese","custom","hotkeys","sentence","cloud"})
        {
            Page(name);
            Check(!AutoSaveControls<Button>(PageContent).Any(b=>b.Content is string text &&
                (text.StartsWith("保存") || text.StartsWith("应用"))),"Manual save/apply button remains: "+name);
        }
        Page("appearance"); _width!.Text="780";
        FlushPendingAutoSaves();
        Check(_store.ReadAppearance().Width==780,"Close flush lost last edit");
    }
}
