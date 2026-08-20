using System;
using System.IO;
using System.Drawing;
using System.Text;
using System.Text.RegularExpressions;

namespace RimeSettings;

internal sealed record InputOptions(
    bool English, bool Japanese, bool ExpandedCommentWidth, bool Fuzzy,
    bool FuzzySokuon, bool FuzzyLongI, bool FuzzyLongU,
    bool FuzzyLongMark, bool FuzzyChiJi, bool FuzzyHuFu,
    bool FuzzyShuSho, bool FuzzyKeKai, bool FuzzyKeKaeGae,
    bool FuzzySeiSai, bool FuzzyDakuten, bool Sentence);
internal sealed record AppearanceOptions(
    int Width, int CommentSize, int CandidateSpacing, int HighlightPadding,
    Color ChineseText, Color ChineseBackground,
    Color JapaneseText, Color JapaneseBackground);

internal sealed class SettingsStore
{
    public string RimeDirectory { get; }
    public string UserYaml => Path.Combine(RimeDirectory, "user.yaml");
    public string WeaselCustomYaml => Path.Combine(RimeDirectory, "weasel.custom.yaml");
    public string JapaneseSchemaYaml => Path.Combine(RimeDirectory, "rime_ice_japanese.schema.yaml");
    public string JapaneseSchemaCustomYaml => Path.Combine(RimeDirectory, "rime_ice_japanese.custom.yaml");

    public SettingsStore(string rimeDirectory)
    {
        RimeDirectory = rimeDirectory;
    }

    public InputOptions ReadInputOptions() => new(
        ReadOption("show_english_annotation", true),
        ReadOption("show_japanese_annotation", true),
        ReadPatchBool("style/expanded_comment_width", true),
        ReadOption("japanese_fuzzy_match", false),
        ReadOption("japanese_fuzzy_sokuon", true),
        ReadOption("japanese_fuzzy_long_i", true),
        ReadOption("japanese_fuzzy_long_u", true),
        ReadOption("japanese_fuzzy_long_mark", true),
        ReadOption("japanese_fuzzy_chi_ji", true),
        ReadOption("japanese_fuzzy_hu_fu", true),
        ReadOption("japanese_fuzzy_shu_sho", true),
        ReadOption("japanese_fuzzy_ke_kai", true),
        ReadOption("japanese_fuzzy_ke_kae_gae", true),
        ReadOption("japanese_fuzzy_sei_sai", true),
        ReadOption("japanese_fuzzy_dakuten", true),
        ReadOption("sentence_translation", false));

    public void SaveInputOptions(InputOptions options)
    {
        Directory.CreateDirectory(RimeDirectory);
        var text = File.Exists(UserYaml) ? File.ReadAllText(UserYaml, Encoding.UTF8) : "var:\n";
        if (!Regex.IsMatch(text, @"(?m)^  option:\s*$"))
        {
            if (Regex.IsMatch(text, @"(?m)^var:\s*$"))
                text = new Regex(@"(?m)^var:\s*$").Replace(text, "var:\n  option:", 1);
            else
                text = text.TrimEnd() + "\nvar:\n  option:\n";
        }
        text = SetOption(text, "show_english_annotation", options.English);
        text = SetOption(text, "show_japanese_annotation", options.Japanese);
        text = SetOption(text, "japanese_fuzzy_match", options.Fuzzy);
        text = SetOption(text, "japanese_fuzzy_sokuon", options.FuzzySokuon);
        text = SetOption(text, "japanese_fuzzy_long_i", options.FuzzyLongI);
        text = SetOption(text, "japanese_fuzzy_long_u", options.FuzzyLongU);
        text = SetOption(text, "japanese_fuzzy_long_mark", options.FuzzyLongMark);
        text = SetOption(text, "japanese_fuzzy_chi_ji", options.FuzzyChiJi);
        text = SetOption(text, "japanese_fuzzy_hu_fu", options.FuzzyHuFu);
        text = SetOption(text, "japanese_fuzzy_shu_sho", options.FuzzyShuSho);
        text = SetOption(text, "japanese_fuzzy_ke_kai", options.FuzzyKeKai);
        text = SetOption(text, "japanese_fuzzy_ke_kae_gae", options.FuzzyKeKaeGae);
        text = SetOption(text, "japanese_fuzzy_sei_sai", options.FuzzySeiSai);
        text = SetOption(text, "japanese_fuzzy_dakuten", options.FuzzyDakuten);
        text = SetOption(text, "sentence_translation", options.Sentence);
        File.WriteAllText(UserYaml, text.TrimEnd() + "\n", new UTF8Encoding(false));

        var weaselText = File.Exists(WeaselCustomYaml)
            ? File.ReadAllText(WeaselCustomYaml, Encoding.UTF8)
            : "patch:\n";
        if (!Regex.IsMatch(weaselText, @"(?m)^patch:\s*$"))
            weaselText = weaselText.TrimEnd() + "\n\npatch:\n";
        weaselText = SetPatchBool(weaselText, "style/expanded_comment_width",
                                  options.ExpandedCommentWidth);
        File.WriteAllText(WeaselCustomYaml, weaselText.TrimEnd() + "\n",
                          new UTF8Encoding(false));
    }

