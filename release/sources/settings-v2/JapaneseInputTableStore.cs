using System.Text;
using System.Text.Json;
using Microsoft.Win32;

namespace ZhongriSettingsV2;

internal sealed record JapaneseInputRule(string Input, string Output, string Pending);
internal sealed record JapaneseInputTable(string Name, List<JapaneseInputRule> Rules, string Id = "", bool Rgt = false);
internal sealed record JapaneseInputTableProfile(string Id, string Name, int Count,
    Dictionary<string, JapaneseKeyDefinition>? Keys = null, bool Deleted = false, bool? SupportsReserved = null);
internal sealed record JapaneseInputPreview(string Text, string Pending);

internal sealed partial class JapaneseInputTableStore(string directory, string registryPath, string? bundledDirectory = null)
{
    internal string TablePath => Path.Combine(directory, "japanese_input_table.tsv");
    private string MetadataPath => Path.Combine(directory, "japanese_input_table.json");
    private string ProfilesDirectory => Path.Combine(directory, "japanese_input_tables");
    private string StatePath => Path.Combine(directory, "japanese_input_table.state");

    internal List<JapaneseInputTableProfile> Profiles()
    {
        EnsureBundledProfiles();
        if (!Directory.Exists(ProfilesDirectory)) return [];
        var profiles = Directory.EnumerateFiles(ProfilesDirectory, "*.json")
            .Where(path => !path.EndsWith(".reserved.json", StringComparison.OrdinalIgnoreCase))
            .Select(path => JsonSerializer.Deserialize<JapaneseInputTableProfile>(File.ReadAllText(path))!)
            .Where(profile => profile is not null && !profile.Deleted)
            .OrderBy(profile => profile.Name, StringComparer.CurrentCultureIgnoreCase).ToList();
        var bundled = BundledProfilesDirectory();
        var builtinIds = Directory.Exists(bundled) ? Directory.EnumerateFiles(bundled, "*.json")
            .Select(path => Path.GetFileNameWithoutExtension(path)).ToHashSet(StringComparer.OrdinalIgnoreCase) : [];
        var activeId = "";
        if (File.Exists(MetadataPath))
        {
            using var metadata = JsonDocument.Parse(File.ReadAllText(MetadataPath));
            if (metadata.RootElement.TryGetProperty("id", out var id)) activeId = id.GetString() ?? "";
        }
        // A migrated library can contain the same named built-in under an old
        // random ID. Hide only that bundled duplicate; keep every file intact.
        return profiles.GroupBy(p => p.Name, StringComparer.OrdinalIgnoreCase)
            .SelectMany(group => group.Any(p => builtinIds.Contains(p.Id)) && group.Count() > 1
                ? new[] { group.FirstOrDefault(p => p.Id == activeId) ?? group.FirstOrDefault(p => !builtinIds.Contains(p.Id)) ?? group.First() }
                : group.AsEnumerable()).ToList();
    }

    private string BundledProfilesDirectory()
    {
        using var key = Registry.CurrentUser.OpenSubKey(registryPath);
        var root = key?.GetValue("ZhongriRoot") as string ?? Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", ".."));
        return bundledDirectory ?? Path.Combine(root, "data", "japanese_input_tables");
    }

    private void EnsureBundledProfiles()
    {
        var bundled = BundledProfilesDirectory();
        if (!Directory.Exists(bundled)) return;
        Directory.CreateDirectory(ProfilesDirectory);
        var names = Directory.EnumerateFiles(ProfilesDirectory, "*.json")
            .Where(path => !path.EndsWith(".reserved.json", StringComparison.OrdinalIgnoreCase))
            .Select(path => JsonSerializer.Deserialize<JapaneseInputTableProfile>(File.ReadAllText(path))?.Name ?? "")
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        foreach (var file in Directory.EnumerateFiles(bundled, "*.json"))
        {
            var profile = JsonSerializer.Deserialize<JapaneseInputTableProfile>(File.ReadAllText(file));
            if (profile is null || !Guid.TryParseExact(profile.Id, "N", out _) || names.Contains(profile.Name)) continue;
            var source = Path.Combine(bundled, profile.Id + ".tsv");
            var text = File.ReadAllText(source);
            _ = Parse(text); // Do not install a malformed built-in table.
            // Existing personal tables and reserved selections are never replaced.
            var target = Path.Combine(ProfilesDirectory, profile.Id + ".tsv");
            if (File.Exists(target)) continue;
            WriteAtomic(target, text);
            WriteAtomic(Path.Combine(ProfilesDirectory, profile.Id + ".json"), File.ReadAllText(file));
            names.Add(profile.Name);
        }
    }

