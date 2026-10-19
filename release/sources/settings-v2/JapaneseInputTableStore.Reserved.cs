using System.Text.Json;
using System.Text.RegularExpressions;

namespace ZhongriSettingsV2;

internal sealed record JapaneseReservedPhrase(string Code, string Text, string Group, bool DefaultEnabled, string? Id = null)
{
    internal string Key => Id ?? Code + "\u001f" + Text;
}
internal sealed record JapaneseReservedSelection(bool Enabled, Dictionary<string, bool> Items,
    List<JapaneseReservedPhrase>? Custom = null, HashSet<string>? Deleted = null, List<string>? Groups = null,
    Dictionary<string, bool>? Expanded = null, int Version = 1);

internal sealed partial class JapaneseInputTableStore
{
    internal static readonly JapaneseReservedPhrase[] ReservedPhrases =
        ("kt:こと st:した tt:たち ht:ひと wt:わた mn:もの ms:ます ds:です " +
         "km:かも tm:ため dm:でも kr:から sr:する tr:たら nr:なる yr:よる " +
         "rr:られ zr:ざる mt:また tb:たび nb:ねば bt:びと gr:がら gt:ごと " +
         "nt:にち dt:だち wr:われ").Split(' ').Select(item =>
        {
            var pair = item.Split(':');
            return new JapaneseReservedPhrase(pair[0], pair[1], "AZIK 原有保留字", true);
        }).Concat(new[]
        {
            new JapaneseReservedPhrase("mst", "ました", "常用词尾", false),
            new JapaneseReservedPhrase("msn", "ません", "常用词尾", false),
            new JapaneseReservedPhrase("msnd", "ませんでした", "常用词尾", false),
            new JapaneseReservedPhrase("dst", "でした", "常用词尾", false),
            new JapaneseReservedPhrase("srr", "される", "常用语法", false),
            new JapaneseReservedPhrase("tir", "ている", "常用语法", false),
            new JapaneseReservedPhrase("tim", "ています", "常用语法", false),
            new JapaneseReservedPhrase("ddn", "だけでなく", "常用语法", false),
            new JapaneseReservedPhrase("ndn", "ので", "常用语法", false),
            new JapaneseReservedPhrase("smms", "すみません", "日常表达", false),
            new JapaneseReservedPhrase("arg", "ありがとうございます", "日常表达", false),
            new JapaneseReservedPhrase("yrsk", "よろしくおねがいします", "日常表达", false),
        }).Concat(AdditionalReservedPhrases()).ToArray();

