using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

namespace RimeSettings;

internal enum FuzzyLanguage { Japanese, Chinese }

internal sealed record CustomFuzzyRule(bool Enabled, string Left, string Right);

internal sealed class CustomFuzzyRuleStore
{
    private const string ChineseBegin = "    # CF_CUSTOM_BEGIN";
    private const string ChineseEnd = "    # CF_CUSTOM_END";
    private static readonly Regex Roman = new("^[a-z]{1,16}$", RegexOptions.Compiled);
    private readonly string _rimeDirectory;
    private readonly string _schema;

    public CustomFuzzyRuleStore(string rimeDirectory)
    {
        _rimeDirectory = rimeDirectory;
        _schema = Path.Combine(rimeDirectory, "rime_ice_japanese.schema.yaml");
    }

    public string FilePath(FuzzyLanguage language) => Path.Combine(_rimeDirectory,
        language == FuzzyLanguage.Japanese
            ? "custom_japanese_fuzzy.tsv"
            : "custom_chinese_fuzzy.tsv");

    public List<CustomFuzzyRule> Load(FuzzyLanguage language)
    {
        var result = new List<CustomFuzzyRule>();
        var path = FilePath(language);
        if (!File.Exists(path)) return result;
        foreach (var line in File.ReadLines(path, Encoding.UTF8))
        {
            if (string.IsNullOrWhiteSpace(line) || line.StartsWith('#')) continue;
            var cells = line.Split('\t');
            if (cells.Length < 3) continue;
            var left = Normalize(cells[1]);
            var right = Normalize(cells[2]);
            if (!IsValid(left, right)) continue;
            result.Add(new(cells[0] != "0", left, right));
        }
        return result;
    }

    public void Save(FuzzyLanguage language, IEnumerable<CustomFuzzyRule> source)
    {
        Directory.CreateDirectory(_rimeDirectory);
        var rules = source
            .Select(x => x with { Left = Normalize(x.Left), Right = Normalize(x.Right) })
            .Where(x => IsValid(x.Left, x.Right))
            .DistinctBy(x => (x.Left, x.Right))
            .ToList();
        var lines = new List<string> { "# enabled\tleft\tright (bidirectional)" };
        lines.AddRange(rules.Select(x => $"{(x.Enabled ? 1 : 0)}\t{x.Left}\t{x.Right}"));
        File.WriteAllLines(FilePath(language), lines, new UTF8Encoding(false));
        if (language == FuzzyLanguage.Chinese) UpdateChineseSchema(rules);
    }

    public static string Normalize(string value) =>
        Regex.Replace((value ?? "").Trim().ToLowerInvariant(), "\\s+", "");

    public static bool IsValid(string left, string right) =>
        Roman.IsMatch(left) && Roman.IsMatch(right) && left != right;

    private void UpdateChineseSchema(IReadOnlyCollection<CustomFuzzyRule> rules)
    {
        if (!File.Exists(_schema)) throw new FileNotFoundException("找不到中日方案文件。", _schema);
        var text = File.ReadAllText(_schema, Encoding.UTF8);
        var generated = new StringBuilder()
            .AppendLine(ChineseBegin)
            .AppendLine("    ### 用户自定义中文模糊匹配；仅允许字面罗马字，双向生成。")
            .AppendLine("    ### 由图形设置面板维护，请勿手工编辑本区块。");
        foreach (var rule in rules.Where(x => x.Enabled))
        {
            generated.AppendLine($"    - derive/(.*){rule.Left}(.*)/$1{rule.Right}$2/");
            generated.AppendLine($"    - derive/(.*){rule.Right}(.*)/$1{rule.Left}$2/");
        }
        generated.Append(ChineseEnd);

        var block = $"(?ms)^\\s*# CF_CUSTOM_BEGIN.*?^\\s*# CF_CUSTOM_END\\s*";
        if (Regex.IsMatch(text, block))
            text = Regex.Replace(text, block, generated + Environment.NewLine);
        else
        {
            var marker = "\nmenu:";
            var index = text.IndexOf(marker, StringComparison.Ordinal);
            if (index < 0) throw new InvalidDataException("方案文件缺少 menu: 插入点。");
            text = text.Insert(index, "\n" + generated + "\n");
        }
        File.Copy(_schema, _schema + ".before_custom_fuzzy", true);
        File.WriteAllText(_schema, text, new UTF8Encoding(false));
    }
}
