using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Drawing;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using Microsoft.Win32;

namespace RimeSettings;

internal sealed record InputOptions(
    bool English, bool Japanese, bool DirectJapaneseReading, bool SingleCharacterAnnotations,
    bool EmojiSecondPosition,
    bool JapanesePrefixCompletion, bool JapanesePrefixCompletionSuffix,
    bool JapanesePrefixCompletionJapaneseFirst,
    bool InlinePreedit, bool InlinePreeditRawInput,
    bool EnterSubmitsToApp, bool SpaceSelectFirst, bool SpaceReadingPreview, bool LongPressReadingPreview,
    bool AltReadingPreview,
    bool ExpandedCommentWidth, bool ExpandedCommentAlignLabel, bool JapaneseContinuationLock, bool Fuzzy,
    bool FuzzySokuon, bool FuzzyLongI, bool FuzzyLongU,
    bool FuzzyLongMark, bool FuzzyChiJi, bool FuzzyHuFu,
    bool FuzzyShuSho, bool FuzzyKeKai, bool FuzzyKeKaeGae,
    bool FuzzySeiSai, bool FuzzyDakuten, bool ParticleWaHa, bool Sentence,
    bool GoogleCloudCandidates);
internal sealed record AppearanceOptions(
    int Width, int CommentSize, int CandidateSpacing, int HighlightPadding,
    Color ChineseText, Color ChineseBackground,
    Color JapaneseText, Color JapaneseBackground,
    Color CommonPhraseText, Color CommonPhraseBackground);

internal sealed class SettingsStore
{
    public string RimeDirectory { get; }
    public string UserYaml => Path.Combine(RimeDirectory, "user.yaml");
    public string WeaselCustomYaml => Path.Combine(RimeDirectory, "weasel.custom.yaml");
    public string JapaneseSchemaYaml => Path.Combine(RimeDirectory, "rime_ice_japanese.schema.yaml");
    public string JapaneseSchemaCustomYaml => Path.Combine(RimeDirectory, "rime_ice_japanese.custom.yaml");
    public string EmojiSecondPositionFile => Path.Combine(RimeDirectory, "emoji_second_position.txt");

    private const string NiuRegistryPath = @"Software\ZhongriInputMethod";

    public bool HasNiuCredentials()
    {
        using var key = Registry.CurrentUser.OpenSubKey(NiuRegistryPath);
        return key?.GetValue("NiuApiKeyProtected") is byte[] bytes && bytes.Length > 0 &&
               key.GetValue("NiuAppId") is string appId && appId.Length > 0;
    }

    public void SaveNiuCredentials(string appId, string apiKey)
    {
        appId = appId.Trim();
        apiKey = apiKey.Trim();
        if (appId.Length == 0 || appId.Length > 128 || appId.Any(char.IsWhiteSpace) ||
            apiKey.Length == 0 || apiKey.Length > 512 || apiKey.Any(char.IsWhiteSpace))
            throw new ArgumentException("App ID 和 API Key 都不能为空，也不能包含空格。");
        var plain = Encoding.UTF8.GetBytes(apiKey);
        try
        {
            var protectedKey = ProtectedData.Protect(plain, null, DataProtectionScope.CurrentUser);
            using var key = Registry.CurrentUser.CreateSubKey(NiuRegistryPath)
                ?? throw new InvalidOperationException("无法保存 API Key。");
            key.SetValue("NiuAppId", appId, RegistryValueKind.String);
            key.SetValue("NiuApiKeyProtected", protectedKey, RegistryValueKind.Binary);
        }
        finally { CryptographicOperations.ZeroMemory(plain); }
    }

    public void ClearNiuCredentials()
    {
        using var key = Registry.CurrentUser.OpenSubKey(NiuRegistryPath, writable: true);
        key?.DeleteValue("NiuApiKeyProtected", throwOnMissingValue: false);
        key?.DeleteValue("NiuAppId", throwOnMissingValue: false);
    }