    // These are literal kana shortcuts, not an automatic conjugation engine.
    // Keep existing codes/groups unchanged and opt in to every new shortcut.
    private static IEnumerable<JapaneseReservedPhrase> AdditionalReservedPhrases()
    {
        var groups = new (string Group, string Entries)[]
        {
            ("常用词尾", "msy:ましょう msyk:ましょうか msk:ますか msnk:ませんか " +
                "dsk:ですか dsn:ですね dsy:ですよ djni:ではない djnkt:ではなかった " +
                "djarm:ではありません djarmd:ではありませんでした jni:じゃない jnkt:じゃなかった " +
                "ndar:のである nds:のです ndes:んです ndesh:んでした"),
            ("普通形・过去・否定", "tta:た tte:て dda:だ dde:で ttt:った ttd:って " +
                "nda:んだ nde:んで ita:いた ite:いて ida:いだ ide:いで " +
                "nni:ない nnkt:なかった nnku:なく nnkte:なくて nnd:ないで znni:ずに " +
                "nnkds:ないです nnktds:なかったです"),
            ("形容词变化", "ktta:かった kni:くない knkt:くなかった kute:くて " +
                "keba:ければ knkds:くないです knktds:くなかったです " +
                "knaru:くなる knari:くなります ksuru:くする"),
            ("する・くる变化", "sht:して sni:しない snkt:しなかった snku:しなく snkte:しなくて " +
                "snd:しないで sms:します smst:しました smsn:しません smsnd:しませんでした " +
                "syu:しよう sreb:すれば kuru:くる kita:きた kite:きて knai:こない " +
                "konkt:こなかった kims:きます kimst:きました kimsn:きません kimsnd:きませんでした " +
                "koyu:こよう kureb:くれば"),
            ("て形・进行状态", "tita:ていた titm:ていました tin:ていない tinm:ていません " +
                "tinkt:ていなかった timnd:ていませんでした trru:ているところ ttru:ていたところ " +
                "diru:でいる dita:でいた dim:でいます dimst:でいました din:でいない dimn:でいません"),
            ("被动・使役", "rru:られる rrta:られた rrte:られて rrni:られない rrnkt:られなかった " +
                "rrms:られます rrmst:られました rrmsn:られません rrmsnd:られませんでした " +
                "reru:れる reta:れた rete:れて reni:れない renkt:れなかった " +
                "rems:れます remst:れました remsn:れません remsnd:れませんでした " +
                "srta:された srte:されて srni:されない srnkt:されなかった " +
                "srms:されます srmst:されました srmsn:されません srmsnd:されませんでした " +
                "ssru:させる ssta:させた sste:させて ssni:させない ssnkt:させなかった " +
                "ssms:させます ssmst:させました ssmsn:させません ssmsnd:させませんでした " +
                "ssrr:させられる ssrrt:させられた ssrrte:させられて ssrrn:させられない " +
                "ssrrnkt:させられなかった ssrrm:させられます ssrrmst:させられました " +
                "ssrrmsn:させられません ssrrmsnd:させられませんでした " +
                "seru:せる seta:せた sete:せて seni:せない senkt:せなかった " +
                "sems:せます semst:せました semsn:せません semsnd:せませんでした"),
            ("可能・意向・愿望", "dkru:できる dkta:できた dkte:できて dkni:できない dknkt:できなかった " +
                "dkms:できます dkmst:できました dkmsn:できません dkmsnd:できませんでした " +
                "yuu:よう youm:ようとおもう youms:ようとおもいます " +
                "tai:たい takta:たかった takni:たくない taknkt:たくなかった " +
                "takute:たくて taids:たいです taknids:たくないです"),
            ("请求・许可・义务", "tkds:てください dkds:でください" +
                " nndk:ないでください tmd:てもいい tmds:てもいいです " +
                "tmdk:てもいいですか dmds:でもいいです tiken:てはいけない " +
                "tikmsn:てはいけません nnktr:なくてはいけない nnkm:なければならない " +
                "nnkms:なければなりません nnktem:なくてもいい tkr:てくれる " +
                "tkrem:てくれますか tmr:てもらう tmrms:てもらいます titdk:ていただく " +
                "titdke:ていただけますか tagen:てあげる"),
            ("补助动词・完成・尝试", "toku:ておく tokim:ておきます toita:ておいた " +
                "taru:てある tarm:てあります tmir:てみる tmim:てみます tmita:てみた " +
                "tsmu:てしまう tsmta:てしまった tsmim:てしまいます " +
                "tiku:ていく tikm:ていきます tkuru:てくる tkim:てきます tkita:てきた " +
                "tyru:てやる hazim:はじめる owar:おわる tuduk:つづける"),
            ("条件・假设・让步", "nreb:ならば nara:なら ttara:ったら ndara:んだら " +
                "itara:いたら idara:いだら nnb:なければ nntara:なかったら " +
                "ttemo:っても ndemo:んでも itemo:いても idemo:いでも " +
                "temo:ても demo:でも tosite:としても nisitem:にしても"),
            ("判断・推测・说明", "dsyu:でしょう daru:だろう kmos:かもしれない " +
                "kmoms:かもしれません hazu:はずだ hazuds:はずです yuda:ようだ yuds:ようです " +
                "suda:そうだ suds:そうです rasii:らしい rashds:らしいです " +
                "mitai:みたい mitds:みたいです tsum:つもり tsumds:つもりです " +
                "wake:わけだ wakeds:わけです toiuk:ということ toiui:といういみ"),
            ("常用接续・语法", "dakara:だから dakdo:だけど dakred:だけれど dkrdm:だけれども " +
                "tokoro:ところ tokini:ときに tameni:ために nond:のに nkara:ながら " +
                "nitu:について nitum:についても niyor:による niyot:によって " +
                "nitai:にたいして nitot:にとって nisur:にする ninar:になる " +
                "ktga:ことが ktdek:ことができる ktaru:ことがある ktnai:ことがない " +
                "ktni:ことにする ktna:ことになる yuni:ように yunis:ようにする " +
                "yunin:ようになる kotoni:ことに bakari:ばかり dake:だけ made:まで " +
                "hodo:ほど kurai:くらい gurai:ぐらい nado:など tatoe:たとえば " +
                "sos:そして sorede:それで soredem:それでも sikasi:しかし"),
        };
        foreach (var (group, entries) in groups)
            foreach (var entry in entries.Split(' ', StringSplitOptions.RemoveEmptyEntries))
            {
                var pair = entry.Split(':');
                yield return new(pair[0], pair[1], group, false);
            }
    }