    internal bool Enabled
    {
        get
        {
            if (File.Exists(StatePath))
                return File.ReadAllLines(StatePath).FirstOrDefault() == "1";
            using var key = Registry.CurrentUser.OpenSubKey(registryPath);
            return key?.GetValue("Option_japanese_input_table_enabled") is int value && value != 0;
        }
    }

    internal bool Mixed => File.Exists(StatePath) && File.ReadAllLines(StatePath).ElementAtOrDefault(2) == "1";
    internal void SetMixed(bool mixed) => Publish(Enabled, mixed);
    internal bool ShowLetters => File.Exists(StatePath) && File.ReadAllLines(StatePath).ElementAtOrDefault(3) == "1";
    internal void SetShowLetters(bool showLetters) => Publish(Enabled, showLetters: showLetters);

    private string VowelTailPath => Path.Combine(directory, "japanese_vowel_tail_overlap.flag");
    private string SingleKeyTailPath => Path.Combine(directory, "japanese_single_key_tail.flag");
    private string ReservedWholePath => Path.Combine(directory, "japanese_reserved_whole.flag");
    internal bool ReservedWhole => File.Exists(ReservedWholePath) && File.ReadAllText(ReservedWholePath).Trim()=="1";
    internal void SetReservedWhole(bool enabled)
    {
        var previous=ReservedWhole;
        if(previous==enabled)return;
        var active=Read();
        var before=new[]{TablePath,MetadataPath,StatePath}.Where(File.Exists).ToDictionary(path=>path,File.ReadAllText);
        Directory.CreateDirectory(directory);
        WriteAtomic(ReservedWholePath,enabled?"1\n":"0\n");
        try {if(active is not null && active.Id!="")SelectProfile(active.Id,Enabled);else Publish(Enabled);}
        catch {
            WriteAtomic(ReservedWholePath,previous?"1\n":"0\n");
            foreach(var (path,contents) in before)WriteAtomic(path,contents);
            throw;
        }
    }
    internal bool SingleKeyTail => File.Exists(SingleKeyTailPath) && File.ReadAllText(SingleKeyTailPath).Trim()=="1";
    internal void SetSingleKeyTail(bool enabled)
    {
        var previous=SingleKeyTail;
        if(previous==enabled)return;
        var active=Read();
        var before=new[]{TablePath,MetadataPath,StatePath}.Where(File.Exists).ToDictionary(path=>path,File.ReadAllText);
        Directory.CreateDirectory(directory);
        WriteAtomic(SingleKeyTailPath,enabled?"1\n":"0\n");
        try {if(active is not null && active.Id!="")SelectProfile(active.Id,Enabled);else Publish(Enabled);}
        catch {
            WriteAtomic(SingleKeyTailPath,previous?"1\n":"0\n");
            foreach(var (path,contents) in before)WriteAtomic(path,contents);
            throw;
        }
    }
    internal bool VowelTailOverlap => File.Exists(VowelTailPath) && File.ReadAllText(VowelTailPath).Trim() == "1";
    internal void SetVowelTailOverlap(bool enabled)
    {
        var previous = VowelTailOverlap;
        if (previous == enabled) return;
        var active = Read();
        var before = new[] { TablePath, MetadataPath, StatePath }
            .Where(File.Exists).ToDictionary(path => path, File.ReadAllText);
        Directory.CreateDirectory(directory);
        WriteAtomic(VowelTailPath, enabled ? "1\n" : "0\n");
        try
        {
            // Publish the ordinary table revision: already-open sessions reload
            // small text rules without deployment or service restart.
            if (active is not null && active.Id != "") SelectProfile(active.Id, Enabled);
            else Publish(Enabled);
        }
        catch (Exception failure)
        {
            try
            {
                WriteAtomic(VowelTailPath, previous ? "1\n" : "0\n");
                foreach (var (path, contents) in before) WriteAtomic(path, contents);
            }
            catch (Exception rollback) { throw new AggregateException("元音简拼保存失败，恢复也失败；请保留当前配置。", failure, rollback); }
            throw new InvalidOperationException("元音简拼保存失败，已恢复原按键表。", failure);
        }
    }

