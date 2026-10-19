using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Drawing;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using Microsoft.Win32;
using System.Security.Cryptography;

namespace RimeSettings;

internal sealed record InputOptions(
    bool English, bool Japanese, bool DirectJapaneseReading, bool SingleCharacterAnnotations,
    bool EmojiSecondPosition,
    bool JapanesePrefixCompletion, bool JapanesePrefixCompletionSuffix,
    bool JapanesePrefixCompletionJapaneseFirst,
    bool InlinePreedit, bool InlinePreeditRawInput,
    bool EnterSubmitsToApp, bool SpaceSelectFirst, bool SpaceReadingPreview, bool LongPressReadingPreview,
    bool AltReadingPreview,
    bool ExpandedCommentWidth, bool ExpandedCommentAlignLabel, bool HoverCommentScroll,
    bool JapaneseContinuationLock, bool Fuzzy,
    bool FuzzySokuon, bool FuzzyLongI, bool FuzzyLongU,
    bool FuzzyLongMark, bool FuzzyChiJi, bool FuzzyHuFu,
    bool FuzzyShuSho, bool FuzzyKeKai, bool FuzzyKeKaeGae,
    bool FuzzySeiSai, bool FuzzyDakuten, bool ParticleWaHa, bool Sentence,
    bool GoogleCloudCandidates, bool JapaneseLearningMode = false, bool JapaneseLearningKanji = false,
    int EnterMode = -1, bool JapaneseLearningSpace = false, bool JapaneseLearningNewline = false,
    bool FuzzyMultiple = false)
{
    [System.Text.Json.Serialization.JsonIgnore]
    public int EffectiveEnterMode => EnterMode is >= 0 and <= 3 ? EnterMode : EnterSubmitsToApp ? 3 : 0;
}
internal sealed record AppearanceOptions(
    int Width, int CommentSize, int CandidateSpacing, int HighlightPadding,
    Color ChineseText, Color ChineseBackground,
    Color JapaneseText, Color JapaneseBackground,
    Color CommonPhraseText, Color CommonPhraseBackground,
    Color FuzzyCorrectionText, int CandidateSize = 24,
    Color? SelectedText = null, Color? SelectedBackground = null,
    Color? AnnotationText = null, Color? PanelBackground = null, int MinHeight = 0,
    Color? ReservedText = null, bool CandidateLanguageSplit=false,
    int CandidateSplitChineseColumns=3,int CandidateSplitJapaneseColumns=3);

internal sealed record AnnotationHotState(
    bool English, bool Japanese, bool DirectJapaneseReading,
    bool SingleCharacterAnnotations, int Generation);

internal sealed class SettingsStore
{
    public bool ReadHideJapaneseTranslationAnnotations() =>
        ReadRegistryBool("HideJapaneseTranslationAnnotations",
            ReadOption("hide_japanese_translation_annotations", false));

    public void SaveHideJapaneseTranslationAnnotations(bool hide)
    {
        Directory.CreateDirectory(RimeDirectory);
        var text = File.Exists(UserYaml) ? File.ReadAllText(UserYaml, Encoding.UTF8) : "var:\n  option:\n";
        if (!Regex.IsMatch(text, @"(?m)^  option:\s*$"))
            text = Regex.IsMatch(text, @"(?m)^var:\s*$")
                ? new Regex(@"(?m)^var:\s*$").Replace(text, "var:\n  option:", 1)
                : text.TrimEnd() + "\nvar:\n  option:\n";
        text = SetOption(text, "hide_japanese_translation_annotations", hide);
        WriteUtf8IfChanged(UserYaml, text.TrimEnd() + "\n");
        using var key = Registry.CurrentUser.CreateSubKey(_hotSettingsRegistryPath);
        key.SetValue("HideJapaneseTranslationAnnotations", hide ? 1 : 0, RegistryValueKind.DWord);
        var generation = key.GetValue("AnnotationSettingsGeneration") is int previous ? previous : 0;
        key.SetValue("AnnotationSettingsGeneration", unchecked(generation + 1), RegistryValueKind.DWord);
    }

    public bool ReadPreferNiuAnnotations()
    {
        using var key = Registry.CurrentUser.OpenSubKey(_hotSettingsRegistryPath);
        return key?.GetValue("PreferNiuAnnotations") is int value && value != 0;
    }

