using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

namespace RimeSettings;

internal sealed record ChineseCorrectionRule(int Number, string Expression, string Title, string Description, bool Enabled);

internal sealed class ChineseCorrectionStore
{
    private static readonly (string Title, string Description)[] Friendly =
    [
        ("zh/ch/sh ↔ hzh/hch/hsh", "h 键位置提前的纠错"),
        ("zha/cha/sha ↔ zah/cah/sah", "h 键位置延后的纠错"),
        ("ai ↔ ia", "适用于 w、g、h、k 开头"),
        ("ei ↔ ie", "适用于 w、f、g、h、k、z 开头"),
        ("ie ↔ ei", "适用于 j、q、x 开头"),
        ("ao ↔ oa", "常见韵母顺序纠错"),
        ("ou ↔ uo", "适用于 y、p、f、m 开头"),
        ("ang ↔ nag", "n 键提前"),
        ("ang ↔ agn", "n/g 键顺序纠错"),
        ("eng ↔ neg", "n 键提前"),
        ("eng ↔ egn", "n/g 键顺序纠错"),
        ("ing ↔ nig", "n 键提前"),
        ("ing ↔ ign", "n/g 键顺序纠错"),
        ("ong ↔ nog", "n 键提前"),
        ("ong ↔ ogn", "n/g 键顺序纠错"),
        ("iao ↔ ioa", "a/o 键顺序纠错"),
        ("iao ↔ oia", "i/a/o 键顺序纠错"),
        ("ui ↔ iu", "适用于部分声母"),
        ("iu ↔ ui", "适用于 j、q、x 等声母"),
        ("iang ↔ aing", "韵母键位顺序纠错"),
        ("iang ↔ inag", "韵母键位顺序纠错"),
        ("ua ↔ au", "适用于 g、k、h、zh、sh"),
        ("uai ↔ aui", "适用于 g、k、h、zh、ch、sh"),
        ("uan ↔ aun", "韵母键位顺序纠错"),
        ("ue ↔ eu", "适用于 n、l、y、j、q、x"),
        ("uang ↔ aung", "韵母键位顺序纠错"),
        ("uang ↔ uagn", "韵母键位顺序纠错"),
        ("uang ↔ unag", "韵母键位顺序纠错"),
        ("uang ↔ augn", "韵母键位顺序纠错"),
        ("iong ↔ inog", "韵母键位顺序纠错"),
        ("iong ↔ oing", "韵母键位顺序纠错"),
        ("iong ↔ iogn", "韵母键位顺序纠错"),
        ("iong ↔ oign", "韵母键位顺序纠错"),
        ("ou/ong → o", "例如 shou → sho；中日混输易冲突，默认关闭"),
        ("ong ↔ on", "省略末尾 g"),
        ("eng ↔ en", "仅适用于 t、l 开头"),
        ("ang/eng/ing/ong → ng", "省略韵母中的元音键")
    ];
    private readonly string _schema;
    private static readonly Regex Active = new(@"^\s*-\s+(derive|abbrev|erase)/.+/$");
    private static readonly Regex Disabled = new(@"^\s*#\s*CF_OFF\s+-\s+(derive|abbrev|erase)/.+/$");

    public ChineseCorrectionStore(string rimeDirectory) =>
        _schema = Path.Combine(rimeDirectory, "rime_ice_japanese.schema.yaml");

    public List<ChineseCorrectionRule> Load()
    {
        var result = new List<ChineseCorrectionRule>();
        var inside = false;
        foreach (var line in File.ReadLines(_schema, Encoding.UTF8))
        {
            if (line.Contains("### 自动纠错")) { inside = true; continue; }
            if (inside && line.StartsWith("menu:")) break;
            if (!inside || (!Active.IsMatch(line) && !Disabled.IsMatch(line))) continue;
            var enabled = Active.IsMatch(line);
            var expression = Regex.Replace(line.Trim(), @"^#\s*CF_OFF\s+", "");
            var number = result.Count + 1;
            var friendly = number <= Friendly.Length
                ? Friendly[number - 1]
                : (Title: $"规则 {number}", Description: "中文自动纠错");
            result.Add(new(number, expression, friendly.Title, friendly.Description, enabled));
        }
        return result;
    }

    public void Save(IReadOnlyCollection<ChineseCorrectionRule> rules)
    {
        var wanted = rules.ToDictionary(x => x.Expression, x => x.Enabled);
        var lines = File.ReadAllLines(_schema, Encoding.UTF8);
        var inside = false;
        for (var i = 0; i < lines.Length; i++)
        {
            if (lines[i].Contains("### 自动纠错")) { inside = true; continue; }
            if (inside && lines[i].StartsWith("menu:")) break;
            if (!inside || (!Active.IsMatch(lines[i]) && !Disabled.IsMatch(lines[i]))) continue;
            var indent = new string(' ', lines[i].TakeWhile(char.IsWhiteSpace).Count());
            var expression = Regex.Replace(lines[i].Trim(), @"^#\s*CF_OFF\s+", "");
            if (wanted.TryGetValue(expression, out var enabled))
                lines[i] = indent + (enabled ? expression : "# CF_OFF " + expression);
        }
        File.Copy(_schema, _schema + ".before_chinese_correction", true);
        File.WriteAllLines(_schema, lines, new UTF8Encoding(false));
    }
}
