using System.Text;
using System.Text.Json;

namespace ZhongriSettingsV2;

internal sealed record JapaneseKeyDefinition(string? First = null, string? Second = null,
    string? Single = null, string Action = "default", Dictionary<string, JapaneseInputRule?>? Rules = null,
    string? LongPress = null, int LongPressMilliseconds = 500, bool EmptyFirst = false, bool LongPressCompose = false);

internal sealed partial class JapaneseInputTableStore
{
    internal const string PrintableKeys = "1234567890-=qwertyuiop[]\\asdfghjkl;'zxcvbnm,./`~!@#$%^&*()_+{}|:\"<>?ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    private static readonly string[] KeyActions = ["default", "input", "literal", "next", "previous"];
    private static readonly Dictionary<string, int> FunctionKeys = new()
        { ["Space"] = 32, ["Enter"] = 0xff0d, ["BackSpace"] = 0xff08, ["PageUp"] = 0xff55, ["PageDown"] = 0xff56 };
    internal static int KeyCode(string key) => key.Length == 1 && PrintableKeys.Contains(key, StringComparison.Ordinal)
        ? key[0] : FunctionKeys.TryGetValue(key, out var code) ? code : throw new ArgumentException("不支持的按键。");

    internal JapaneseInputTableProfile KeyProfile(string id) => Profiles().Single(p => p.Id == id);
    internal JapaneseInputTable BaseTable(string id)
    {
        var profile = KeyProfile(id);
        var text = File.ReadAllText(Path.Combine(ProfilesDirectory, id + ".tsv"));
        return new(profile.Name, Parse(text), id, IsRgt(text));
    }
    internal JapaneseKeyDefinition KeyDefinition(string id, string key) =>
        KeyProfile(id).Keys?.GetValueOrDefault(key) ?? new();

    internal JapaneseInputTable CreateEditableRomaji(string name = "罗马字自定义")
    {
        var id = Guid.NewGuid().ToString("N");
        var rules = StandardRomaji();
        var profile = new JapaneseInputTableProfile(id, ValidateProfileName(name), rules.Count);
        Directory.CreateDirectory(ProfilesDirectory);
        WriteAtomic(Path.Combine(ProfilesDirectory, id + ".tsv"), string.Join("\n", rules.Select(r => $"{r.Input}\t{r.Output}\t{r.Pending}")) + "\n");
        WriteAtomic(Path.Combine(ProfilesDirectory, id + ".json"), JsonSerializer.Serialize(profile));
        return SelectProfile(id);
    }

    internal JapaneseInputTable SaveKey(string id, string key, JapaneseKeyDefinition? definition)
        => SaveKeys(id, new Dictionary<string, JapaneseKeyDefinition?> { [key] = definition });

    internal JapaneseInputTable SaveKeys(string id, IReadOnlyDictionary<string, JapaneseKeyDefinition?> changes)
    {
        lock(_keyCompilationGate) return SaveKeysCore(id,changes);
    }

