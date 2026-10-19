using Microsoft.UI.Xaml;
using Microsoft.Win32;
using Microsoft.UI.Xaml.Controls;

namespace ZhongriSettingsV2;

public sealed partial class MainWindow
{
    internal async Task RunReservedMultiSearchSelfTestAsync(string screenshot)
    {
        using(var key=Registry.CurrentUser.CreateSubKey(_store.HotSettingsRegistryPath)) key.SetValue("ZhongriRoot",@"H:\ZhongriInputMethod");
        var store=new JapaneseInputTableStore(_store.RimeDirectory,_store.HotSettingsRegistryPath,@"H:\ZhongriInputMethod\data\japanese_input_tables");
        var profile=store.Profiles().First(p=>p.Name.Contains("AZIK") && store.SupportsReserved(p.Id));
        store.SelectProfile(profile.Id);
        var first=store.ReservedCatalog(profile.Id).Single(p=>p.Code=="sms" && p.Text=="します");
        store.SetReservedSelection(profile.Id,code:first.Key,selected:true);
        store.UpsertReservedPhrase(profile.Id,"sms","すみません","测试多读音");
        SelectPage("japanese-table");Root.UpdateLayout();
        if(_tableReservedWholeSwitch is null || _tableReservedWholeSwitch.IsOn)throw new InvalidOperationException("Missing/default reserved whole mode control");
        var beforeWhole=File.ReadAllText(store.TablePath);
        _tableReservedWholeSwitch.IsOn=true;
        if(!store.ReservedWhole || !File.ReadAllText(store.TablePath).Contains("# zhongri-reserved-whole: 1"))throw new InvalidOperationException("Reserved whole mode not hot-saved");
        _tableReservedWholeSwitch.IsOn=false;
        if(store.ReservedWhole || File.ReadAllText(store.TablePath)!=beforeWhole)throw new InvalidOperationException("Legacy mode did not restore exact compiled rules");
        _tableReservedWholeSwitch.IsOn=true;
        _tableReservedWholeSwitch.StartBringIntoView(new BringIntoViewOptions { VerticalAlignmentRatio=0.35 });
        await Task.Delay(250);Root.UpdateLayout();
        var wholeY=_tableReservedWholeSwitch.TransformToVisual(Root).TransformPoint(new Windows.Foundation.Point()).Y;
        if(wholeY<0 || wholeY>Root.ActualHeight-80)throw new InvalidOperationException("Whole conversion switch not visible in rendered evidence");
        await RenderAppearanceElementAsync(Root,Path.Combine(Path.GetDirectoryName(screenshot)!,"reserved-whole-switch.png"));
        if(_tableSingleTailSwitch is null || _tableSingleTailSwitch.IsOn)throw new InvalidOperationException("Missing/default single-key mode control");
        _tableSingleTailSwitch.IsOn=true;
        if(!store.SingleKeyTail || !File.ReadAllText(store.TablePath).Contains("# zhongri-single-key-tail: 1"))throw new InvalidOperationException("Single-key mode not hot-saved");
        if(_reservedSearchInput is null || _reservedSearchRun is null || _reservedSearchResults is null)throw new InvalidOperationException("Missing reserved search controls");
        _reservedSearchInput.Text="sms";await _reservedSearchRun();Root.UpdateLayout();
        List<string> SearchDisplays() => _reservedSearchResults.Items.Cast<FrameworkElement>().Select(e=>((JapaneseCodeMatch)e.Tag).Display).ToList();
        var results=SearchDisplays();
        if(!results.Any(x=>x.Contains("します")) || !results.Any(x=>x.Contains("すみません")) || !results.Any(x=>x.Contains("虚拟解析")))throw new InvalidOperationException("Search did not display duplicate and virtual routes");
        _reservedChecks["sms"].IsChecked=false;
        var selection=store.ReservedSelection(profile.Id);
        if(store.IsReservedSelected(selection,first) || !store.IsReservedSelected(selection,store.ReservedCatalog(profile.Id).Single(p=>p.Code=="sms" && p.Text=="すみません")))throw new InvalidOperationException("Checkbox changed another same-code entry");
        await _reservedSearchRun();Root.UpdateLayout();
        if(_reservedSearchStatus!.Text.Contains("查询中") || !SearchDisplays().Any(x=>x.StartsWith("保留字 · sms → します") && x.Contains("未启用")))throw new InvalidOperationException("Search returned stale enabled state or remained busy");
        _reservedSearchEnabledFilter!.SelectedIndex=2;
        await _reservedSearchRun();
        if(SearchDisplays().Count==0 || SearchDisplays().Any(x=>!x.Contains("未启用")))throw new InvalidOperationException("Disabled search filter failed");
        _reservedSearchGroupFilter!.SelectedItem=first.Group;
        await _reservedSearchRun();
        var firstCheck=_reservedSearchResults.Items.OfType<CheckBox>().Single(c=>((JapaneseCodeMatch)c.Tag).ReservedKey==first.Key);
        firstCheck.IsChecked=true;
        await _reservedSearchRun!();
        if(!store.IsReservedSelected(store.ReservedSelection(profile.Id),first) || _reservedSearchResults.Items.OfType<CheckBox>().Any(c=>((JapaneseCodeMatch)c.Tag).ReservedKey==first.Key))throw new InvalidOperationException("Result checkbox did not save/re-filter");
        _reservedSearchEnabledFilter!.SelectedIndex=1;
        await _reservedSearchRun();
        var enabledMatches=_reservedSearchResults.Items.Cast<FrameworkElement>().Select(e=>(JapaneseCodeMatch)e.Tag).ToList();
        if(enabledMatches.Count==0 || enabledMatches.Any(m=>!m.Enabled || m.Group!=first.Group || m.ReservedKey is null))throw new InvalidOperationException("Enabled/group intersection failed");
        _reservedSearchResults.Items.OfType<CheckBox>().Single(c=>((JapaneseCodeMatch)c.Tag).ReservedKey==first.Key).IsChecked=false;
        await _reservedSearchRun!();
        if(store.IsReservedSelected(store.ReservedSelection(profile.Id),first) || !store.IsReservedSelected(store.ReservedSelection(profile.Id),store.ReservedCatalog(profile.Id).Single(p=>p.Code=="sms" && p.Text=="すみません")))throw new InvalidOperationException("Search toggle changed duplicate sibling");
        _reservedSearchEnabledFilter!.SelectedIndex=0;
        _reservedSearchGroupFilter!.SelectedIndex=0;
        _reservedSearchInput!.Text="";await _reservedSearchRun!();
        if(_reservedSearchResults.Items.Count<store.ReservedCatalog(profile.Id).Count)throw new InvalidOperationException("Empty query must allow browsing all reserved entries");
        _reservedSearchInput.Text="sms";await _reservedSearchRun();
        _reservedSearchInput.StartBringIntoView(new BringIntoViewOptions { VerticalAlignmentRatio=0 });
        await Task.Delay(200);Root.UpdateLayout();
        var searchY=_reservedSearchInput.TransformToVisual(Root).TransformPoint(new Windows.Foundation.Point()).Y;
        if(searchY<0 || searchY>Root.ActualHeight/2)throw new InvalidOperationException("Search controls not visible in rendered evidence");
        await RenderAppearanceElementAsync(Root,screenshot);
        SelectPage("appearance");Root.UpdateLayout();
        if(!_colorPickers.ContainsKey("reservedText"))throw new InvalidOperationException("Missing reserved segment color picker");
        _colorPickers["reservedText"].Color=Windows.UI.Color.FromArgb(255,30,210,100);
        FlushAutoSaves();
        if(_store.ReadAppearance().ReservedText?.G!=210)throw new InvalidOperationException("Reserved segment color did not autosave");
        var profiles=new GlobalSettingsProfileStore(_store);var saved=profiles.Add("保留字颜色");
        store.SetSingleKeyTail(false);
        store.SetReservedWhole(false);
        _store.SaveAppearanceHot(_store.ReadAppearance() with {ReservedText=System.Drawing.Color.Red});
        profiles.Apply(saved.Id);
        if(_store.ReadAppearance().ReservedText?.G!=210)throw new InvalidOperationException("Named profile did not restore reserved segment color");
        if(!store.SingleKeyTail)throw new InvalidOperationException("Named profile did not restore single-key mode");
        if(!store.ReservedWhole)throw new InvalidOperationException("Named profile did not restore reserved whole mode");
        SelectPage("input");Root.UpdateLayout();
        if(_candidateSplitSwitch is null||_candidateSplitLeft is null||_candidateSplitRight is null)throw new InvalidOperationException("Missing language split controls");
        if(_candidateSplitPreset is null)throw new InvalidOperationException("Missing split preset picker");
        _candidateSplitSwitch.IsOn=true;_candidateSplitPreset.SelectedIndex=1;FlushAutoSaves();
        var split=_store.ReadAppearance();
        if(!split.CandidateLanguageSplit||split.CandidateSplitChineseColumns!=4||split.CandidateSplitJapaneseColumns!=2)throw new InvalidOperationException("Split controls did not save live");
        var splitSaved=profiles.Add("候选分栏");
        _store.SaveAppearanceHot(split with { CandidateLanguageSplit=false,CandidateSplitChineseColumns=3,CandidateSplitJapaneseColumns=3 });
        profiles.Apply(splitSaved.Id);
        if(_store.ReadAppearance()!=split)throw new InvalidOperationException("Named profile did not restore split layout");
        bool refused=false;try {_store.SaveAppearanceHot(split with {CandidateSplitChineseColumns=8,CandidateSplitJapaneseColumns=8});}catch(ArgumentException){refused=true;}
        if(!refused||_store.ReadAppearance()!=split)throw new InvalidOperationException("Invalid split partially persisted");
        _store.SaveAppearanceHot(split with {CandidateSplitChineseColumns=3,CandidateSplitJapaneseColumns=3});
        SelectPage("input");Root.UpdateLayout();
        await RenderAppearanceElementAsync(Root,System.IO.Path.Combine(System.IO.Path.GetDirectoryName(screenshot)!,"candidate-language-split-settings.png"));
    }
}