    public int ReadRareSingleCharThreshold()
    {
        const int fallback = 4000;
        if (File.Exists(JapaneseSchemaCustomYaml))
        {
            var custom = File.ReadAllText(JapaneseSchemaCustomYaml, Encoding.UTF8);
            var patched = Regex.Match(custom,
                @"(?m)^\s*[""']?rare_single_char_filter/frequency_threshold[""']?\s*:\s*(\d+)\s*$");
            if (patched.Success && int.TryParse(patched.Groups[1].Value, out var value))
                return value;
        }
        if (File.Exists(JapaneseSchemaYaml))
        {
            var schema = File.ReadAllText(JapaneseSchemaYaml, Encoding.UTF8);
            var configured = Regex.Match(schema,
                @"(?ms)^rare_single_char_filter:\s*.*?^\s+frequency_threshold:\s*(\d+)\s*$");
            if (configured.Success && int.TryParse(configured.Groups[1].Value, out var value))
                return value;
        }
        return fallback;
    }

    public void SaveRareSingleCharThreshold(int threshold)
    {
        Directory.CreateDirectory(RimeDirectory);
        var text = File.Exists(JapaneseSchemaCustomYaml)
            ? File.ReadAllText(JapaneseSchemaCustomYaml, Encoding.UTF8)
            : "patch:\n";
        if (!Regex.IsMatch(text, @"(?m)^patch:\s*$"))
            text = text.TrimEnd() + "\n\npatch:\n";
        text = SetPatchInt(text, "rare_single_char_filter/frequency_threshold", threshold);
        File.WriteAllText(JapaneseSchemaCustomYaml, text.TrimEnd() + "\n", new UTF8Encoding(false));
    }

    public AppearanceOptions ReadAppearance() => new(
        ReadPatchInt("style/layout/min_width", 600),
        ReadPatchInt("style/comment_font_point", 11),
        ReadPatchInt("style/layout/candidate_spacing", 16),
        ReadPatchInt("style/layout/hilite_padding", 8),
        ReadPatchColor("preset_color_schemes/android/chinese_candidate_text_color", Color.FromArgb(255, 242, 242, 242)),
        ReadPatchColor("preset_color_schemes/android/chinese_candidate_back_color", Color.FromArgb(0, 0, 0, 0)),
        ReadPatchColor("preset_color_schemes/android/japanese_candidate_text_color", Color.FromArgb(255, 128, 203, 196)),
        ReadPatchColor("preset_color_schemes/android/japanese_candidate_back_color", Color.FromArgb(0, 0, 0, 0)));

    public void SaveAppearance(AppearanceOptions options)
    {
        Directory.CreateDirectory(RimeDirectory);
        var text = File.Exists(WeaselCustomYaml)
            ? File.ReadAllText(WeaselCustomYaml, Encoding.UTF8)
            : "patch:\n";
        if (!Regex.IsMatch(text, @"(?m)^patch:\s*$"))
            text = text.TrimEnd() + "\n\npatch:\n";
        text = SetPatchInt(text, "style/layout/min_width", options.Width);
        text = SetPatchInt(text, "style/layout/max_height", 600);
        text = SetPatchInt(text, "style/comment_font_point", options.CommentSize);
        text = SetPatchInt(text, "style/layout/candidate_spacing", options.CandidateSpacing);
        text = SetPatchInt(text, "style/layout/hilite_padding", options.HighlightPadding);
        text = SetPatchColor(text, "preset_color_schemes/android/chinese_candidate_text_color", options.ChineseText);
        text = SetPatchColor(text, "preset_color_schemes/android/chinese_candidate_back_color", options.ChineseBackground);
        text = SetPatchColor(text, "preset_color_schemes/android/japanese_candidate_text_color", options.JapaneseText);
        text = SetPatchColor(text, "preset_color_schemes/android/japanese_candidate_back_color", options.JapaneseBackground);
        File.WriteAllText(WeaselCustomYaml, text.TrimEnd() + "\n", new UTF8Encoding(false));
    }