    private JapaneseInputTable SaveKeysCore(string id, IReadOnlyDictionary<string, JapaneseKeyDefinition?> changes)
    {
        var profile = KeyProfile(id);
        var keys = new Dictionary<string, JapaneseKeyDefinition>(profile.Keys ?? []);
        foreach (var (key, definition) in changes)
        {
        _ = KeyCode(key);
        if (definition is null) keys.Remove(key);
        else
        {
            if (!KeyActions.Contains(definition.Action)) throw new ArgumentException("无效的按键功能。");
            if (definition.First is not null && (definition.First.Length > 16 || definition.First.Any(c => !char.IsAsciiLetter(c))))
                throw new ArgumentException("第一键请填写罗马字声母，如 k、ky、ry。");
            if (definition.Second is not null) _ = SplitTails(definition.Second);
            if (definition.Single is not null && (definition.Single.Length > 128 || definition.Single.Any(char.IsControl)))
                throw new ArgumentException("单键输出不能包含控制字符，最多 128 个字符。");
            if (definition.LongPress is not null && (key.Length != 1 || definition.LongPress.Length > 128 || definition.LongPress.Any(char.IsControl)))
                throw new ArgumentException("长按输出仅支持字符键，最多 128 个字符，不能包含控制字符。");
            if (definition.LongPressMilliseconds is < 100 or > 3000)
                throw new ArgumentException("长按时间须为 100～3000 毫秒。");
            if (definition.EmptyFirst && definition.Action is "literal" or "next" or "previous")
                throw new ArgumentException("空声母键须使用默认或参与日语按键表功能。");
            if (key.Length != 1 && (definition.EmptyFirst || definition.First is not null || definition.Second is not null || definition.Single is not null ||
                definition.Action is "input" or "literal")) throw new ArgumentException("功能键支持默认、上一行和下一行。");
            if (definition.Rules is not null)
                foreach (var pair in definition.Rules)
                {
                    if (pair.Key.Length == 0 || (pair.Value is not null && pair.Key != pair.Value.Input))
                        throw new ArgumentException("组合按键不一致。");
                    if (pair.Value is not null) _ = Parse($"{pair.Value.Input}\t{pair.Value.Output}\t{pair.Value.Pending}");
                }
            keys[key] = definition;
        }
        }
        var updated = profile with { Keys = keys };
        // Validate all generated rows before publishing either metadata or live rules.
        var compiled = CompileKeyDefinitions(updated, File.ReadAllText(Path.Combine(ProfilesDirectory, id + ".tsv")));
        compiled=ApplyReservedPhrases(updated,compiled);
        _=Parse(compiled);
        WriteAtomic(Path.Combine(ProfilesDirectory, id + ".json"), JsonSerializer.Serialize(updated));
        return PublishCompiledProfile(updated,compiled,Enabled);
    }

    private static List<JapaneseInputRule>? _standardRomaji;
    private static Dictionary<string, string>? _reverseRomaji;
    private static List<JapaneseInputRule> StandardRomaji()
    {
        if (_standardRomaji is not null) return _standardRomaji;
        var path = Path.Combine(AppContext.BaseDirectory, "romaji-reference.tsv");
        if (!File.Exists(path)) throw new InvalidOperationException("缺少日语读音参考表，请修复设置程序。");
        return _standardRomaji = Parse(File.ReadAllText(path).Replace("\\t", "\t"));
    }

    internal static string Romanize(string kana)
    {
        kana = string.Concat(kana.Select(c => c >= 'ァ' && c <= 'ヶ' ? (char)(c - 0x60) : c));
        var reverse = _reverseRomaji ??= BuildCanonicalReverse();
        var result = new StringBuilder();
        while (kana.Length > 0)
        {
            var match = reverse.Keys.Where(k => kana.StartsWith(k, StringComparison.Ordinal)).MaxBy(k => k.Length);
            if (match is null) return "";
            result.Append(reverse[match]); kana = kana[match.Length..];
        }
        return result.ToString();
    }

    private static Dictionary<string, string> BuildCanonicalReverse()
    {
        var reverse = StandardRomaji().Where(r => r.Pending == "" && r.Output != "" && r.Input.All(char.IsAsciiLetter))
            .GroupBy(r => r.Output).ToDictionary(g => g.Key, g => g.OrderBy(r => r.Input.Length).ThenBy(r => r.Input).First().Input);
        foreach (var (prefix, row) in new[] { ("", "あいうえお"), ("k", "かきくけこ"), ("g", "がぎぐげご"),
            ("s", "さしすせそ"), ("z", "ざじずぜぞ"), ("t", "たちつてと"), ("d", "だぢづでど"),
            ("n", "なにぬねの"), ("h", "はひふへほ"), ("b", "ばびぶべぼ"), ("p", "ぱぴぷぺぽ"),
            ("m", "まみむめも"), ("r", "らりるれろ") })
            for (var i = 0; i < 5; i++) reverse[row[i].ToString()] = prefix + "aiueo"[i];
        foreach (var (head, prefix) in new[] { ("き", "ky"), ("ぎ", "gy"), ("し", "sh"), ("じ", "j"),
            ("ち", "ch"), ("に", "ny"), ("ひ", "hy"), ("び", "by"), ("ぴ", "py"), ("み", "my"), ("り", "ry") })
            for (var i = 0; i < 3; i++) reverse[head + "ゃゅょ"[i]] = prefix + "auo"[i];
        reverse["し"] = "shi"; reverse["ち"] = "chi"; reverse["つ"] = "tsu";
        reverse["ふ"] = "fu"; reverse["じ"] = "ji"; reverse["ん"] = "n";
        return reverse;
    }

