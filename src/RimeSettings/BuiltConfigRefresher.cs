using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

namespace RimeSettings;

/// <summary>
/// Updates the already compiled, scalar-only runtime settings without asking
/// librime to rebuild dictionaries.  These files are generated caches; source
/// custom YAML is still written first by <see cref="SettingsStore"/>.
/// </summary>
internal static class BuiltConfigRefresher
{
    public static void RefreshAppearance(string rimeDirectory, AppearanceOptions options)
    {
        var colors = new Dictionary<string, string>
        {
            ["preset_color_schemes/android/chinese_candidate_text_color"] = ToAbgr(options.ChineseText),
            ["preset_color_schemes/android/chinese_candidate_back_color"] = ToAbgr(options.ChineseBackground),
            ["preset_color_schemes/android/japanese_candidate_text_color"] = ToAbgr(options.JapaneseText),
            ["preset_color_schemes/android/japanese_candidate_back_color"] = ToAbgr(options.JapaneseBackground),
            ["preset_color_schemes/android/common_phrase_candidate_text_color"] = ToAbgr(options.CommonPhraseText),
            ["preset_color_schemes/android/common_phrase_candidate_back_color"] = ToAbgr(options.CommonPhraseBackground),
            ["style/layout/min_width"] = options.Width.ToString(),
            ["style/layout/max_height"] = "600",
            ["style/comment_font_point"] = options.CommentSize.ToString(),
            ["style/layout/candidate_spacing"] = options.CandidateSpacing.ToString(),
            ["style/layout/hilite_padding"] = options.HighlightPadding.ToString()
        };
        Refresh(Path.Combine(rimeDirectory, "build", "weasel.yaml"), colors);
    }

    public static void RefreshInputLayout(string rimeDirectory, InputOptions options, int rareThreshold)
    {
        Refresh(Path.Combine(rimeDirectory, "build", "weasel.yaml"), new Dictionary<string, string>
        {
            ["style/inline_preedit"] = Bool(options.InlinePreedit),
            ["style/inline_preedit_raw_input"] = Bool(options.InlinePreeditRawInput),
            ["style/expanded_comment_width"] = Bool(options.ExpandedCommentWidth),
            ["style/expanded_comment_align_label"] = Bool(options.ExpandedCommentAlignLabel)
        });
        Refresh(Path.Combine(rimeDirectory, "build", "rime_ice_japanese.schema.yaml"),
            new Dictionary<string, string>
            {
                ["space_commit_raw/select_first"] = Bool(options.SpaceSelectFirst),
                ["space_commit_raw/enter_submits_to_app"] = Bool(options.EnterSubmitsToApp),
                ["space_commit_raw/reading_preview"] = Bool(options.SpaceReadingPreview),
                ["rare_single_char_filter/frequency_threshold"] = rareThreshold.ToString()
            });
    }

    private static string Bool(bool value) => value ? "true" : "false";

    private static string ToAbgr(Color value)
    {
        uint abgr = ((uint)value.A << 24) | ((uint)value.B << 16) |
                    ((uint)value.G << 8) | value.R;
        return $"0x{abgr:X8}";
    }

    private static void Refresh(string file, IReadOnlyDictionary<string, string> values)
    {
        if (!File.Exists(file))
            throw new FileNotFoundException("尚未生成运行配置，请先在“维护”页执行一次完整部署。", file);

        var original = File.ReadAllText(file, Encoding.UTF8);
        var updated = original;
        foreach (var pair in values)
        {
            var path = pair.Key.Split('/');
            updated = ReplaceScalar(updated, path, pair.Value, out var found);
            if (!found)
                throw new InvalidDataException($"运行配置缺少设置项：{pair.Key}。请在“维护”页执行一次完整部署。 ");
        }

        if (updated == original) return;
        File.Copy(file, file + ".before_light_refresh", true);
        File.WriteAllText(file, updated, new UTF8Encoding(false));
    }

    internal static string ReplaceScalar(string yaml, IReadOnlyList<string> path,
                                         string value, out bool found)
    {
        var newline = yaml.Contains("\r\n", StringComparison.Ordinal) ? "\r\n" : "\n";
        var lines = Regex.Split(yaml, "\r?\n");
        var parents = new List<(int Indent, string Key)>();
        found = false;

        for (var i = 0; i < lines.Length; i++)
        {
            var match = Regex.Match(lines[i], @"^(?<space> *)(?<key>[A-Za-z0-9_]+):(?<rest>.*)$");
            if (!match.Success) continue;
            var indent = match.Groups["space"].Length;
            while (parents.Count > 0 && parents[^1].Indent >= indent) parents.RemoveAt(parents.Count - 1);

            var key = match.Groups["key"].Value;
            if (key == path[^1] && parents.Select(x => x.Key).SequenceEqual(path.Take(path.Count - 1)))
            {
                lines[i] = match.Groups["space"].Value + key + ": " + value;
                found = true;
                break;
            }

            if (string.IsNullOrWhiteSpace(match.Groups["rest"].Value))
                parents.Add((indent, key));
        }
        return string.Join(newline, lines);
    }
}