    internal bool IsAzik(string? name) => name?.Contains("AZIK", StringComparison.OrdinalIgnoreCase) == true;
    internal bool SupportsReserved(string id) { var profile = KeyProfile(id); return profile.SupportsReserved ?? IsAzik(profile.Name); }
    private string ReservedPath(string id) => Path.Combine(ProfilesDirectory, id + ".reserved.json");
    internal JapaneseReservedSelection ReservedSelection(string id) => MigrateReserved(File.Exists(ReservedPath(id))
        ? JsonSerializer.Deserialize<JapaneseReservedSelection>(File.ReadAllText(ReservedPath(id)))!
        : new(true, new()));
    private static JapaneseReservedSelection MigrateReserved(JapaneseReservedSelection selection)
    {
        if (selection.Version >= 2) return selection;
        var custom = selection.Custom ?? [];
        var all = ReservedPhrases.Concat(custom).ToList();
        var deleted = all.Where(p => selection.Deleted?.Contains(p.Code) == true).Select(p => p.Key).ToHashSet();
        // In v1 a custom entry replaced the built-in sharing its code. Keep
        // that replacement on migration; new v2 entries are independently keyed.
        foreach (var item in custom)
            foreach (var original in ReservedPhrases.Where(p => p.Code == item.Code && p.Key != item.Key)) deleted.Add(original.Key);
        var items = new Dictionary<string, bool>(selection.Items);
        foreach (var phrase in all)
            if (selection.Items.TryGetValue(phrase.Code, out var chosen)) items[phrase.Key] = chosen;
        foreach (var code in all.Select(p => p.Code).Distinct()) items.Remove(code);
        return selection with { Items = items, Deleted = deleted, Version = 2 };
    }
    internal bool IsReservedSelected(JapaneseReservedSelection selection, JapaneseReservedPhrase phrase) =>
        selection.Items.TryGetValue(phrase.Key, out var value) ? value : phrase.DefaultEnabled;

    internal List<JapaneseReservedPhrase> ReservedCatalog(string id)
    {
        var selection = ReservedSelection(id);
        return ReservedPhrases.Concat(selection.Custom ?? []).GroupBy(phrase => phrase.Key)
            .Select(group => group.Last()).Where(phrase => selection.Deleted?.Contains(phrase.Key) != true).ToList();
    }
    internal List<string> ReservedGroups(string id) => (ReservedSelection(id).Groups ?? ReservedPhrases.Select(p => p.Group).Distinct().ToList())
        .Concat(ReservedCatalog(id).Select(phrase => phrase.Group)).Distinct().ToList();

    private void SaveReserved(string id, JapaneseReservedSelection selection, bool refresh = true)
    {
        var profile = Profiles().Single(p => p.Id == id);
        if (!(profile.SupportsReserved ?? IsAzik(profile.Name))) throw new ArgumentException("保留字仅适用于 AZIK 及其复制配置。");
        var path = ReservedPath(id);
        if (File.Exists(path)) File.Copy(path, path + ".bak", true);
        WriteAtomic(path, JsonSerializer.Serialize(selection));
        if (refresh && Read()?.Id == id) SelectProfile(id, Enabled);
    }
    internal void SetReservedGroupExpanded(string id, string group, bool expanded)
    {
        var selection = ReservedSelection(id);
        var state = selection.Expanded is null ? new Dictionary<string, bool>() : new(selection.Expanded);
        state[group] = expanded;
        SaveReserved(id, selection with { Expanded = state }, false);
    }