    internal JapaneseInputTable? Read()
    {
        if (!File.Exists(TablePath)) return null;
        var name = "自定义按键表";
        var id = "";
        if (File.Exists(MetadataPath))
        {
            using var metadata = JsonDocument.Parse(File.ReadAllText(MetadataPath));
            name = metadata.RootElement.GetProperty("name").GetString() ?? name;
            if (metadata.RootElement.TryGetProperty("id", out var identifier)) id = identifier.GetString() ?? "";
            var profilePath = Path.Combine(ProfilesDirectory, id + ".json");
            if (File.Exists(profilePath) && JsonSerializer.Deserialize<JapaneseInputTableProfile>(File.ReadAllText(profilePath))?.Deleted == true) return null;
        }
        var text = File.ReadAllText(TablePath, Encoding.UTF8);
        return new(name, Parse(text), id, IsRgt(text));
    }

    internal JapaneseInputTable Import(string path, bool enable = true)
    {
        var profile = AddProfile(path);
        return SelectProfile(profile.Id, enable);
    }

    // Add to the library without temporarily replacing the user's live table.
    internal JapaneseInputTableProfile AddProfile(string path)
    {
        var bytes = File.ReadAllBytes(path);
        if (bytes.Length > 1024 * 1024) throw new ArgumentException("按键表不能超过 1 MB。");
        string text;
        if (bytes.Length >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe)
            text = new UnicodeEncoding(false, true, true).GetString(bytes);
        else if (bytes.Length >= 2 && bytes[0] == 0xfe && bytes[1] == 0xff)
            text = new UnicodeEncoding(true, true, true).GetString(bytes);
        else
        {
            try { text = new UTF8Encoding(false, true).GetString(bytes); }
            catch (DecoderFallbackException)
            {
                Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
                text = Encoding.GetEncoding(932, EncoderFallback.ExceptionFallback,
                    DecoderFallback.ExceptionFallback).GetString(bytes);
            }
        }
        var rules = Parse(text);
        var declarations = text.Replace("\r\n", "\n").Split('\n').Where(line =>
            line.StartsWith("# zhongri-fixed: ", StringComparison.Ordinal) ||
            line.StartsWith("# zhongri-alternative: ", StringComparison.Ordinal) ||
            line.StartsWith("# zhongri-normal-reading: ", StringComparison.Ordinal)).ToArray();
        foreach (var line in declarations)
        {
            if (line.StartsWith("# zhongri-normal-reading: ", StringComparison.Ordinal))
            {
                var normal = System.Text.RegularExpressions.Regex.Match(line,
                    @"^# zhongri-normal-reading: ([a-z0-9]+)=([^;\t\r\n]*);([a-z0-9]*)$");
                if (!normal.Success || !rules.Any(rule => rule.Input == normal.Groups[1].Value))
                    throw new ArgumentException("原始读音声明无效：" + line);
                continue;
            }
            var match = System.Text.RegularExpressions.Regex.Match(line,
                @"^# zhongri-(?:fixed: ([a-z0-9]+)|alternative: ([a-z0-9]+)=([\u3041-\u3096\u30a1-\u30fa\u30fc]+))$");
            var code = match.Groups[1].Success ? match.Groups[1].Value : match.Groups[2].Value;
            if (!match.Success || !rules.Any(rule => rule.Input == code))
                throw new ArgumentException("固定搭配或备选读音声明无效：" + line);
        }
        var id = Guid.NewGuid().ToString("N");
        var name = Path.GetFileNameWithoutExtension(path);
        Directory.CreateDirectory(ProfilesDirectory);
        WriteAtomic(Path.Combine(ProfilesDirectory, id + ".tsv"), (IsRgt(text) ? RgtHeader + "\n" : "") +
            (declarations.Length > 0 ? string.Join("\n", declarations) + "\n" : "") + string.Join("\n", rules.Select(rule =>
            $"{rule.Input}\t{rule.Output}\t{rule.Pending}")) + "\n");
        WriteAtomic(Path.Combine(ProfilesDirectory, id + ".json"),
            JsonSerializer.Serialize(new JapaneseInputTableProfile(id, name, rules.Count)));
        return new JapaneseInputTableProfile(id, name, rules.Count);
    }

    internal JapaneseInputTable SelectProfile(string id, bool enable = true)
    {
        if (!Guid.TryParseExact(id, "N", out _)) throw new ArgumentException("无效的按键表标识。");
        var profile = Profiles().SingleOrDefault(profile => profile.Id == id)
            ?? throw new ArgumentException("找不到这套按键表。");
        var text = File.ReadAllText(Path.Combine(ProfilesDirectory, id + ".tsv"), Encoding.UTF8);
        text = CompileKeyDefinitions(profile, text);
        text = ApplyReservedPhrases(profile, text);
        return PublishCompiledProfile(profile,text,enable);
    }

