using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Automation;

namespace ZhongriSettingsV2;

public sealed partial class MainWindow
{
    internal async Task RunKeyEditorUiSelfTestAsync()
    {
        var store = new JapaneseInputTableStore(_store.RimeDirectory, _store.HotSettingsRegistryPath);
        if (ParseImeShortcut("Ctrl+Alt+L") != ((12 << 16) | 'L') || ParseImeShortcut("Ctrl+Alt+PageDown") != ((12 << 16) | 0xff56) || ParseImeShortcut("") != 0)
            throw new InvalidOperationException("Feature shortcut parsing failed");
        try { ParseImeShortcut("Alt+L"); throw new InvalidOperationException("Unsafe shortcut accepted"); } catch (ArgumentException) { }
        var table = store.CreateEditableRomaji();
        foreach (var key in new[] { "b", "m" })
            if (DescribeDiagramKey(key, table).Second.Contains("→", StringComparison.Ordinal))
                throw new InvalidOperationException("Combination example leaked onto keycap: " + key);
        var exampleTable = new JapaneseInputTable("键帽示例测试",
            [new("nb", "ねば", ""), new("km", "かも", "")]);
        foreach (var key in new[] { "b", "m" })
        {
            var label = DescribeDiagramKey(key, exampleTable);
            if (label.Second != "" || !label.Details.Contains("→", StringComparison.Ordinal))
                throw new InvalidOperationException("Empty tail must stay blank and examples must remain in tooltip: " + key);
        }
        var originalOptions = _store.ReadInputOptions();
        _store.SaveInputOptions(originalOptions with { JapaneseLearningMode = true });
        if (!_store.ReadInputOptions().JapaneseLearningMode) throw new InvalidOperationException("Learning mode did not persist");
        _store.SaveInputOptions(_store.ReadInputOptions() with { JapaneseLearningKanji = true });
        if (!_store.ReadInputOptions().JapaneseLearningKanji) throw new InvalidOperationException("Kanji learning format did not persist");
        _store.SaveInputOptions(originalOptions with { JapaneseLearningMode = false });
        SelectPage("hotkeys"); Root.UpdateLayout();
        SelectPage("japanese"); Root.UpdateLayout();
        store.SetMixed(true);
        var baseText = File.ReadAllText(Path.Combine(_store.RimeDirectory, "japanese_input_tables", table.Id + ".tsv"));
        void Check(string code, string expected)
        {
            var reading = JapaneseInputTableStore.Convert(code, store.Read()!.Rules);
            if (reading.Text != expected || reading.Pending != "") throw new InvalidOperationException(code + " != " + expected);
        }
        var (editor, read) = BuildJapaneseKeyEditor(store, table, "q");
        PageContent.Children.Clear();
        PageContent.Children.Add(editor);
        Root.UpdateLayout();
        var first = editor.Children.OfType<TextBox>().Single(b => AutomationProperties.GetAutomationId(b) == "KeyFirst");
        if (!IsInteractivePointerTarget(first)) throw new InvalidOperationException("Key editor input swallowed by blank focus handling");
        first.Text = "ky";
        await Task.Delay(220);
        store.SaveKey(table.Id, "q", read());
        Check("qa", "きゃ"); Check("qu", "きゅ"); Check("qo", "きょ");
        store.SaveKey(table.Id, "p", new(Second: "ou")); Check("kp", "こう");
        store.SaveKey(table.Id, "0", new(Second: "ou")); Check("k0", "こう");
        store.SaveKey(table.Id, "=", new(Second: "ou")); Check("k=", "こう");
        if (JapaneseInputTableStore.InferKey("a", store.BaseTable(table.Id)).First != "")
            throw new InvalidOperationException("Standalone vowel was inferred as an initial consonant");
        store.SaveKey(table.Id, "3", new(Second: "ati/iti/utu")); Check("k3", "かち");
        if (!File.ReadAllText(store.TablePath).Contains("# zhongri-key-variants: k3=かち/きち/くつ"))
            throw new InvalidOperationException("Multiple tails missing");
        store.SaveKey(table.Id, "S", new(Second: "ati/iti")); Check("kS", "かち"); Check("sa", "さ");
        store.SaveKey(table.Id, "A", new(Single: "A", Action: "input")); Check("A", "A"); Check("a", "あ");
        store.SaveKeys(table.Id, new Dictionary<string, JapaneseKeyDefinition?> { ["a"] = new(Single: "あ"), ["A"] = new(Single: "A", Action: "input") });
        Check("a", "あ"); Check("A", "A");
        store.SaveKeys(table.Id, new Dictionary<string, JapaneseKeyDefinition?> { ["a"] = new(Single: "あ", LongPress: "abc", LongPressMilliseconds: 350),
            ["A"] = new(Single: "A", Action: "input", LongPress: "XYZ", LongPressMilliseconds: 700) });
        if (!File.ReadAllText(store.TablePath).Contains("# zhongri-key-long-press: 97=350,616263") ||
            !File.ReadAllText(store.TablePath).Contains("# zhongri-key-long-press: 65=700,58595A") || store.KeyDefinition(table.Id, "a").LongPress != "abc")
            throw new InvalidOperationException("Ordinary/Shift long press was not persisted and compiled");
        try { store.SaveKey(table.Id, "a", new(LongPress: "abc", LongPressMilliseconds: 20)); throw new InvalidOperationException("Invalid long press duration accepted"); }
        catch (ArgumentException) { }
        Check("Ka", "か");
        store.SaveKey(table.Id, "v", new(First: "f")); Check("va", "ふぁ"); Check("vi", "ふぃ");
        store.SaveKey(table.Id, ";", new(Single: "っ", Action: "input")); Check("ka;ta", "かった");
        store.SaveKey(table.Id, "-", new(Single: "ー", Action: "input")); Check("ko-hi-", "こーひー");
        store.SaveKey(table.Id, "+", new(Action: "next"));
        store.SaveKey(table.Id, "[", new(Single: "「", Action: "input")); Check("[", "「");
        store.SaveKey(table.Id, ",", new(Single: "，", Action: "literal")); Check(",", "，");
        store.SaveKey(table.Id, "PageUp", new(Action: "previous"));
        store.SaveKey(table.Id, "k", new(Rules: new() { ["k;"] = new("k;", "くぁ", "") })); Check("k;", "くぁ");
        var compiled = File.ReadAllText(store.TablePath);
        foreach (var declaration in new[] { "43=next", "91=input", "44=literal", "65365=previous" })
            if (!compiled.Contains("# zhongri-key-action: " + declaration)) throw new InvalidOperationException("Missing key action " + declaration);
        try { store.SaveKey(table.Id, "q", new(First: "bad!")); throw new InvalidOperationException("Bad role accepted"); }
        catch (ArgumentException) { }
        if (File.ReadAllText(store.TablePath) != compiled) throw new InvalidOperationException("Invalid edit changed active table");
        var profilePath = Path.Combine(_store.RimeDirectory, "japanese_input_tables", table.Id + ".json");
        var profileBeforeBatch = File.ReadAllText(profilePath);
        try
        {
            store.SaveKeys(table.Id, new Dictionary<string, JapaneseKeyDefinition?> { ["a"] = new(Single: "changed"), ["A"] = new(Second: "bad!") });
            throw new InvalidOperationException("Invalid Shift batch accepted");
        }
        catch (ArgumentException) { }
        if (File.ReadAllText(store.TablePath) != compiled || File.ReadAllText(profilePath) != profileBeforeBatch)
            throw new InvalidOperationException("Invalid Shift layer partially saved the normal layer");
        if (ShiftedJapaneseKey("a") != "A" || ShiftedJapaneseKey(";") != ":" || ShiftedJapaneseKey("-") != "_" || ShiftedJapaneseKey("=") != "+")
            throw new InvalidOperationException("Physical Shift pairing is incorrect");
        store.SaveKey(table.Id, "q", null);
        if (store.KeyProfile(table.Id).Keys!.ContainsKey("q")) throw new InvalidOperationException("Restore did not clear key override");
        store.SaveKey(table.Id, "q", new(First: "ky"));
        var reservedFixture = Path.Combine(_store.RimeDirectory, "AZIK editor fixture.txt");
        File.WriteAllText(reservedFixture, "# zhongri-fixed: ds\n# zhongri-alternative: ds=だい\n" +
            "ds\tです\t\nda\tだ\t\ndi\tぢ\t\ndu\tづ\t\nde\tで\t\ndo\tど\t\n" +
            "ka\tか\t\nki\tき\t\nku\tく\t\nke\tけ\t\nko\tこ\t\nks\tかい\t\nms\tまい\t\nhs\tはい\t\n");
        var azik = store.Import(reservedFixture);
        store.UpsertReservedPhrase(azik.Id, "custhold", "ている", "自定义测试分类");
        var copiedAzik = store.CopyProfile(azik.Id, "我的双拼副本");
        store.RenameProfile(copiedAzik.Id, "重命名后的双拼");
        if (!store.SupportsReserved(copiedAzik.Id) || !store.ReservedCatalog(copiedAzik.Id).Any(p => p.Code == "custhold" && p.Text == "ている"))
            throw new InvalidOperationException("Copy/rename without AZIK in name lost custom reserved phrases");
        store.UpsertReservedPhrase(copiedAzik.Id, "custcopy", "でした", "副本独立分类");
        if (store.ReservedCatalog(azik.Id).Any(p => p.Code == "custcopy")) throw new InvalidOperationException("Copied reserved settings mutated source");
        store.SelectProfile(copiedAzik.Id);
        Check("custhold", "ている");
        store.SelectProfile(azik.Id);
        store.SaveKey(azik.Id, "s", new(Second: "ou"));
        if (!File.ReadAllText(store.TablePath).Contains("# zhongri-normal-reading: ds=どう;") ||
            store.Read()!.Rules.Single(r => r.Input == "ds").Output != "です")
            throw new InvalidOperationException("Reserved first choice or edited normal reading was lost");
        store.SelectProfile(table.Id);
        Check("qa", "きゃ");
        if (File.ReadAllText(Path.Combine(_store.RimeDirectory, "japanese_input_tables", table.Id + ".tsv")) != baseText)
            throw new InvalidOperationException("Base table was overwritten");
        if (!store.Mixed || !store.Enabled) throw new InvalidOperationException("Input modes were changed by key edit");
        var sourceDefinition = store.KeyDefinition(table.Id, "a");
        var copy = store.CopyProfile(table.Id, "管理测试副本");
        if (store.Read()?.Id != table.Id || store.KeyDefinition(copy.Id, "a").LongPress != sourceDefinition.LongPress ||
            store.KeyDefinition(copy.Id, "S").Second != store.KeyDefinition(table.Id, "S").Second)
            throw new InvalidOperationException("Copy changed active profile or lost key layers/hold settings");
        var copyProfilePath = Path.Combine(_store.RimeDirectory, "japanese_input_tables", copy.Id + ".json");
        store.RenameProfile(copy.Id, "管理测试重命名");
        if (store.KeyProfile(copy.Id).Name != "管理测试重命名") throw new InvalidOperationException("Rename failed");
        try { store.RenameProfile(copy.Id, table.Name); throw new InvalidOperationException("Duplicate name accepted"); }
        catch (ArgumentException) { }
        store.DeleteProfile(copy.Id);
        if (store.Profiles().Any(p => p.Id == copy.Id) || !File.Exists(copyProfilePath) || store.Read()?.Id != table.Id)
            throw new InvalidOperationException("Soft deletion lost data, affected active scheme or left the scheme visible");
        var activeCopy = store.CopyProfile(table.Id, "待删除的当前配置");
        store.SelectProfile(activeCopy.Id);
        store.DeleteProfile(activeCopy.Id);
        if (store.Read() is null || store.Read()!.Id == activeCopy.Id || !store.Enabled || !store.Mixed)
            throw new InvalidOperationException("Deleting the active scheme failed to switch or changed input modes");
        store.SelectProfile(table.Id);
        var lastStore = new JapaneseInputTableStore(Path.Combine(_store.RimeDirectory, "last-profile"), _store.HotSettingsRegistryPath + "\\LastProfile", Path.Combine(_store.RimeDirectory, "no-builtins"));
        var last = lastStore.CreateEditableRomaji("唯一配置");
        lastStore.DeleteProfile(last.Id);
        if (lastStore.Profiles().Count != 0 || lastStore.Enabled || lastStore.Read() is not null)
            throw new InvalidOperationException("Deleting the last scheme did not fall back to default romaji");
        SelectPage("japanese-table");
        await Task.Delay(250);
        Root.UpdateLayout();
        if (_japaneseKeyboardDiagram!.ActualHeight <= 0) throw new InvalidOperationException("Editable keyboard did not render");
        var diagram = ((StackPanel)_japaneseKeyboardDiagram).Children.OfType<StackPanel>().Single();
        var keyboard = (StackPanel)diagram.Children.OfType<Viewbox>().Single().Child;
        var rows = keyboard.Children.OfType<StackPanel>().ToArray();
        if (rows.Length != 5) throw new InvalidOperationException("Keyboard should have five physical rows, not separate uppercase/symbol rows");
        foreach (var row in rows)
        {
            var caps = row.Children.OfType<Button>().ToArray();
            if (Math.Abs(row.Children.OfType<FrameworkElement>().Sum(cap => cap.Width) + (row.Children.Count - 1) * row.Spacing - keyboard.Width) > 0.01)
                throw new InvalidOperationException("Physical row widths do not align");
            foreach (var cap in caps)
                if (cap.Content is StackPanel labels && labels.Children.OfType<TextBlock>().Any(t => t.Text == "—" || t.Text.StartsWith("单 ")))
                    throw new InvalidOperationException("Empty role or obsolete single prefix is visible");
        }
        if (rows.SelectMany(row => row.Children.OfType<Button>()).Any(cap => AutomationProperties.GetAutomationId(cap) == "EditJapaneseKey_65"))
            throw new InvalidOperationException("Uppercase A was rendered as a separate physical key");
        foreach (var key in new[] { "q", "a", "1", ";", "-", "PageUp" })
            await EditJapaneseKeyAsync(key, store.Read()!, true);
        File.WriteAllText(Path.Combine(Path.GetTempPath(), "ZhongriSettingsV2-key-editor-fixture.txt"), _store.RimeDirectory);
    }
}