    internal JapaneseInputTable PreviewKey(string id, string key, JapaneseKeyDefinition definition)
    {
        lock(_keyCompilationGate) {
        var profile = KeyProfile(id);
        var keys = new Dictionary<string, JapaneseKeyDefinition>(profile.Keys ?? []) { [key] = definition };
        var text = CompileKeyDefinitions(profile with { Keys = keys }, File.ReadAllText(Path.Combine(ProfilesDirectory, id + ".tsv")));
        text = ApplyReservedPhrases(profile, text);
        return new(profile.Name, Parse(text), id, IsRgt(text));
        }
    }

    internal static (string First, string Second, string Single) InferKey(string key, JapaneseInputTable table)
    {
        if (key.Length != 1) return ("", "", "");
        var single = table.Rules.FirstOrDefault(r => r.Input == key && r.Pending == "")?.Output ?? "";
        if (char.IsAsciiLetterUpper(key[0]) && !table.Rules.Any(r => r.Input.StartsWith(key, StringComparison.Ordinal)))
            return InferKey(key.ToLowerInvariant(), table);
        var first = "";
        foreach (var vowel in "aiueo")
        {
            var value = Convert(key + vowel, table.Rules, table.Rgt);
            var reading = value.Pending == "" ? Romanize(value.Text) : "";
            if (table.Rules.Any(r => r.Input == key + vowel) && reading.EndsWith(vowel) && value.Text.Length <= 2 &&
                !"aeiou".Contains(key, StringComparison.OrdinalIgnoreCase))
            { first = reading[..^1]; break; }
        }
        var tails = new List<string>();
        foreach (var prefix in new[] { "k", "h", "m", "r", "n", "g", "b", "p" })
        {
            var value = Convert(prefix + key, table.Rules, table.Rgt);
            var reading = value.Pending == "" ? Romanize(value.Text) : "";
            if (reading.StartsWith(prefix) && reading.Length > prefix.Length) tails.Add(reading[prefix.Length..]);
        }
        var tail = tails.GroupBy(x => x).OrderByDescending(g => g.Count()).FirstOrDefault();
        return (first, tail is not null && tail.Count() >= 3 ? tail.Key : "aeiou".Contains(key) ? key : "", single);
    }

    private readonly object _keyCompilationGate=new();
    private string? _inferredBaseText;
    private Dictionary<string,(string First,string Second,string Single)>? _inferredBaseKeys;

    private string CompileKeyDefinitions(JapaneseInputTableProfile profile, string text)
    {
        lock(_keyCompilationGate) return CompileKeyDefinitionsCore(profile,text);
    }