    internal void UpsertReservedPhrase(string id, string code, string text, string group, string? previousCode = null, string? previousText = null, string? previousKey = null)
    {
        code = code.Trim().ToLowerInvariant(); text = text.Trim(); group = group.Trim();
        if (!Regex.IsMatch(code, @"^[a-z][a-z0-9]{0,31}$")) throw new ArgumentException("短码应以字母开头，使用 1～32 个小写字母或数字。");
        if (!Regex.IsMatch(text, @"^[\u3041-\u3096\u30a1-\u30fa\u30fc]{1,128}$")) throw new ArgumentException("请输入假名读音（可含长音），汉字由输入法转换；最多 128 个字。");
        if (group.Length is < 1 or > 40 || group.Any(char.IsControl)) throw new ArgumentException("分类名应为 1～40 个字，不含控制字符。");
        var selection = ReservedSelection(id);
        var custom = selection.Custom?.ToList() ?? [];
        var deleted = selection.Deleted is null ? new HashSet<string>() : new(selection.Deleted);
        var catalog = ReservedCatalog(id);
        var previous = previousKey is not null ? catalog.Single(p=>p.Key==previousKey) : previousCode is null ? null : catalog.Single(p => p.Code == previousCode && (previousText is null || p.Text == previousText));
        var item = new JapaneseReservedPhrase(code, text, group, false,previous?.Key??Guid.NewGuid().ToString("N"));
        if (catalog.Any(p => p.Code == code && p.Text == text && p.Group == group && p.Key != previous?.Key))
            throw new ArgumentException("完全相同的条目已存在（短码、读音、分类均相同）。短码可以重复，同读音也可放在不同分类。");
        var chosen = previous is null || IsReservedSelected(selection, previous);
        if (previous is not null) { custom.RemoveAll(p => p.Key == previous.Key); deleted.Add(previous.Key); selection.Items.Remove(previous.Key); }
        custom.RemoveAll(p => p.Key == item.Key);
        custom.Add(item);
        deleted.Remove(item.Key); selection.Items[item.Key] = chosen;
        SaveReserved(id, selection with { Custom = custom, Deleted = deleted });
    }
    internal void DeleteReservedPhrase(string id, string code, string? text = null, string? key = null)
    {
        var selection = ReservedSelection(id);
        var custom = selection.Custom?.ToList() ?? [];
        var deleted = selection.Deleted is null ? new HashSet<string>() : new(selection.Deleted);
        foreach (var item in ReservedCatalog(id).Where(p => key is not null ? p.Key==key : p.Code == code && (text is null || p.Text == text)))
        { custom.RemoveAll(p => p.Key == item.Key); deleted.Add(item.Key); selection.Items.Remove(item.Key); }
        SaveReserved(id, selection with { Custom = custom, Deleted = deleted });
    }
    internal void ChangeReservedGroup(string id, string name, string? previous = null, bool remove = false)
    {
        name = name.Trim();
        if (name.Length is < 1 or > 40 || name.Any(char.IsControl)) throw new ArgumentException("分类名应为 1～40 个字。");
        var selection = ReservedSelection(id);
        var groups = ReservedGroups(id);
        var custom = selection.Custom?.ToList() ?? [];
        if (previous is not null)
        {
            foreach (var phrase in ReservedCatalog(id).Where(phrase => phrase.Group == previous))
            { custom.RemoveAll(item => item.Key == phrase.Key); custom.Add(phrase with { Group = remove ? "未分类" : name }); }
            groups.Remove(previous);
        }
        if (!remove && !groups.Contains(name)) groups.Add(name);
        SaveReserved(id, selection with { Custom = custom, Groups = groups });
    }

    internal void SetReservedGroupSelection(string id, string group, bool selected)
    {
        var profile = Profiles().Single(p => p.Id == id);
        if (!(profile.SupportsReserved ?? IsAzik(profile.Name))) throw new ArgumentException("保留字面板仅适用于 AZIK 及其复制配置。");
        var selection = ReservedSelection(id);
        foreach (var phrase in ReservedCatalog(id).Where(p => p.Group == group))
            selection.Items[phrase.Key] = selected;
        // Publish once for the entire group, not once per checkbox.
        SaveReserved(id, selection);
    }

    internal void SetReservedSelection(string id, bool? enabled = null, string? code = null, bool selected = false)
    {
        var profile = Profiles().Single(p => p.Id == id);
        if (!(profile.SupportsReserved ?? IsAzik(profile.Name))) throw new ArgumentException("保留字面板仅适用于 AZIK 及其复制配置。");
        var selection = ReservedSelection(id);
        if (code is not null)
        {
            var matches = ReservedCatalog(id).Where(p => p.Key == code || p.Code == code).ToList();
            if (matches.Count == 0) throw new ArgumentException("未知保留字。");
            foreach (var phrase in matches) selection.Items[phrase.Key] = selected;
        }
        selection = selection with { Enabled = enabled ?? selection.Enabled };
        SaveReserved(id, selection);
    }

