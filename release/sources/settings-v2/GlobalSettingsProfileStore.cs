using System.Text;
using System.Text.Json;
using Microsoft.Win32;
using RimeSettings;
using DrawingColor=System.Drawing.Color;

namespace ZhongriSettingsV2;

internal sealed record GlobalAppearance(int Width,int CommentSize,int Spacing,int Padding,int CandidateSize,int MinHeight,int[] Colors,
    bool CandidateLanguageSplit=false,int CandidateSplitChineseColumns=3,int CandidateSplitJapaneseColumns=3);
internal sealed record GlobalSettingsState(InputOptions Input,bool HideJapaneseTranslations,bool PreferNiu,
    int CloudCount,int CloudLanguage,int RareThreshold,bool SelectionGuard,int SelectionGuardMs,
    bool TableEnabled,bool TableMixed,bool TableLetters,GlobalAppearance Appearance,
    Dictionary<string,bool> Chinese,Dictionary<string,bool> CustomChinese,Dictionary<string,bool> CustomJapanese,
    Dictionary<string,string> ShortcutText,Dictionary<string,int> ShortcutCode,bool VowelTailOverlap=false,bool SingleKeyTail=false,bool ReservedWhole=false);
internal sealed record GlobalSettingsProfile(string Id,string Name,GlobalSettingsState State,DateTimeOffset Updated,bool Deleted=false);
internal sealed record GlobalSettingsDocument(int Version,string? CurrentId,List<GlobalSettingsProfile> Profiles);