    private string CompileKeyDefinitionsCore(JapaneseInputTableProfile profile, string text)
    {
        // One coherent flag snapshot per compile, never thousands of synchronous
        // File.Exists/ReadAllText calls inside the key-pair cross product.
        var vowelTailOverlap=VowelTailOverlap;
        profile = profile with { Keys = profile.Keys ?? new Dictionary<string, JapaneseKeyDefinition>() };
        var original = new JapaneseInputTable(profile.Name, Parse(text), profile.Id, IsRgt(text));
        var rows = original.Rules.ToDictionary(r => r.Input, StringComparer.Ordinal);
        var changedCodes = new HashSet<string>(StringComparer.Ordinal);
        if(_inferredBaseText!=text || _inferredBaseKeys is null) {
            _inferredBaseKeys=PrintableKeys.ToDictionary(c=>c.ToString(),c=>InferKey(c.ToString(),original));
            _inferredBaseText=text;
        }
        var inferred = _inferredBaseKeys;
        var tailCache=new Dictionary<string,string[]>();
        string[] Tails(string value) {
            if(!tailCache.TryGetValue(value,out var result))tailCache[value]=result=SplitTails(value);
            return result;
        }
        var joinCache=new Dictionary<(string,string),JapaneseInputPreview>();
        JapaneseInputPreview Join(string first,string tail) {
            if(!joinCache.TryGetValue((first,tail),out var result))joinCache[(first,tail)]=result=JoinTail(first,tail);
            return result;
        }
        var actions = new List<string>();
        foreach(var key in PrintableKeys.Select(c=>c.ToString())) {
            var tail=profile.Keys.GetValueOrDefault(key)?.Second ?? inferred[key].Second;
            var readings=Tails(tail).Select(t=>Convert(t,StandardRomaji()))
                .Where(p=>p.Pending=="" && p.Text!="").Select(p=>p.Text).Distinct().ToList();
            if(readings.Count>0)actions.Add($"# zhongri-key-tails: {KeyCode(key)}="+string.Join("/",readings));
        }
        var variants = new Dictionary<string, List<string>>();
        var overlapCodes = new HashSet<string>(StringComparer.Ordinal);
        foreach (var key in PrintableKeys.Select(c => c.ToString()))
            foreach (var other in PrintableKeys.Select(c => c.ToString()))
            {
                var a = profile.Keys.GetValueOrDefault(key);
                var b = profile.Keys.GetValueOrDefault(other);
                var upperFirst = char.IsAsciiLetterUpper(key[0]) && profile.Keys.ContainsKey(key);
                var upperSecond = char.IsAsciiLetterUpper(other[0]) && profile.Keys.ContainsKey(other);
                if (vowelTailOverlap && a?.EmptyFirst != true && "aeiou".Contains(key, StringComparison.Ordinal) &&
                    string.IsNullOrEmpty(a?.First) && inferred[key].First == "")
                {
                    // A vowel selects the matching onset of a second-key tail:
                    // a + aku -> aku, i + itsu -> itsu. Never rewrite ordinary
                    // kana concatenation or take over an existing/custom code.
                    var code = key + other;
                    if (rows.ContainsKey(code)) continue;
                    var tail = b?.Second ?? inferred[other].Second;
                    var overlapReadings = Tails(tail)
                        .Where(t => t.Length > 1 && t.StartsWith(key, StringComparison.OrdinalIgnoreCase))
                        .Select(t => Convert(t.ToLowerInvariant(), StandardRomaji()))
                        .Where(p => p.Pending == "" && p.Text.Length > 0)
                        .Select(p => p.Text).Distinct().ToList();
                    if (overlapReadings.Count > 0)
                    {
                        changedCodes.Add(code);
                        rows[code] = new(code, overlapReadings[0], "");
                        overlapCodes.Add(code);
                        if (overlapReadings.Count > 1) variants[code] = overlapReadings;
                    }
                    continue;
                }
                if (a?.First is null && a?.EmptyFirst != true && b?.Second is null && !upperFirst && !upperSecond) continue;
                changedCodes.Add(key + other);
                var first = a?.EmptyFirst == true ? "" : a?.First ?? (char.IsAsciiLetterUpper(key[0]) && !upperFirst && !original.Rules.Any(r => r.Input.StartsWith(key, StringComparison.Ordinal)) ? "" : inferred[key].First);
                var second = b?.Second ?? (char.IsAsciiLetterUpper(other[0]) && !upperSecond && !original.Rules.Any(r => r.Input.Contains(other, StringComparison.Ordinal)) ? "" : inferred[other].Second);
                rows.Remove(key + other);
                if ((first == "" && a?.EmptyFirst != true) || second == "") continue;
                var readings = Tails(second).Select(tail => Join(first, tail))
                    .Where(output => output.Pending == "" && output.Text.Length > 0).Select(output => output.Text).Distinct().ToList();
                if (readings.Count > 0) rows[key + other] = new(key + other, readings[0], "");
                if (readings.Count > 1) variants[key + other] = readings;
            }
        // Imported tables also contain multi-letter onsets (hy/sh/ch/...):
        // updating a tail must regenerate hys as well as hs. Derive the onset
        // from that row's original reading and original inferred tail; never
        // rewrite an arbitrary phrase or a later explicit rule override.
        foreach(var rule in original.Rules.Where(r=>r.Input.Length is >=3 and <=5 && r.Pending=="")) {
            var last=rule.Input[^1..];var definition=profile.Keys.GetValueOrDefault(last);
            if(definition?.Second is null)continue;
            var roman=Romanize(rule.Output);
            var old=Tails(inferred[last].Second).OrderByDescending(t=>t.Length)
                .FirstOrDefault(t=>roman.EndsWith(t,StringComparison.Ordinal) && roman.Length>t.Length);
            if(old is null)continue;
            var onset=roman[..^old.Length];
            if(onset.Any(ch=>!char.IsAsciiLetter(ch) || "aeiou".Contains(ch)))continue;
            var readings=Tails(definition.Second).Select(t=>Join(onset,t)).Where(p=>p.Pending=="" && p.Text!="")
                .Select(p=>p.Text).Distinct().ToList();
            changedCodes.Add(rule.Input);rows.Remove(rule.Input);variants.Remove(rule.Input);
            if(readings.Count>0)rows[rule.Input]=new(rule.Input,readings[0],"");
            if(readings.Count>1)variants[rule.Input]=readings;
        }
        foreach (var (key, definition) in profile.Keys.OrderBy(p => p.Key, StringComparer.Ordinal))
        {
            if(definition.EmptyFirst) actions.Add($"# zhongri-key-empty-first: {KeyCode(key)}");
            if (definition.EmptyFirst) rows.Remove(key);
            else if (definition.Single is not null)
            {
                rows.Remove(key);
                if (definition.Single != "") rows[key] = new(key, definition.Single, "");
            }
            if (definition.Rules is not null)
                foreach (var (code, rule) in definition.Rules)
                {
                    changedCodes.Add(code);
                    overlapCodes.Remove(code);
                    variants.Remove(code);
                    if (rule is null) rows.Remove(code); else rows[code] = rule;
                }
            var action = definition.Action;
            if (action == "default" && (definition.EmptyFirst || definition.First is not null || definition.Second is not null || definition.Single is not null || definition.Rules?.Count > 0)) action = "input";
            if (action != "default") actions.Add($"# zhongri-key-action: {KeyCode(key)}={action}");
        }
        // An alias must not steal a longer original syllable at its boundary:
        // it -> いつ, but i + tsu must remain いつ, not it + su -> いつす.
        // Materialize only small text look-ahead rows, keeping explicit rows
        // and overrides first. OFF regenerates the exact original table.
        if (overlapCodes.Count > 0)
        {
            var baseline = rows.Values.Where(r => !overlapCodes.Contains(r.Input)).ToList();
            var standard = StandardRomaji().ToDictionary(r => r.Input, StringComparer.Ordinal);
            foreach (var alias in overlapCodes)
                // Protect real romaji syllables, not arbitrary two-key AZIK
                // abbreviations such as sm=せき. Otherwise asm steals as+ma
                // and the promised asma=あくま becomes あせきあ.
                foreach (var originalRule in baseline.Where(r => r.Input.StartsWith(alias[1..], StringComparison.Ordinal) && r.Input.Length > 1 &&
                    standard.TryGetValue(r.Input, out var syllable) && syllable.Output == r.Output && syllable.Pending == r.Pending))
                {
                    var code = alias[..1] + originalRule.Input;
                    if (rows.ContainsKey(code) || changedCodes.Contains(code)) continue;
                    var before = Convert(code, baseline, original.Rgt);
                    if (before.Text.Length > 0) rows[code] = new(code, before.Text, before.Pending);
                }
        }
        foreach(var (key,definition) in profile.Keys.OrderBy(p=>p.Key,StringComparer.Ordinal))
            if(!string.IsNullOrEmpty(definition.LongPress)) {
                var output=definition.LongPressCompose?EncodeLongPressComposition(definition.LongPress,rows.Values.ToList(),original.Rgt):definition.LongPress;
                actions.Add($"# zhongri-key-long-press: {KeyCode(key)}={definition.LongPressMilliseconds},{System.Convert.ToHexString(Encoding.UTF8.GetBytes(output))}"+(definition.LongPressCompose?",input":""));
            }
        var declarations = text.Replace("\r\n", "\n").Split('\n').Where(line =>
        {
            if (!line.StartsWith('#') || line.StartsWith("# zhongri-key-action:") || line.StartsWith("# zhongri-key-variants:") || line.StartsWith("# zhongri-key-long-press:") || line.StartsWith("# zhongri-key-empty-first:") || line.StartsWith("# zhongri-key-tails:") || line.StartsWith("# zhongri-single-key-tail:") || line.StartsWith("# zhongri-reserved-whole:")) return false;
            foreach (var code in changedCodes)
                if (line.StartsWith("# zhongri-alternative: " + code + "=", StringComparison.Ordinal) ||
                    line.StartsWith("# zhongri-normal-reading: " + code + "=", StringComparison.Ordinal)) return false;
            return true;
        });
        actions.Add("# zhongri-single-key-tail: "+(SingleKeyTail?"1":"0"));
        actions.Add("# zhongri-reserved-whole: "+(ReservedWhole?"1":"0"));
        var compiled = string.Join("\n", declarations.Concat(actions)
            .Concat(variants.Select(p => "# zhongri-key-variants: " + p.Key + "=" + string.Join("/", p.Value)))
            .Concat(rows.Values.Select(r => $"{r.Input}\t{r.Output}\t{r.Pending}"))) + "\n";
        _ = Parse(compiled);
        return compiled;
    }