    public async Task<string> TestNiuConnectionAsync()
    {
        using var registry = Registry.CurrentUser.OpenSubKey(NiuRegistryPath);
        if (registry?.GetValue("NiuAppId") is not string appId ||
            registry.GetValue("NiuApiKeyProtected") is not byte[] protectedKey)
            throw new InvalidOperationException("请先保存 App ID 和 API Key。");
        var keyBytes = ProtectedData.Unprotect(protectedKey, null, DataProtectionScope.CurrentUser);
        try
        {
            var apiKey = Encoding.UTF8.GetString(keyBytes);
            const string source = "你好";
            using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(5) };
            async Task<string> TranslateAsync(string target)
            {
                var timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds().ToString();
                var signed = $"apikey={apiKey}&appId={appId}&from=zh&srcText={source}&timestamp={timestamp}&to={target}";
                var auth = Convert.ToHexString(MD5.HashData(Encoding.UTF8.GetBytes(signed))).ToLowerInvariant();
                using var body = new FormUrlEncodedContent(new Dictionary<string, string>
                {
                    ["from"] = "zh", ["to"] = target, ["appId"] = appId,
                    ["timestamp"] = timestamp, ["srcText"] = source, ["authStr"] = auth
                });
                using var response = await client.PostAsync("https://api.niutrans.com/v2/text/translate", body);
                response.EnsureSuccessStatusCode();
                using var result = JsonDocument.Parse(await response.Content.ReadAsStringAsync());
                if (result.RootElement.TryGetProperty("tgtText", out var translated) &&
                    translated.ValueKind == JsonValueKind.String &&
                    !string.IsNullOrWhiteSpace(translated.GetString()))
                    return translated.GetString()!;
                var code = result.RootElement.TryGetProperty("errorCode", out var error)
                    ? error.ToString() : "未知错误";
                throw new InvalidOperationException($"小牛 {target} 未返回译文，错误码：{code}");
            }
            var translations = await Task.WhenAll(TranslateAsync("ja"), TranslateAsync("en"));
            return string.Join(" / ", translations);
        }
        finally { CryptographicOperations.ZeroMemory(keyBytes); }
    }

    public SettingsStore(string rimeDirectory)
    {
        RimeDirectory = rimeDirectory;
    }

    public InputOptions ReadInputOptions() => new(
        ReadOption("show_english_annotation", true),
        ReadOption("show_japanese_annotation", true),
        ReadOption("show_direct_japanese_reading", false),
        ReadOption("show_single_character_annotation", true),
        ReadEmojiSecondPosition(),
        !ReadOption("japanese_prefix_completion_disabled", true),
        ReadOption("japanese_prefix_completion_suffix", false),
        ReadOption("japanese_prefix_completion_japanese_first", true),
        ReadPatchBool("style/inline_preedit", true),
        ReadPatchBool("style/inline_preedit_raw_input", true),
        ReadSchemaPatchBool("space_commit_raw/enter_submits_to_app", false),
        ReadSchemaPatchBool("space_commit_raw/select_first", false),
        ReadSchemaPatchBool("space_commit_raw/reading_preview", false),
        ReadRegistryBool("LongPressReadingPreview", false),
        ReadRegistryBool("AltReadingPreview", false),
        ReadPatchBool("style/expanded_comment_width", false),
        ReadPatchBool("style/expanded_comment_align_label", true),
        ReadOption("japanese_continuation_lock", true),
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
        ReadOption("japanese_particle_wa_ha", false),
        ReadOption("sentence_translation", false),
        ReadOption("google_cloud_candidates", false));

    public void SaveAnnotationOptions(bool english, bool japanese,
                                      bool directJapaneseReading, bool singleCharacterAnnotations)
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
        text = SetOption(text, "show_english_annotation", english);
        text = SetOption(text, "show_japanese_annotation", japanese);
        text = SetOption(text, "show_direct_japanese_reading", directJapaneseReading);
        text = SetOption(text, "show_single_character_annotation", singleCharacterAnnotations);
        WriteUtf8IfChanged(UserYaml, text.TrimEnd() + "\n");
        PublishAnnotationOptions(english, japanese, directJapaneseReading, singleCharacterAnnotations);
    }

    private static void PublishAnnotationOptions(bool english, bool japanese,
                                                 bool directJapaneseReading, bool singleCharacterAnnotations)
    {
        using var key = Registry.CurrentUser.CreateSubKey(@"Software\Rime\Weasel");
        if (key is null) throw new InvalidOperationException("无法更新日英注释开关。");
        key.SetValue("ShowEnglishAnnotation", english ? 1 : 0, RegistryValueKind.DWord);
        key.SetValue("ShowJapaneseAnnotation", japanese ? 1 : 0, RegistryValueKind.DWord);
        key.SetValue("ShowDirectJapaneseReading", directJapaneseReading ? 1 : 0, RegistryValueKind.DWord);
        key.SetValue("ShowSingleCharacterAnnotations", singleCharacterAnnotations ? 1 : 0, RegistryValueKind.DWord);
        // Publish last: already-open TSF candidate windows watch this revision.
        var previous = key.GetValue("AnnotationSettingsGeneration") is int value ? value : 0;
        key.SetValue("AnnotationSettingsGeneration", unchecked(previous + 1), RegistryValueKind.DWord);
    }

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
        text = SetOption(text, "show_direct_japanese_reading", options.DirectJapaneseReading);
        text = SetOption(text, "show_single_character_annotation", options.SingleCharacterAnnotations);
        text = SetOption(text, "japanese_prefix_completion_disabled", !options.JapanesePrefixCompletion);
        text = SetOption(text, "japanese_prefix_completion_suffix", options.JapanesePrefixCompletionSuffix);
        text = SetOption(text, "japanese_prefix_completion_japanese_first", options.JapanesePrefixCompletionJapaneseFirst);
        text = SetOption(text, "japanese_continuation_lock", options.JapaneseContinuationLock);
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
        text = SetOption(text, "japanese_particle_wa_ha", options.ParticleWaHa);
        text = SetOption(text, "sentence_translation", options.Sentence);
        text = SetOption(text, "google_cloud_candidates", options.GoogleCloudCandidates);
        WriteUtf8IfChanged(UserYaml, text.TrimEnd() + "\n");
        PublishAnnotationOptions(options.English, options.Japanese,
                                 options.DirectJapaneseReading, options.SingleCharacterAnnotations);
        WriteUtf8IfChanged(EmojiSecondPositionFile,
                           options.EmojiSecondPosition ? "1\n" : "0\n");

        var schemaText = File.Exists(JapaneseSchemaCustomYaml)
            ? File.ReadAllText(JapaneseSchemaCustomYaml, Encoding.UTF8)
            : "patch:\n";
        if (!Regex.IsMatch(schemaText, @"(?m)^patch:\s*$"))
            schemaText = schemaText.TrimEnd() + "\n\npatch:\n";
        schemaText = SetPatchBool(schemaText, "space_commit_raw/select_first",
                                  options.SpaceSelectFirst);
        schemaText = SetPatchBool(schemaText, "space_commit_raw/enter_submits_to_app",
                                  options.EnterSubmitsToApp);
        schemaText = SetPatchBool(schemaText, "space_commit_raw/reading_preview",
                                  options.SpaceReadingPreview);
        WriteUtf8IfChanged(JapaneseSchemaCustomYaml, schemaText.TrimEnd() + "\n");

        // TSF must know the mode before Rime processes the Space key.  A small
        // per-user registry value avoids file IO on every keystroke.
        using (var key = Registry.CurrentUser.CreateSubKey(@"Software\Rime\Weasel"))
        {
            key?.SetValue("SpaceReadingPreview", options.SpaceReadingPreview ? 1 : 0,
                          RegistryValueKind.DWord);
            key?.SetValue("LongPressReadingPreview",
                          options.LongPressReadingPreview ? 1 : 0,
                          RegistryValueKind.DWord);
            key?.SetValue("AltReadingPreview",
                          options.AltReadingPreview ? 1 : 0,
                          RegistryValueKind.DWord);
            key?.SetValue("EnterSubmitsToApp", options.EnterSubmitsToApp ? 1 : 0,
                          RegistryValueKind.DWord);
        }

        var weaselText = File.Exists(WeaselCustomYaml)
            ? File.ReadAllText(WeaselCustomYaml, Encoding.UTF8)
            : "patch:\n";
        if (!Regex.IsMatch(weaselText, @"(?m)^patch:\s*$"))
            weaselText = weaselText.TrimEnd() + "\n\npatch:\n";
        weaselText = SetPatchBool(weaselText, "style/inline_preedit",
                                  options.InlinePreedit);
        weaselText = SetPatchBool(weaselText, "style/inline_preedit_raw_input",
                                  options.InlinePreeditRawInput);
        weaselText = SetPatchBool(weaselText, "style/expanded_comment_width",
                                  options.ExpandedCommentWidth);
        weaselText = SetPatchBool(weaselText, "style/expanded_comment_align_label",
                                  options.ExpandedCommentAlignLabel);
        WriteUtf8IfChanged(WeaselCustomYaml, weaselText.TrimEnd() + "\n");
    }

    private static bool ReadRegistryBool(string name, bool fallback)
    {
        using var key = Registry.CurrentUser.OpenSubKey(@"Software\Rime\Weasel");
        return key?.GetValue(name) is int value ? value != 0 : fallback;
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
        WriteUtf8IfChanged(JapaneseSchemaCustomYaml, text.TrimEnd() + "\n");
    }

    public AppearanceOptions ReadAppearance() => new(
        ReadPatchInt("style/layout/min_width", 600),
        ReadPatchInt("style/comment_font_point", 11),
        ReadPatchInt("style/layout/candidate_spacing", 16),
        ReadPatchInt("style/layout/hilite_padding", 8),
        ReadPatchColor("preset_color_schemes/android/chinese_candidate_text_color", Color.FromArgb(255, 242, 242, 242)),
        ReadPatchColor("preset_color_schemes/android/chinese_candidate_back_color", Color.FromArgb(0, 0, 0, 0)),
        ReadPatchColor("preset_color_schemes/android/japanese_candidate_text_color", Color.FromArgb(255, 128, 203, 196)),
        ReadPatchColor("preset_color_schemes/android/japanese_candidate_back_color", Color.FromArgb(0, 0, 0, 0)),
        ReadPatchColor("preset_color_schemes/android/common_phrase_candidate_text_color", Color.FromArgb(255, 242, 242, 242)),
        ReadPatchColor("preset_color_schemes/android/common_phrase_candidate_back_color", Color.FromArgb(0, 0, 0, 0)));

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
        text = SetPatchColor(text, "preset_color_schemes/android/common_phrase_candidate_text_color", options.CommonPhraseText);
        text = SetPatchColor(text, "preset_color_schemes/android/common_phrase_candidate_back_color", options.CommonPhraseBackground);
        WriteUtf8IfChanged(WeaselCustomYaml, text.TrimEnd() + "\n");
    }

    public void SaveAppearanceHot(AppearanceOptions options)
    {
        SaveAppearance(options);
        RefreshBuiltAppearance(options);
        using var key = Registry.CurrentUser.CreateSubKey(@"Software\Rime\Weasel");
        if (key is null) throw new InvalidOperationException("无法更新候选窗外观。");
        key.SetValue("CandidateWidth", options.Width, RegistryValueKind.DWord);
        key.SetValue("CommentFontSize", options.CommentSize, RegistryValueKind.DWord);
        key.SetValue("CandidateSpacing", options.CandidateSpacing, RegistryValueKind.DWord);
        key.SetValue("HighlightPadding", options.HighlightPadding, RegistryValueKind.DWord);
        key.SetValue("ChineseTextColor", ToAbgr(options.ChineseText), RegistryValueKind.DWord);
        key.SetValue("ChineseBackgroundColor", ToAbgr(options.ChineseBackground), RegistryValueKind.DWord);
        key.SetValue("JapaneseTextColor", ToAbgr(options.JapaneseText), RegistryValueKind.DWord);
        key.SetValue("JapaneseBackgroundColor", ToAbgr(options.JapaneseBackground), RegistryValueKind.DWord);
        key.SetValue("CommonPhraseTextColor", ToAbgr(options.CommonPhraseText), RegistryValueKind.DWord);
        key.SetValue("CommonPhraseBackgroundColor", ToAbgr(options.CommonPhraseBackground), RegistryValueKind.DWord);
        var previous = key.GetValue("AppearanceSettingsGeneration") is int value ? value : 0;
        key.SetValue("AppearanceSettingsGeneration", unchecked(previous + 1), RegistryValueKind.DWord);
        var hotPrevious = key.GetValue("AnnotationSettingsGeneration") is int hotValue ? hotValue : 0;
        key.SetValue("AnnotationSettingsGeneration", unchecked(hotPrevious + 1), RegistryValueKind.DWord);
    }

    private static int ToAbgr(Color color) => unchecked((int)(
        ((uint)color.A << 24) | ((uint)color.B << 16) |
        ((uint)color.G << 8) | color.R));

    public void RefreshBuiltAppearance(AppearanceOptions options) =>
        BuiltConfigRefresher.RefreshAppearance(RimeDirectory, options);

    public void RefreshBuiltInputLayout(InputOptions options, int rareThreshold) =>
        BuiltConfigRefresher.RefreshInputLayout(RimeDirectory, options, rareThreshold);

    private static void WriteUtf8IfChanged(string path, string text)
    {
        if (File.Exists(path) && File.ReadAllText(path, Encoding.UTF8) == text) return;
        File.WriteAllText(path, text, new UTF8Encoding(false));
    }

    private bool ReadOption(string key, bool fallback)
    {
        if (!File.Exists(UserYaml)) return fallback;
        var text = File.ReadAllText(UserYaml, Encoding.UTF8);
        var match = Regex.Match(text, $@"(?m)^\s+{Regex.Escape(key)}:\s*(true|false)\s*$");
        return match.Success ? match.Groups[1].Value == "true" : fallback;
    }

    private bool ReadEmojiSecondPosition()
    {
        if (!File.Exists(EmojiSecondPositionFile)) return true;
        return File.ReadAllText(EmojiSecondPositionFile, Encoding.UTF8).Trim() != "0";
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

    private bool ReadSchemaPatchBool(string key, bool fallback)
    {
        if (!File.Exists(JapaneseSchemaCustomYaml)) return fallback;
        var text = File.ReadAllText(JapaneseSchemaCustomYaml, Encoding.UTF8);
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