    private bool ReadOption(string key, bool fallback)
    {
        if (!File.Exists(UserYaml)) return fallback;
        var text = File.ReadAllText(UserYaml, Encoding.UTF8);
        var match = Regex.Match(text, $@"(?m)^\s+{Regex.Escape(key)}:\s*(true|false)\s*$");
        return match.Success ? match.Groups[1].Value == "true" : fallback;
    }

    private int ReadPatchInt(string key, int fallback)
    {
        if (!File.Exists(WeaselCustomYaml)) return fallback;
        var text = File.ReadAllText(WeaselCustomYaml, Encoding.UTF8);
        var match = Regex.Match(text, $@"(?m)^\s*[""']?{Regex.Escape(key)}[""']?\s*:\s*(-?\d+)\s*$");
        return match.Success && int.TryParse(match.Groups[1].Value, out var value) ? value : fallback;
    }

    private bool ReadPatchBool(string key, bool fallback)
    {
        if (!File.Exists(WeaselCustomYaml)) return fallback;
        var text = File.ReadAllText(WeaselCustomYaml, Encoding.UTF8);
        var match = Regex.Match(text,
            $@"(?m)^\s*[""']?{Regex.Escape(key)}[""']?\s*:\s*(true|false)\s*$");
        return match.Success ? match.Groups[1].Value == "true" : fallback;
    }

    private Color ReadPatchColor(string key, Color fallback)
    {
        if (!File.Exists(WeaselCustomYaml)) return fallback;
        var text = File.ReadAllText(WeaselCustomYaml, Encoding.UTF8);
        var match = Regex.Match(text, $@"(?m)^\s*[""']?{Regex.Escape(key)}[""']?\s*:\s*0x([0-9a-fA-F]{{8}})\s*$");
        if (!match.Success || !uint.TryParse(match.Groups[1].Value,
                System.Globalization.NumberStyles.HexNumber, null, out var abgr)) return fallback;
        return Color.FromArgb((byte)(abgr >> 24), (byte)abgr,
                              (byte)(abgr >> 8), (byte)(abgr >> 16));
    }

    private static string SetOption(string text, string key, bool value)
    {
        var pattern = $@"(?m)^    {Regex.Escape(key)}:\s*(true|false)\s*$";
        var replacement = $"    {key}: {value.ToString().ToLowerInvariant()}";
        return Regex.IsMatch(text, pattern)
            ? new Regex(pattern).Replace(text, replacement, 1)
            : new Regex(@"(?m)^  option:\s*$").Replace(text, $"  option:\n{replacement}", 1);
    }

    private static string SetPatchInt(string text, string key, int value)
    {
        var pattern = $@"(?m)^\s*[""']?{Regex.Escape(key)}[""']?\s*:\s*-?\d+\s*$";
        var replacement = $"  \"{key}\": {value}";
        return Regex.IsMatch(text, pattern)
            ? new Regex(pattern).Replace(text, replacement, 1)
            : new Regex(@"(?m)^patch:\s*$").Replace(text, $"patch:\n{replacement}", 1);
    }

    private static string SetPatchBool(string text, string key, bool value)
    {
        var pattern = $@"(?m)^\s*[""']?{Regex.Escape(key)}[""']?\s*:\s*(true|false)\s*$";
        var replacement = $"  \"{key}\": {value.ToString().ToLowerInvariant()}";
        return Regex.IsMatch(text, pattern)
            ? new Regex(pattern).Replace(text, replacement, 1)
            : new Regex(@"(?m)^patch:\s*$").Replace(text, $"patch:\n{replacement}", 1);
    }

    private static string SetPatchColor(string text, string key, Color value)
    {
        uint abgr = ((uint)value.A << 24) | ((uint)value.B << 16) |
                    ((uint)value.G << 8) | value.R;
        var pattern = $@"(?m)^\s*[""']?{Regex.Escape(key)}[""']?\s*:\s*0x[0-9a-fA-F]{{8}}\s*$";
        var replacement = $"  \"{key}\": 0x{abgr:X8}";
        return Regex.IsMatch(text, pattern)
            ? new Regex(pattern).Replace(text, replacement, 1)
            : new Regex(@"(?m)^patch:\s*$").Replace(text, $"patch:\n{replacement}", 1);
    }
}