    internal static string[] SplitTails(string value)
    {
        if (value == "") return [];
        var parts = value.Split(['/', '／', ',', '，', ' ', '\n'], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries).Distinct().ToArray();
        if (parts.Length is < 1 or > 8 || parts.Any(p => p.Length is < 1 or > 16 || p.Any(c => !char.IsAsciiLetter(c))))
            throw new ArgumentException("第二键支持多个韵尾，如 ati/iti；最多 8 个，用斜线、逗号或空格分隔。");
        return parts;
    }

    private static string EncodeLongPressComposition(string value,List<JapaneseInputRule> rules,bool rgt)
    {
        // Accept physical codes directly, or encode kana through this exact
        // current table. Never assume standard romaji on a custom keyboard.
        if(value.All(c=>PrintableKeys.Contains(c))) {
            var parsed=Convert(value,rules,rgt);
            if(parsed.Pending=="" && parsed.Text!="")return value;
        }
        var states=new Dictionary<int,List<string>>{[0]=[""]};
        var choices=rules.Where(r=>r.Output!="" && r.Pending=="" && r.Input.All(c=>PrintableKeys.Contains(c)))
            .OrderBy(r=>r.Input.Length).ThenBy(r=>r.Input,StringComparer.Ordinal).ToList();
        for(var position=0;position<value.Length;position++) {
            if(!states.TryGetValue(position,out var prefixes))continue;
            foreach(var row in choices) {
                if(!value.AsSpan(position).StartsWith(row.Output,StringComparison.Ordinal))continue;
                var end=position+row.Output.Length;
                if(!states.TryGetValue(end,out var bucket))states[end]=bucket=[];
                foreach(var prefix in prefixes) {
                    var code=prefix+row.Input;
                    if(code.Length<=128 && bucket.Count<16 && !bucket.Contains(code))bucket.Add(code);
                }
            }
        }
        foreach(var code in states.GetValueOrDefault(value.Length) ?? []) {
            var parsed=Convert(code,rules,rgt);
            if(parsed.Text==value && parsed.Pending=="")return code;
        }
        throw new ArgumentException("长按候选内容无法用当前按键表表示；请填写可用的假名读音或按键编码。");
    }

    private static JapaneseInputPreview JoinTail(string first, string tail)
    {
        first = first.ToLowerInvariant(); tail = tail.ToLowerInvariant();
        // Compose the first syllable and the remainder independently so
        // full-string shortcut aliases cannot change the suffix's meaning.
        if ("aiueo".Contains(tail[0]))
        {
            var head = Convert(first + tail[..1], StandardRomaji());
            var rest = Convert(tail[1..], StandardRomaji());
            return new(head.Text + rest.Text, head.Pending + rest.Pending);
        }
        return Convert(first + tail, StandardRomaji());
    }
}