    private JapaneseInputTable PublishCompiledProfile(JapaneseInputTableProfile profile,string text,bool enable)
    {
        var rules = Parse(text);
        WriteAtomic(TablePath, text);
        WriteAtomic(MetadataPath, JsonSerializer.Serialize(new { id=profile.Id, name = profile.Name, count = rules.Count }));
        Publish(enable);
        return new(profile.Name, rules, profile.Id, IsRgt(text));
    }

    private string ValidateProfileName(string name, string? exceptId = null)
    {
        name = name.Trim();
        if (name.Length is < 1 or > 60 || name.Any(char.IsControl)) throw new ArgumentException("名称须为 1～60 个字符，不能包含控制字符。");
        if (Profiles().Any(p => p.Id != exceptId && string.Equals(p.Name, name, StringComparison.OrdinalIgnoreCase)))
            throw new ArgumentException("已有同名配置，请换一个名称。");
        return name;
    }

    internal JapaneseInputTableProfile CopyProfile(string id, string name)
    {
        name = ValidateProfileName(name);
        var source = KeyProfile(id);
        var copy = source with { Id = Guid.NewGuid().ToString("N"), Name = name, Deleted = false,
            SupportsReserved = source.SupportsReserved ?? IsAzik(source.Name) };
        var text = File.ReadAllText(Path.Combine(ProfilesDirectory, source.Id + ".tsv"));
        _ = CompileKeyDefinitions(copy, text);
        WriteAtomic(Path.Combine(ProfilesDirectory, copy.Id + ".tsv"), text);
        if (File.Exists(ReservedPath(id))) WriteAtomic(ReservedPath(copy.Id), File.ReadAllText(ReservedPath(id)));
        WriteAtomic(Path.Combine(ProfilesDirectory, copy.Id + ".json"), JsonSerializer.Serialize(copy));
        return copy;
    }

    internal void RenameProfile(string id, string name)
    {
        var source = KeyProfile(id);
        var profile = source with { Name = ValidateProfileName(name, id), SupportsReserved = source.SupportsReserved ?? IsAzik(source.Name) };
        var active = Read()?.Id == id;
        WriteAtomic(Path.Combine(ProfilesDirectory, id + ".json"), JsonSerializer.Serialize(profile));
        if (active) SelectProfile(id, Enabled);
    }

    internal void DeleteProfile(string id)
    {
        var profile = KeyProfile(id);
        var active = Read()?.Id == id;
        var enabled = Enabled;
        // Soft deletion also stops bundled seeding from recreating the scheme.
        // The table, key overrides and reserved selection remain recoverable.
        WriteAtomic(Path.Combine(ProfilesDirectory, id + ".json"), JsonSerializer.Serialize(profile with { Deleted = true }));
        var bundled = BundledProfilesDirectory();
        if (Directory.Exists(bundled))
            foreach (var builtinFile in Directory.EnumerateFiles(bundled, "*.json"))
            {
                var builtin = JsonSerializer.Deserialize<JapaneseInputTableProfile>(File.ReadAllText(builtinFile));
                if (builtin is null || builtin.Id == id || !string.Equals(builtin.Name, profile.Name, StringComparison.OrdinalIgnoreCase)) continue;
                var duplicatePath = Path.Combine(ProfilesDirectory, builtin.Id + ".json");
                if (File.Exists(duplicatePath))
                {
                    var duplicate = JsonSerializer.Deserialize<JapaneseInputTableProfile>(File.ReadAllText(duplicatePath));
                    if (duplicate is not null && duplicate.Name == profile.Name)
                        WriteAtomic(duplicatePath, JsonSerializer.Serialize(duplicate with { Deleted = true }));
                }
            }
        if (active)
        {
            var replacement = Profiles().FirstOrDefault();
            if (replacement is null) SetEnabled(false);
            else SelectProfile(replacement.Id, enabled);
        }
    }

    internal void SetEnabled(bool enabled)
    {
        if (enabled && Read() is null) throw new InvalidOperationException("请先导入日语按键表。");
        Publish(enabled);
    }

    private void Publish(bool enabled, bool? mixed = null, bool? showLetters = null)
    {
        Directory.CreateDirectory(directory);
        // The settings process and IME service must share one durable source
        // of truth, even when their registry views differ. Publish last after
        // the active table and its metadata have been replaced atomically.
        var useMixed = mixed ?? Mixed;
        var useLetters = showLetters ?? ShowLetters;
        WriteAtomic(StatePath, (enabled ? "1" : "0") + "\n" + Guid.NewGuid().ToString("N") + "\n" + (useMixed ? "1" : "0") + "\n" + (useLetters ? "1" : "0") + "\n");
        using var key = Registry.CurrentUser.CreateSubKey(registryPath)
            ?? throw new InvalidOperationException("无法发布日语按键表。");
        key.SetValue("Option_japanese_input_table_enabled", enabled ? 1 : 0, RegistryValueKind.DWord);
        foreach (var name in new[] { "JapaneseInputTableGeneration", "InputSettingsGeneration",
                                    "AnnotationSettingsGeneration" })
        {
            var previous = key.GetValue(name) is int value ? value : 0;
            key.SetValue(name, unchecked(previous + 1), RegistryValueKind.DWord);
        }
    }