// Explicit typed allowlist: no registry dumps, credentials, learned databases,
// model copies, keyboard definitions or user dictionary contents in profiles.
internal sealed class GlobalSettingsProfileStore(SettingsStore settings)
{
    private static readonly JsonSerializerOptions Json=new() { WriteIndented=true };
    internal string FilePath=>Path.Combine(settings.RimeDirectory,"global-settings-profiles.json");
    private readonly string[] _shortcuts=["Learning","NextProfile","PreviousProfile"];
    private JapaneseInputTableStore Table()=>new(settings.RimeDirectory,settings.HotSettingsRegistryPath);
    internal GlobalSettingsState Capture() {
        var a=settings.ReadAppearance();
        var colors=new[]{a.ChineseText,a.ChineseBackground,a.JapaneseText,a.JapaneseBackground,a.CommonPhraseText,a.CommonPhraseBackground,a.FuzzyCorrectionText,
            a.SelectedText??DrawingColor.White,a.SelectedBackground??DrawingColor.FromArgb(255,20,184,196),a.AnnotationText??DrawingColor.LightGray,a.PanelBackground??DrawingColor.FromArgb(255,64,64,64),a.ReservedText??DrawingColor.FromArgb(255,74,222,128)};
        var table=Table();var custom=new CustomFuzzyRuleStore(settings.RimeDirectory);
        var chinese=File.Exists(settings.JapaneseSchemaYaml)?new ChineseCorrectionStore(settings.RimeDirectory).Load().ToDictionary(r=>r.Expression,r=>r.Enabled):new Dictionary<string,bool>();
        Dictionary<string,bool> Rules(FuzzyLanguage language)=>custom.Load(language).ToDictionary(r=>r.Left+"\t"+r.Right,r=>r.Enabled);
        var text=new Dictionary<string,string>();var codes=new Dictionary<string,int>();
        using(var key=Registry.CurrentUser.OpenSubKey(settings.HotSettingsRegistryPath))
            foreach(var name in _shortcuts) {
                text[name]=key?.GetValue("ShortcutText_"+name) as string??(name=="Learning"?"Ctrl+Alt+L":name=="NextProfile"?"Ctrl+Alt+PageDown":"Ctrl+Alt+PageUp");
                codes[name]=key?.GetValue("Shortcut_"+name) is int code?code:(12<<16)|(name=="Learning"?'L':name=="NextProfile"?0xff56:0xff55);
            }
        return new(settings.ReadInputOptions(),settings.ReadHideJapaneseTranslationAnnotations(),settings.ReadPreferNiuAnnotations(),
            settings.ReadCloudCandidateCount(),settings.ReadCloudLanguageMode(),settings.ReadRareSingleCharThreshold(),
            settings.ReadCandidateSelectionGuardEnabled(),settings.ReadCandidateSelectionGuardMs(),table.Enabled,table.Mixed,table.ShowLetters,
            new(a.Width,a.CommentSize,a.CandidateSpacing,a.HighlightPadding,a.CandidateSize,a.MinHeight,colors.Select(c=>c.ToArgb()).ToArray(),a.CandidateLanguageSplit,a.CandidateSplitChineseColumns,a.CandidateSplitJapaneseColumns),
            chinese,Rules(FuzzyLanguage.Chinese),Rules(FuzzyLanguage.Japanese),text,codes,table.VowelTailOverlap,table.SingleKeyTail,table.ReservedWhole);
    }
    internal GlobalSettingsDocument Read() {
        if(!File.Exists(FilePath)) return new(1,null,[]);
        if(new FileInfo(FilePath).Length>8*1024*1024) throw new InvalidDataException("总配置文件过大，已停止读取，原文件未改动。");
        var doc=JsonSerializer.Deserialize<GlobalSettingsDocument>(File.ReadAllText(FilePath),Json)??throw new InvalidDataException("总配置文件为空。");
        if(doc.Version!=1 || doc.Profiles is null || doc.Profiles.Count>256 || doc.Profiles.Any(p=>!Guid.TryParseExact(p.Id,"N",out _) || p.State is null) || doc.Profiles.Select(p=>p.Id).Distinct().Count()!=doc.Profiles.Count)
            throw new InvalidDataException("总配置格式不受支持或已损坏；不会覆盖原文件。");
        return doc;
    }
    private T Change<T>(Func<GlobalSettingsDocument,(GlobalSettingsDocument,T)> edit) {
        Directory.CreateDirectory(settings.RimeDirectory);
        using var lease=new FileStream(FilePath+".lock",FileMode.OpenOrCreate,FileAccess.ReadWrite,FileShare.None);
        var (doc,result)=edit(Read());
        var text=JsonSerializer.Serialize(doc,Json);
        if(Encoding.UTF8.GetByteCount(text)>8*1024*1024) throw new InvalidOperationException("总配置超过容量限制；没有覆盖原文件。");
        var pending=FilePath+".new";
        using(var file=new FileStream(pending,FileMode.Create,FileAccess.Write,FileShare.None)) { var bytes=Encoding.UTF8.GetBytes(text);file.Write(bytes);file.Flush(true); }
        File.Move(pending,FilePath,true);
        return result;
    }
    private static string Name(string name) {
        name=(name??"").Trim().Normalize();
        if(name.Length is <1 or >64 || name.Any(char.IsControl)) throw new ArgumentException("名称须为 1～64 个字符，不能包含控制字符。");
        return name;
    }
    private static void Unique(GlobalSettingsDocument doc,string name,string? except=null) {
        if(doc.Profiles.Any(p=>!p.Deleted && p.Id!=except && string.Equals(p.Name,name,StringComparison.OrdinalIgnoreCase))) throw new ArgumentException("已有同名配置，请换一个名称。");
    }
    internal GlobalSettingsProfile Add(string name,string? copyId=null) {
        name=Name(name);var snapshot=copyId is null?Capture():null;
        return Change(doc=> {
            Unique(doc,name);if(doc.Profiles.Count>=256) throw new InvalidOperationException("最多保留 256 套配置（含回收记录）。");
            var state=copyId is null?snapshot!:doc.Profiles.Single(p=>p.Id==copyId && !p.Deleted).State;
            var item=new GlobalSettingsProfile(Guid.NewGuid().ToString("N"),name,state,DateTimeOffset.Now);
            return (doc with { Profiles=[..doc.Profiles,item],CurrentId=copyId is null?item.Id:doc.CurrentId },item);
        });
    }
    internal void Rename(string id,string name) {
        name=Name(name);Change(doc=> { Unique(doc,name,id);var item=doc.Profiles.Single(p=>p.Id==id && !p.Deleted);return (doc with { Profiles=doc.Profiles.Select(p=>p.Id==id?item with { Name=name,Updated=DateTimeOffset.Now }:p).ToList() },true); });
    }
    internal void SaveCurrent(string id) {
        var state=Capture();Change(doc=> { var item=doc.Profiles.Single(p=>p.Id==id && !p.Deleted);return (doc with { CurrentId=id,Profiles=doc.Profiles.Select(p=>p.Id==id?item with { State=state,Updated=DateTimeOffset.Now }:p).ToList() },true); });
    }
    internal void Delete(string id) {
        Change(doc=> { var item=doc.Profiles.Single(p=>p.Id==id && !p.Deleted);return (doc with { CurrentId=doc.CurrentId==id?null:doc.CurrentId,Profiles=doc.Profiles.Select(p=>p.Id==id?item with { Deleted=true }:p).ToList() },true); });
    }
    internal bool Matches(GlobalSettingsState state)=>JsonSerializer.Serialize(Capture())==JsonSerializer.Serialize(state);
    internal void Apply(string id) {
        GlobalSettingsState? before=null;
        try { Change(doc=> {
            var target=doc.Profiles.Single(p=>p.Id==id && !p.Deleted).State;
            Validate(target);before=Capture();ApplyState(target);
            return (doc with { CurrentId=id },true);
        }); } catch(Exception failure) {
            if(before is null) throw;
            try { ApplyState(before); } catch(Exception rollback) { throw new AggregateException("切换失败，恢复也失败，请保留当前配置文件以便修复。",failure,rollback); }
            throw new InvalidOperationException("切换失败，已恢复原设置。",failure);
        }
    }
    private void Validate(GlobalSettingsState s) {
        if(s.Appearance is { } split && (split.CandidateSplitChineseColumns<1||split.CandidateSplitJapaneseColumns<1||split.CandidateSplitChineseColumns+split.CandidateSplitJapaneseColumns>9))throw new InvalidDataException("分栏列数不合法；未修改当前设置。");
        if(s.Input is not null && s.Input.EnterMode is < -1 or > 3) throw new InvalidDataException("回车模式不合法；未修改当前设置。");
        if(s.Input is null || s.Appearance?.Colors?.Length is not (11 or 12) || s.CloudCount is <1 or >10 || s.CloudLanguage is <0 or >3 || s.RareThreshold is <0 or >50000 || s.SelectionGuardMs is <50 or >1000 || s.Appearance.Width is <120 or >4000 || s.Appearance.CandidateSize is <8 or >80 || s.Appearance.CommentSize is <6 or >40)
            throw new InvalidDataException("配置数值不合法；未修改当前设置。");
        if(s.TableEnabled && Table().Read() is null) throw new InvalidOperationException("当前没有可用日语按键表；请先导入按键表，再切换这套配置。");
        if(_shortcuts.Any(n=>!s.ShortcutText.ContainsKey(n) || !s.ShortcutCode.ContainsKey(n))) throw new InvalidDataException("配置缺少快捷键字段。");
    }
    private void ApplyState(GlobalSettingsState s) {
        settings.SaveInputOptions(s.Input);
        settings.SaveHideJapaneseTranslationAnnotations(s.HideJapaneseTranslations);settings.SavePreferNiuAnnotations(s.PreferNiu);
        settings.SaveCloudCandidateCount(s.CloudCount);settings.SaveCloudLanguageMode(s.CloudLanguage);
        settings.SaveRareSingleCharThreshold(s.RareThreshold);settings.SaveCandidateSelectionGuard(s.SelectionGuard,s.SelectionGuardMs);
        var a=s.Appearance;var c=a.Colors.Select(DrawingColor.FromArgb).ToArray();
        settings.SaveAppearanceHot(new(a.Width,a.CommentSize,a.Spacing,a.Padding,c[0],c[1],c[2],c[3],c[4],c[5],c[6],a.CandidateSize,c[7],c[8],c[9],c[10],a.MinHeight,c.Length>11?c[11]:DrawingColor.FromArgb(255,74,222,128),a.CandidateLanguageSplit,a.CandidateSplitChineseColumns,a.CandidateSplitJapaneseColumns));
        var table=Table();table.SetVowelTailOverlap(s.VowelTailOverlap);table.SetSingleKeyTail(s.SingleKeyTail);table.SetReservedWhole(s.ReservedWhole);table.SetMixed(s.TableMixed);table.SetShowLetters(s.TableLetters);table.SetEnabled(s.TableEnabled);
        if(File.Exists(settings.JapaneseSchemaYaml)) {
            var store=new ChineseCorrectionStore(settings.RimeDirectory);
            store.Save(store.Load().Select(r=>s.Chinese.TryGetValue(r.Expression,out var on)?r with { Enabled=on }:r).ToList());
        }
        var custom=new CustomFuzzyRuleStore(settings.RimeDirectory);
        foreach(var language in new[]{FuzzyLanguage.Chinese,FuzzyLanguage.Japanese}) {
            var flags=language==FuzzyLanguage.Chinese?s.CustomChinese:s.CustomJapanese;
            custom.Save(language,custom.Load(language).Select(r=>flags.TryGetValue(r.Left+"\t"+r.Right,out var on)?r with { Enabled=on }:r));
        }
        using(var key=Registry.CurrentUser.CreateSubKey(settings.HotSettingsRegistryPath)) foreach(var name in _shortcuts) {
            key.SetValue("ShortcutText_"+name,s.ShortcutText[name]);key.SetValue("Shortcut_"+name,s.ShortcutCode[name],RegistryValueKind.DWord);
        }
        settings.PublishJapaneseFuzzyReload();settings.PublishChineseFuzzyReload();
    }
}