    private string ApplyReservedPhrases(JapaneseInputTableProfile profile, string text)
    {
        if (!(profile.SupportsReserved ?? IsAzik(profile.Name))) return text;
        var selection = ReservedSelection(profile.Id);
        var rules = Parse(text).ToDictionary(rule => rule.Input, StringComparer.Ordinal);
        var declarations = text.Replace("\r\n", "\n").Split('\n')
            .Where(line => line.StartsWith("# zhongri-", StringComparison.Ordinal)).ToList();
        // Decode normal spelling without reserved shortcuts. Explicit Test
        // alternatives (ds=だい / ms=まい) belong to the normal spelling route.
        var normalRules = rules.ToDictionary(pair => pair.Key, pair => pair.Value, StringComparer.Ordinal);
        foreach (var phrase in ReservedPhrases.Where(phrase => phrase.DefaultEnabled))
        {
            var prefix = "# zhongri-alternative: " + phrase.Code + "=";
            var reading = declarations.FirstOrDefault(line => line.StartsWith(prefix, StringComparison.Ordinal))?[prefix.Length..];
            if (reading is not null) normalRules[phrase.Code] = new(phrase.Code, reading, "");
            else if (normalRules.TryGetValue(phrase.Code, out var rule) && rule.Output == phrase.Text)
                normalRules.Remove(phrase.Code);
        }
        var available = ReservedCatalog(profile.Id);
        var managed = ReservedPhrases.Concat(selection.Custom ?? []).GroupBy(phrase => phrase.Code).Select(group => group.Last());
        foreach (var managedPhrase in managed)
        {
            var activePhrases = available.Where(item => item.Code == managedPhrase.Code && selection.Enabled && IsReservedSelected(selection, item)).ToList();
            var phrase = activePhrases.FirstOrDefault() ?? managedPhrase;
            rules.TryGetValue(phrase.Code, out var original);
            var fixedLine = "# zhongri-fixed: " + phrase.Code;
            var alternatePrefix = "# zhongri-alternative: " + phrase.Code + "=";
            var normalPrefix = "# zhongri-normal-reading: " + phrase.Code + "=";
            var alternate = declarations.FirstOrDefault(line => line.StartsWith(alternatePrefix, StringComparison.Ordinal))?[alternatePrefix.Length..];
            var variantsPrefix = "# zhongri-reserved-variants: " + phrase.Code + "=";
            declarations.RemoveAll(line => line == fixedLine || line.StartsWith(alternatePrefix, StringComparison.Ordinal) || line.StartsWith(normalPrefix, StringComparison.Ordinal) || line.StartsWith(variantsPrefix, StringComparison.Ordinal));
            var active = activePhrases.Count > 0;
            if (active)
            {
                if (alternate is null && original is not null && original.Output != phrase.Text && original.Pending.Length == 0)
                    alternate = original.Output;
                rules[phrase.Code] = new(phrase.Code, phrase.Text, "");
                declarations.Add(fixedLine);
                declarations.Add(variantsPrefix + string.Join("/", activePhrases.Select(p => p.Text).Distinct()));
                var normal = Convert(phrase.Code, normalRules.Values.ToList());
                declarations.Add(normalPrefix + normal.Text + ";" + normal.Pending);
                if (!string.IsNullOrEmpty(alternate)) declarations.Add(alternatePrefix + alternate);
            }
            else if (ReservedPhrases.Any(item => item.Code == phrase.Code && item.DefaultEnabled))
            {
                // Test's ds/ms have explicit normal readings. Restore those
                // when unchecked; otherwise remove only the reserved rule.
                if (normalRules.TryGetValue(phrase.Code, out var normalRule)) rules[phrase.Code] = normalRule;
                else rules.Remove(phrase.Code);
            }
        }
        return string.Join("\n", declarations.Concat(rules.Values.Select(rule =>
            $"{rule.Input}\t{rule.Output}\t{rule.Pending}"))) + "\n";
    }
}