    private static void WriteAtomic(string path, string text)
    {
        var temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        File.WriteAllText(temporary, text, new UTF8Encoding(false));
        File.Move(temporary, path, true);
    }

    internal static List<JapaneseInputRule> Parse(string text)
    {
        var rules = new Dictionary<string, JapaneseInputRule>(StringComparer.Ordinal);
        var lines = text.TrimStart('\uFEFF').Replace("\r\n", "\n").Split('\n');
        for (var index = 0; index < lines.Length; index++)
        {
            var line = lines[index].TrimEnd('\r');
            if (string.IsNullOrWhiteSpace(line) || (line.StartsWith('#') && !line.Contains('\t'))) continue;
            var columns = line.Split('\t');
            if (columns.Length is < 2 or > 3)
                throw new ArgumentException($"第 {index + 1} 行应为 2～3 列，用 Tab 分隔：按键、输出、保留输入。");
            var input = columns[0];
            var output = columns[1];
            var pending = columns.Length == 3 ? columns[2] : "";
            if (input.Length is < 1 or > 64 || input.Any(ch => ch < 33 || ch > 126) ||
                pending.Length > 64 || pending.Any(ch => ch < 33 || ch > 126) ||
                output.Length > 1024 || output.Any(char.IsControl) ||
                (output.Length == 0 && pending.Length == 0))
                throw new ArgumentException($"第 {index + 1} 行包含无效按键、控制字符或空规则。");
            var rule = new JapaneseInputRule(input, output, pending);
            // Mozc replaces an earlier rule with the last row for that key.
            // Some published tables (including Kyuuri) rely on this behavior.
            rules[input] = rule;
            if (rules.Count > 20000) throw new ArgumentException("按键表最多支持 20000 条规则。");
        }
        if (rules.Count == 0) throw new ArgumentException("文件中没有有效规则；请选择罗马字表，不是词典或快捷键表。");
        var result = rules.Values.ToList();
        foreach (var rule in result.Where(rule => rule.Pending.Length > 0)) Convert(rule.Input, result);
        return result;
    }

    internal const string RgtHeader = "# zhongri-mode: rgt-hpk-2018";
    internal static bool IsRgt(string text) => text.TrimStart('\uFEFF').Replace("\r\n", "\n").Split('\n').Contains(RgtHeader);

    internal static JapaneseInputPreview Convert(string input, IReadOnlyList<JapaneseInputRule> rules, bool rgt = false)
    {
        var uppercaseKeys = rules.SelectMany(rule => rule.Input).Where(char.IsAsciiLetterUpper).ToHashSet();
        input = string.Concat(input.Select(c => char.IsAsciiLetterUpper(c) && !uppercaseKeys.Contains(c) ? char.ToLowerInvariant(c) : c));
        var output = new StringBuilder();
        var remaining = input;
        var visited = new HashSet<string>(StringComparer.Ordinal);
        while (remaining.Length > 0)
        {
            // Runtime also inserts this explicit boundary after HPK's timeout.
            if (rgt && remaining[0] == ' ') { remaining = remaining[1..]; continue; }
            if (!visited.Add(remaining) || visited.Count > Math.Max(256, input.Length * 8))
                throw new ArgumentException("按键表的保留输入形成循环，无法使用。");
            var rule = rules.Where(rule => remaining.StartsWith(rule.Input, StringComparison.Ordinal))
                .MaxBy(rule => rule.Input.Length);
            if (rgt && rules.Any(candidate => candidate.Input.Length > remaining.Length &&
                candidate.Input.StartsWith(remaining, StringComparison.Ordinal)) &&
                (rule is null || rule.Input.Length != remaining.Length)) break;
            if (rule is null) break;
            output.Append(rule.Output);
            remaining = rule.Pending + remaining[rule.Input.Length..];
            if (remaining.Length > 4096 || output.Length > 32768)
                throw new ArgumentException("按键表的保留输入持续扩张，无法使用。");
        }
        return new(output.ToString(), remaining);
    }
}