    public void SavePreferNiuAnnotations(bool prefer)
    {
        using var key = Registry.CurrentUser.CreateSubKey(_hotSettingsRegistryPath);
        key.SetValue("PreferNiuAnnotations", prefer ? 1 : 0, RegistryValueKind.DWord);
        var generation = key.GetValue("AnnotationSettingsGeneration") is int old ? old : 0;
        key.SetValue("AnnotationSettingsGeneration", unchecked(generation + 1), RegistryValueKind.DWord);
    }
    private readonly string _hotSettingsRegistryPath;
    internal string HotSettingsRegistryPath => _hotSettingsRegistryPath;
    public string RimeDirectory { get; }
    public string UserYaml => Path.Combine(RimeDirectory, "user.yaml");
    public string WeaselCustomYaml => Path.Combine(RimeDirectory, "weasel.custom.yaml");
    public string JapaneseSchemaYaml => Path.Combine(RimeDirectory, "rime_ice_japanese.schema.yaml");
    public string JapaneseSchemaCustomYaml => Path.Combine(RimeDirectory, "rime_ice_japanese.custom.yaml");
    public string EmojiSecondPositionFile => Path.Combine(RimeDirectory, "emoji_second_position.txt");

    public bool HasNiuCredentials()
    {
        using var key = Registry.CurrentUser.OpenSubKey(_hotSettingsRegistryPath);
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
            using var key = Registry.CurrentUser.CreateSubKey(_hotSettingsRegistryPath)
                ?? throw new InvalidOperationException("无法保存 API Key。");
            key.SetValue("NiuAppId", appId, RegistryValueKind.String);
            key.SetValue("NiuApiKeyProtected", protectedKey, RegistryValueKind.Binary);
        }
        finally { CryptographicOperations.ZeroMemory(plain); }
    }

    public void ClearNiuCredentials()
    {
        using var key = Registry.CurrentUser.OpenSubKey(_hotSettingsRegistryPath, writable: true);
        key?.DeleteValue("NiuApiKeyProtected", throwOnMissingValue: false);
        key?.DeleteValue("NiuAppId", throwOnMissingValue: false);
    }

    public async Task<string> TestNiuConnectionAsync()
    {
        using var registry = Registry.CurrentUser.OpenSubKey(_hotSettingsRegistryPath);
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

    public SettingsStore(string rimeDirectory,
                         string hotSettingsRegistryPath = @"Software\ZhongriInputMethod")
    {
        RimeDirectory = rimeDirectory;
        _hotSettingsRegistryPath = hotSettingsRegistryPath;
    }

    public InputOptions ReadInputOptions() => new(
        ReadRegistryBool("ShowEnglishAnnotation", ReadOption("show_english_annotation", true)),
        ReadRegistryBool("ShowJapaneseAnnotation", ReadOption("show_japanese_annotation", true)),
        ReadRegistryBool("ShowDirectJapaneseReading", ReadOption("show_direct_japanese_reading", false)),
        ReadRegistryBool("ShowSingleCharacterAnnotations", ReadOption("show_single_character_annotation", true)),
        ReadRegistryBool("Option_emoji_second_position", ReadEmojiSecondPosition()),
        !ReadHotOption("japanese_prefix_completion_disabled", true),
        ReadHotOption("japanese_prefix_completion_suffix", false),
        ReadHotOption("japanese_prefix_completion_japanese_first", true),
        ReadPatchBool("style/inline_preedit", true),
        ReadPatchBool("style/inline_preedit_raw_input", true),
        ReadEnterMode()==3,
        ReadSchemaPatchBool("space_commit_raw/select_first", false),
        ReadSchemaPatchBool("space_commit_raw/reading_preview", false),
        ReadRegistryBool("LongPressReadingPreview", false),
        ReadRegistryBool("AltReadingPreview", false),
        ReadPatchBool("style/expanded_comment_width", false),
        ReadPatchBool("style/expanded_comment_align_label", true),
        ReadPatchBool("style/hover_comment_scroll", false),
        ReadHotOption("japanese_continuation_lock", true),
        ReadHotOption("japanese_fuzzy_match", false),
        ReadHotOption("japanese_fuzzy_sokuon", true),
        ReadHotOption("japanese_fuzzy_long_i", true),
        ReadHotOption("japanese_fuzzy_long_u", true),
        ReadHotOption("japanese_fuzzy_long_mark", true),
        ReadHotOption("japanese_fuzzy_chi_ji", true),
        ReadHotOption("japanese_fuzzy_hu_fu", true),
        ReadHotOption("japanese_fuzzy_shu_sho", true),
        ReadHotOption("japanese_fuzzy_ke_kai", true),
        ReadHotOption("japanese_fuzzy_ke_kae_gae", true),
        ReadHotOption("japanese_fuzzy_sei_sai", true),
        ReadHotOption("japanese_fuzzy_dakuten", true),
        ReadHotOption("japanese_particle_wa_ha", false),
        ReadHotOption("sentence_translation", false),
        ReadHotOption("google_cloud_candidates", false),
        ReadHotOption("japanese_learning_mode", false),
        ReadHotOption("japanese_learning_kanji", false), ReadEnterMode(),
        ReadHotOption("japanese_learning_space", false), ReadHotOption("japanese_learning_newline", false),
        ReadHotOption("japanese_fuzzy_multiple", false));

    public int ReadEnterMode()
    {
        using var key = Registry.CurrentUser.OpenSubKey(_hotSettingsRegistryPath);
        if (key?.GetValue("EnterMode") is int mode && mode is >= 0 and <= 3) return mode;
        return ReadRegistryBool("EnterSubmitsToApp", ReadSchemaPatchBool("space_commit_raw/enter_submits_to_app", false)) ? 3 : 0;
    }

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

    public AnnotationHotState? ReadAnnotationHotState()
    {
        using var key = Registry.CurrentUser.OpenSubKey(_hotSettingsRegistryPath);
        if (key?.GetValue("ShowEnglishAnnotation") is not int english ||
            key.GetValue("ShowJapaneseAnnotation") is not int japanese ||
            key.GetValue("ShowDirectJapaneseReading") is not int directReading ||
            key.GetValue("ShowSingleCharacterAnnotations") is not int singleCharacter ||
            key.GetValue("AnnotationSettingsGeneration") is not int generation)
            return null;
        return new AnnotationHotState(english != 0, japanese != 0,
            directReading != 0, singleCharacter != 0, generation);
    }

    private void PublishAnnotationOptions(bool english, bool japanese,
                                          bool directJapaneseReading, bool singleCharacterAnnotations)
    {
        using var key = Registry.CurrentUser.CreateSubKey(_hotSettingsRegistryPath);
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
        if (options.EnterMode is < -1 or > 3) throw new ArgumentOutOfRangeException(nameof(options.EnterMode));
        options = options with { EnterMode=options.EffectiveEnterMode, EnterSubmitsToApp=options.EffectiveEnterMode==3 };
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
        text = SetOption(text, "japanese_fuzzy_multiple", options.FuzzyMultiple);
        text = SetOption(text, "japanese_particle_wa_ha", options.ParticleWaHa);
        text = SetOption(text, "japanese_learning_mode", options.JapaneseLearningMode);
        text = SetOption(text, "japanese_learning_kanji", options.JapaneseLearningKanji);
        text = SetOption(text, "japanese_learning_space", options.JapaneseLearningSpace);
        text = SetOption(text, "japanese_learning_newline", options.JapaneseLearningNewline);
        text = SetOption(text, "sentence_translation", options.Sentence);
        text = SetOption(text, "google_cloud_candidates", options.GoogleCloudCandidates);
        WriteUtf8IfChanged(UserYaml, text.TrimEnd() + "\n");
        var multipleFile=Path.Combine(RimeDirectory,"japanese_fuzzy_multiple.flag");
        var previousMultiple=File.Exists(multipleFile) && File.ReadAllText(multipleFile,Encoding.UTF8).Trim()=="1";
        WriteUtf8IfChanged(multipleFile,options.FuzzyMultiple?"1\n":"0\n");
        PublishHotInputOptions(options);
        if(previousMultiple!=options.FuzzyMultiple) PublishJapaneseFuzzyReload();
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
        schemaText = SetPatchInt(schemaText,"space_commit_raw/enter_mode",options.EffectiveEnterMode);
        schemaText = SetPatchBool(schemaText, "space_commit_raw/reading_preview",
                                  options.SpaceReadingPreview);
        WriteUtf8IfChanged(JapaneseSchemaCustomYaml, schemaText.TrimEnd() + "\n");

        // TSF must know the mode before Rime processes the Space key.  A small
        // per-user registry value avoids file IO on every keystroke.
        using (var key = Registry.CurrentUser.CreateSubKey(_hotSettingsRegistryPath))
        {
            key?.SetValue("SpaceReadingPreview", options.SpaceReadingPreview ? 1 : 0,
                          RegistryValueKind.DWord);
            key?.SetValue("LongPressReadingPreview",
                          options.LongPressReadingPreview ? 1 : 0,
                          RegistryValueKind.DWord);
            key?.SetValue("AltReadingPreview",
                          options.AltReadingPreview ? 1 : 0,
                          RegistryValueKind.DWord);
            // New modes are handled by the backend, including exact raw text.
            // Disable the old client's inline-preedit-abort shortcut.
            key?.SetValue("EnterMode", options.EffectiveEnterMode, RegistryValueKind.DWord);
            key?.SetValue("EnterSubmitsToApp", 0,
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
        weaselText = SetPatchBool(weaselText, "style/hover_comment_scroll",
                                  options.HoverCommentScroll);
        WriteUtf8IfChanged(WeaselCustomYaml, weaselText.TrimEnd() + "\n");
    }

    private void PublishHotInputOptions(InputOptions options)
    {
        using var key = Registry.CurrentUser.CreateSubKey(_hotSettingsRegistryPath)
            ?? throw new InvalidOperationException("无法发布输入热设置。");
        (string Name, bool Enabled)[] switches =
        [
            ("japanese_prefix_completion_disabled", !options.JapanesePrefixCompletion),
            ("japanese_prefix_completion_suffix", options.JapanesePrefixCompletionSuffix),
            ("japanese_prefix_completion_japanese_first", options.JapanesePrefixCompletionJapaneseFirst),
            ("japanese_continuation_lock", options.JapaneseContinuationLock),
            ("emoji_second_position", options.EmojiSecondPosition),
            ("japanese_fuzzy_match", options.Fuzzy),
            ("japanese_fuzzy_sokuon", options.FuzzySokuon),
            ("japanese_fuzzy_long_i", options.FuzzyLongI),
            ("japanese_fuzzy_long_u", options.FuzzyLongU),
            ("japanese_fuzzy_long_mark", options.FuzzyLongMark),
            ("japanese_fuzzy_chi_ji", options.FuzzyChiJi),
            ("japanese_fuzzy_hu_fu", options.FuzzyHuFu),
            ("japanese_fuzzy_shu_sho", options.FuzzyShuSho),
            ("japanese_fuzzy_ke_kai", options.FuzzyKeKai),
            ("japanese_fuzzy_ke_kae_gae", options.FuzzyKeKaeGae),
            ("japanese_fuzzy_sei_sai", options.FuzzySeiSai),
            ("japanese_fuzzy_dakuten", options.FuzzyDakuten),
            ("japanese_fuzzy_multiple", options.FuzzyMultiple),
            ("japanese_particle_wa_ha", options.ParticleWaHa),
            ("sentence_translation", options.Sentence),
            ("google_cloud_candidates", options.GoogleCloudCandidates),
            ("japanese_learning_mode", options.JapaneseLearningMode),
            ("japanese_learning_kanji", options.JapaneseLearningKanji),
            ("japanese_learning_space", options.JapaneseLearningSpace),
            ("japanese_learning_newline", options.JapaneseLearningNewline)
        ];
        foreach (var (name, enabled) in switches)
            key.SetValue("Option_" + name, enabled ? 1 : 0, RegistryValueKind.DWord);
        key.SetValue("InlinePreedit", options.InlinePreedit ? 1 : 0, RegistryValueKind.DWord);
        key.SetValue("InlinePreeditRawInput", options.InlinePreeditRawInput ? 1 : 0, RegistryValueKind.DWord);
        key.SetValue("ExpandedCommentWidth", options.ExpandedCommentWidth ? 1 : 0, RegistryValueKind.DWord);
        key.SetValue("ExpandedCommentAlignLabel", options.ExpandedCommentAlignLabel ? 1 : 0, RegistryValueKind.DWord);
        key.SetValue("HoverCommentScroll", options.HoverCommentScroll ? 1 : 0, RegistryValueKind.DWord);
        key.SetValue("SpaceHotMode", options.SpaceReadingPreview ? 2 : options.SpaceSelectFirst ? 1 : 0,
                     RegistryValueKind.DWord);
        var appearance = key.GetValue("AppearanceSettingsGeneration") is int oldAppearance ? oldAppearance : 0;
        key.SetValue("AppearanceSettingsGeneration", unchecked(appearance + 1), RegistryValueKind.DWord);
        // Publish last, after every option value. Existing sessions compare
        // this generation on their next key or TSF panel refresh.
        var previous = key.GetValue("InputSettingsGeneration") is int value ? value : 0;
        key.SetValue("InputSettingsGeneration", unchecked(previous + 1), RegistryValueKind.DWord);
    }

    private bool ReadRegistryBool(string name, bool fallback)
    {
        using var key = Registry.CurrentUser.OpenSubKey(_hotSettingsRegistryPath);
        return key?.GetValue(name) is int value ? value != 0 : fallback;
    }

    private bool ReadHotOption(string name, bool fallback) =>
        ReadRegistryBool("Option_" + name, ReadOption(name, fallback));

    public int ReadCloudCandidateCount()
    {
        using var key = Registry.CurrentUser.OpenSubKey(_hotSettingsRegistryPath);
        return Math.Clamp(key?.GetValue("CloudCandidateCount") is int value ? value : 2, 1, 10);
    }

    public bool ReadCandidateSelectionGuardEnabled() => ReadRegistryBool("CandidateSelectionGuardEnabled", true);

    public int ReadCandidateSelectionGuardMs()
    {
        using var key = Registry.CurrentUser.OpenSubKey(_hotSettingsRegistryPath);
        return Math.Clamp(key?.GetValue("CandidateSelectionGuardMs") is int value ? value : 250, 50, 1000);
    }

    public void SaveCandidateSelectionGuard(bool enabled, int milliseconds)
    {
        if (milliseconds < 50 || milliseconds > 1000) throw new ArgumentOutOfRangeException(nameof(milliseconds));
        using var key = Registry.CurrentUser.CreateSubKey(_hotSettingsRegistryPath)
            ?? throw new InvalidOperationException("无法保存候选误选保护。");
        key.SetValue("CandidateSelectionGuardMs", milliseconds, RegistryValueKind.DWord);
        key.SetValue("CandidateSelectionGuardEnabled", enabled ? 1 : 0, RegistryValueKind.DWord);
    }

    public int ReadCloudLanguageMode()
    {
        using var key = Registry.CurrentUser.OpenSubKey(_hotSettingsRegistryPath);
        return key?.GetValue("CloudLanguageMode") is int value && value >= 0 && value <= 3 ? value : 0;
    }

    public void SaveCloudLanguageMode(int mode)
    {
        if (mode < 0 || mode > 3) throw new ArgumentOutOfRangeException(nameof(mode));
        using var key = Registry.CurrentUser.CreateSubKey(_hotSettingsRegistryPath)
            ?? throw new InvalidOperationException("无法保存云候选语言。");
        key.SetValue("CloudLanguageMode", mode, RegistryValueKind.DWord);
        foreach (var name in new[] { "InputSettingsGeneration", "AnnotationSettingsGeneration" })
            key.SetValue(name, unchecked((key.GetValue(name) is int old ? old : 0) + 1), RegistryValueKind.DWord);
    }

    public void SaveCloudCandidateCount(int count)
    {
        if (count < 1 || count > 10) throw new ArgumentOutOfRangeException(nameof(count));
        using var key = Registry.CurrentUser.CreateSubKey(_hotSettingsRegistryPath)
            ?? throw new InvalidOperationException("无法保存云候选数量。");
        key.SetValue("CloudCandidateCount", count, RegistryValueKind.DWord);
        foreach (var name in new[] { "InputSettingsGeneration", "AnnotationSettingsGeneration" })
            key.SetValue(name, unchecked((key.GetValue(name) is int old ? old : 0) + 1), RegistryValueKind.DWord);
    }

    public int ReadRareSingleCharThreshold()
    {
        using (var key = Registry.CurrentUser.OpenSubKey(_hotSettingsRegistryPath))
            if (key?.GetValue("RareSingleCharThreshold") is int hotThreshold)
                return hotThreshold;
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
        using var key = Registry.CurrentUser.CreateSubKey(_hotSettingsRegistryPath)
            ?? throw new InvalidOperationException("无法发布单字过滤热设置。");
        key.SetValue("RareSingleCharThreshold", threshold, RegistryValueKind.DWord);
        var generation = key.GetValue("InputSettingsGeneration") is int value ? value : 0;
        key.SetValue("InputSettingsGeneration", unchecked(generation + 1), RegistryValueKind.DWord);
        var annotation = key.GetValue("AnnotationSettingsGeneration") is int previous ? previous : 0;
        key.SetValue("AnnotationSettingsGeneration", unchecked(annotation + 1), RegistryValueKind.DWord);
    }

    public void PublishPhraseReload()
    {
        using var key = Registry.CurrentUser.CreateSubKey(_hotSettingsRegistryPath)
            ?? throw new InvalidOperationException("无法发布常用语热更新。");
        var phrase = key.GetValue("PhraseSettingsGeneration") is int oldPhrase ? oldPhrase : 0;
        key.SetValue("PhraseSettingsGeneration", unchecked(phrase + 1), RegistryValueKind.DWord);
        var input = key.GetValue("InputSettingsGeneration") is int oldInput ? oldInput : 0;
        key.SetValue("InputSettingsGeneration", unchecked(input + 1), RegistryValueKind.DWord);
        var annotation = key.GetValue("AnnotationSettingsGeneration") is int oldAnnotation ? oldAnnotation : 0;
        key.SetValue("AnnotationSettingsGeneration", unchecked(annotation + 1), RegistryValueKind.DWord);
    }

    public void PublishChineseFuzzyReload()
    {
        using var key=Registry.CurrentUser.CreateSubKey(_hotSettingsRegistryPath);
        foreach(var name in new[]{"InputSettingsGeneration","AnnotationSettingsGeneration"})
            key.SetValue(name,unchecked((key.GetValue(name) is int old?old:0)+1),RegistryValueKind.DWord);
    }

    public void PublishJapaneseFuzzyReload()
    {
        using var key = Registry.CurrentUser.CreateSubKey(_hotSettingsRegistryPath)
            ?? throw new InvalidOperationException("无法发布日语模糊规则热更新。");
        var fuzzy = key.GetValue("JapaneseFuzzyRulesGeneration") is int oldFuzzy ? oldFuzzy : 0;
        key.SetValue("JapaneseFuzzyRulesGeneration", unchecked(fuzzy + 1), RegistryValueKind.DWord);
        var input = key.GetValue("InputSettingsGeneration") is int oldInput ? oldInput : 0;
        key.SetValue("InputSettingsGeneration", unchecked(input + 1), RegistryValueKind.DWord);
        var annotation = key.GetValue("AnnotationSettingsGeneration") is int oldAnnotation ? oldAnnotation : 0;
        key.SetValue("AnnotationSettingsGeneration", unchecked(annotation + 1), RegistryValueKind.DWord);
    }

    public AppearanceOptions ReadAppearance()
    {
        var appearance=new AppearanceOptions(
        ReadPatchInt("style/layout/min_width", 600),
        ReadPatchInt("style/comment_font_point", 11),
        ReadPatchInt("style/layout/candidate_spacing", 16),
        ReadPatchInt("style/layout/hilite_padding", 8),
        ReadPatchColor("preset_color_schemes/android/chinese_candidate_text_color", Color.FromArgb(255, 242, 242, 242)),
        ReadPatchColor("preset_color_schemes/android/chinese_candidate_back_color", Color.FromArgb(0, 0, 0, 0)),
        ReadPatchColor("preset_color_schemes/android/japanese_candidate_text_color", Color.FromArgb(255, 128, 203, 196)),
        ReadPatchColor("preset_color_schemes/android/japanese_candidate_back_color", Color.FromArgb(0, 0, 0, 0)),
        ReadPatchColor("preset_color_schemes/android/common_phrase_candidate_text_color", Color.FromArgb(255, 242, 242, 242)),
        ReadPatchColor("preset_color_schemes/android/common_phrase_candidate_back_color", Color.FromArgb(0, 0, 0, 0)),
        ReadPatchColor("preset_color_schemes/android/fuzzy_correction_text_color", Color.FromArgb(255, 255, 255, 0)),
        ReadPatchInt("style/font_point",ReadBuiltStyleNumber("font_point",14)),
        ReadPatchColor("preset_color_schemes/android/hilited_candidate_text_color",ReadBuiltThemeColor("hilited_candidate_text_color",Color.White)),
        ReadPatchColor("preset_color_schemes/android/hilited_candidate_back_color",ReadBuiltThemeColor("hilited_candidate_back_color",Color.FromArgb(255,20,184,196))),
        ReadPatchColor("preset_color_schemes/android/comment_text_color",ReadBuiltThemeColor("comment_text_color",Color.FromArgb(255,210,210,210))),
        ReadPatchColor("preset_color_schemes/android/back_color",ReadBuiltThemeColor("back_color",Color.FromArgb(255,64,64,64))),
        ReadPatchInt("style/layout/min_height",0));
        // The running candidate panel applies these hot values over compiled
        // YAML. The editor must start from the same effective state.
        using var hot=Registry.CurrentUser.OpenSubKey(_hotSettingsRegistryPath);
        int Number(string name,int fallback)=>hot?.GetValue(name) is int value?value:fallback;
        Color Paint(string name,Color fallback) {
            if(hot?.GetValue(name) is not int value) return fallback;
            var abgr=unchecked((uint)value);
            return Color.FromArgb((byte)(abgr>>24),(byte)abgr,(byte)(abgr>>8),(byte)(abgr>>16));
        }
        return appearance with {
            CandidateLanguageSplit=Number("CandidateLanguageSplit",0)!=0,
            CandidateSplitChineseColumns=Number("CandidateSplitChineseColumns",3),
            CandidateSplitJapaneseColumns=Number("CandidateSplitJapaneseColumns",3),
            Width=Number("CandidateWidth",appearance.Width),MinHeight=Number("CandidateMinHeight",appearance.MinHeight),
            CandidateSize=Number("CandidateFontSize",appearance.CandidateSize),CommentSize=Number("CommentFontSize",appearance.CommentSize),
            CandidateSpacing=Number("CandidateSpacing",appearance.CandidateSpacing),HighlightPadding=Number("HighlightPadding",appearance.HighlightPadding),
            ChineseText=Paint("ChineseTextColor",appearance.ChineseText),ChineseBackground=Paint("ChineseBackgroundColor",appearance.ChineseBackground),
            JapaneseText=Paint("JapaneseTextColor",appearance.JapaneseText),JapaneseBackground=Paint("JapaneseBackgroundColor",appearance.JapaneseBackground),
            CommonPhraseText=Paint("CommonPhraseTextColor",appearance.CommonPhraseText),CommonPhraseBackground=Paint("CommonPhraseBackgroundColor",appearance.CommonPhraseBackground),
            FuzzyCorrectionText=Paint("FuzzyCorrectionTextColor",appearance.FuzzyCorrectionText),
            ReservedText=Paint("ReservedPhraseTextColor",ReadPatchColor("preset_color_schemes/android/reserved_phrase_text_color",Color.FromArgb(255,74,222,128))),
            SelectedText=Paint("HighlightTextColor",appearance.SelectedText??Color.White),SelectedBackground=Paint("HighlightBackgroundColor",appearance.SelectedBackground??Color.FromArgb(255,20,184,196)),
            AnnotationText=Paint("CommentTextColor",appearance.AnnotationText??Color.LightGray),PanelBackground=Paint("PanelBackgroundColor",appearance.PanelBackground??Color.FromArgb(255,64,64,64))
        };
    }

    public int ReadAppearanceLabelSize() => ReadPatchInt("style/label_font_point",ReadBuiltStyleNumber("label_font_point",14));

    private int ReadBuiltStyleNumber(string name, int fallback)
    {
        var path=Path.Combine(RimeDirectory,"build","weasel.yaml");
        if(!File.Exists(path)) return fallback;
        var text=File.ReadAllText(path,Encoding.UTF8);
        var section=Regex.Match(text,@"(?ms)^style:\s*\n(?<body>(?:[ \t].*?\n|\s*\n)+)");
        var found=Regex.Match(section.Groups["body"].Value,@"(?m)^  "+Regex.Escape(name)+@":\s*(\d+)");
        return found.Success && int.TryParse(found.Groups[1].Value,out var value)?value:fallback;
    }

    private Color ReadBuiltThemeColor(string name,Color fallback)
    {
        var path=Path.Combine(RimeDirectory,"build","weasel.yaml");
        if(!File.Exists(path)) return fallback;
        var text=File.ReadAllText(path,Encoding.UTF8);
        using var personalize=Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize");
        var dark=personalize?.GetValue("AppsUseLightTheme") is int light && light==0;
        var scheme=Regex.Match(text,@"(?m)^  color_scheme"+(dark?"_dark":"")+@":\s*[""']?([a-zA-Z0-9_-]+)");
        if(!scheme.Success) scheme=Regex.Match(text,@"(?m)^  color_scheme:\s*[""']?([a-zA-Z0-9_-]+)");
        if(!scheme.Success) return fallback;
        var section=Regex.Match(text,@"(?m)^  "+Regex.Escape(scheme.Groups[1].Value)+@":\r?\n(?<body>(?:    .*\r?\n)+)");
        var color=Regex.Match(section.Groups["body"].Value,@"(?m)^    "+Regex.Escape(name)+@":\s*0x([0-9a-fA-F]{1,8})");
        if(!color.Success || !uint.TryParse(color.Groups[1].Value,System.Globalization.NumberStyles.HexNumber,null,out var abgr)) return fallback;
        if(color.Groups[1].Value.Length<=6) abgr|=0xff000000;
        return Color.FromArgb((byte)(abgr>>24),(byte)abgr,(byte)(abgr>>8),(byte)(abgr>>16));
    }

    public void SaveAppearance(AppearanceOptions options)
    {
        Directory.CreateDirectory(RimeDirectory);
        var text = File.Exists(WeaselCustomYaml)
            ? File.ReadAllText(WeaselCustomYaml, Encoding.UTF8)
            : "patch:\n";
        if (!Regex.IsMatch(text, @"(?m)^patch:\s*$"))
            text = text.TrimEnd() + "\n\npatch:\n";
        text = SetPatchInt(text, "style/layout/min_width", options.Width);
        text = SetPatchInt(text,"style/layout/min_height",options.MinHeight);
        text = SetPatchInt(text, "style/layout/max_height", 600);
        text = SetPatchInt(text, "style/comment_font_point", options.CommentSize);
        text = SetPatchInt(text,"style/font_point",options.CandidateSize);
        foreach(var item in new[] { ("hilited_candidate_text_color",options.SelectedText), ("hilited_candidate_back_color",options.SelectedBackground), ("comment_text_color",options.AnnotationText), ("back_color",options.PanelBackground) })
            if(item.Item2 is Color color) text=SetPatchColor(text,"preset_color_schemes/android/"+item.Item1,color);
        text = SetPatchInt(text, "style/layout/candidate_spacing", options.CandidateSpacing);
        text = SetPatchInt(text, "style/layout/hilite_padding", options.HighlightPadding);
        text = SetPatchColor(text, "preset_color_schemes/android/chinese_candidate_text_color", options.ChineseText);
        text = SetPatchColor(text, "preset_color_schemes/android/chinese_candidate_back_color", options.ChineseBackground);
        text = SetPatchColor(text, "preset_color_schemes/android/japanese_candidate_text_color", options.JapaneseText);
        text = SetPatchColor(text, "preset_color_schemes/android/japanese_candidate_back_color", options.JapaneseBackground);
        text = SetPatchColor(text, "preset_color_schemes/android/common_phrase_candidate_text_color", options.CommonPhraseText);
        text = SetPatchColor(text, "preset_color_schemes/android/common_phrase_candidate_back_color", options.CommonPhraseBackground);
        text = SetPatchColor(text, "preset_color_schemes/android/fuzzy_correction_text_color", options.FuzzyCorrectionText);
        text = SetPatchColor(text, "preset_color_schemes/android/reserved_phrase_text_color", options.ReservedText??Color.FromArgb(255,74,222,128));
        WriteUtf8IfChanged(WeaselCustomYaml, text.TrimEnd() + "\n");
    }

    public void SaveAppearanceHot(AppearanceOptions options)
    {
        if(options.CandidateSplitChineseColumns<1||options.CandidateSplitJapaneseColumns<1||
            options.CandidateSplitChineseColumns+options.CandidateSplitJapaneseColumns>9)
            throw new ArgumentException("左右各至少一列，合计不能超过九列。");
        SaveAppearance(options);
        RefreshBuiltAppearance(options);
        using var key = Registry.CurrentUser.CreateSubKey(_hotSettingsRegistryPath);
        if (key is null) throw new InvalidOperationException("无法更新候选窗外观。");
        key.SetValue("CandidateWidth", options.Width, RegistryValueKind.DWord);
        key.SetValue("CandidateLanguageSplit",options.CandidateLanguageSplit?1:0,RegistryValueKind.DWord);
        key.SetValue("CandidateSplitChineseColumns",options.CandidateSplitChineseColumns,RegistryValueKind.DWord);
        key.SetValue("CandidateSplitJapaneseColumns",options.CandidateSplitJapaneseColumns,RegistryValueKind.DWord);
        key.SetValue("CandidateMinHeight",options.MinHeight,RegistryValueKind.DWord);
        key.SetValue("CommentFontSize", options.CommentSize, RegistryValueKind.DWord);
        key.SetValue("CandidateFontSize",options.CandidateSize,RegistryValueKind.DWord);
        foreach(var item in new[] { ("HighlightTextColor",options.SelectedText), ("HighlightBackgroundColor",options.SelectedBackground), ("CommentTextColor",options.AnnotationText), ("PanelBackgroundColor",options.PanelBackground) })
            if(item.Item2 is Color color) key.SetValue(item.Item1,ToAbgr(color),RegistryValueKind.DWord);
        key.SetValue("CandidateSpacing", options.CandidateSpacing, RegistryValueKind.DWord);
        key.SetValue("HighlightPadding", options.HighlightPadding, RegistryValueKind.DWord);
        key.SetValue("ChineseTextColor", ToAbgr(options.ChineseText), RegistryValueKind.DWord);
        key.SetValue("ChineseBackgroundColor", ToAbgr(options.ChineseBackground), RegistryValueKind.DWord);
        key.SetValue("JapaneseTextColor", ToAbgr(options.JapaneseText), RegistryValueKind.DWord);
        key.SetValue("JapaneseBackgroundColor", ToAbgr(options.JapaneseBackground), RegistryValueKind.DWord);
        key.SetValue("CommonPhraseTextColor", ToAbgr(options.CommonPhraseText), RegistryValueKind.DWord);
        key.SetValue("CommonPhraseBackgroundColor", ToAbgr(options.CommonPhraseBackground), RegistryValueKind.DWord);
        key.SetValue("FuzzyCorrectionTextColor", ToAbgr(options.FuzzyCorrectionText), RegistryValueKind.DWord);
        key.SetValue("ReservedPhraseTextColor", ToAbgr(options.ReservedText??Color.FromArgb(255,74,222,128)), RegistryValueKind.DWord);
        var previous = key.GetValue("AppearanceSettingsGeneration") is int value ? value : 0;
        key.SetValue("AppearanceSettingsGeneration", unchecked(previous + 1), RegistryValueKind.DWord);
        // The already-open candidate panel watches this shared revision.
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
