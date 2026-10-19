using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace ZhongriSettingsV2;

public sealed partial class MainWindow
{
    private FrameworkElement? _japaneseKeyboardDiagram;
    private string _japaneseKeyboardDiagramName = "";

    internal void TestAllJapaneseKeyboardDiagrams()
    {
        var store = new JapaneseInputTableStore(_store.RimeDirectory, _store.HotSettingsRegistryPath);
        var original = store.Read();
        var wasEnabled = store.Enabled;
        var reports = new List<string>();
        foreach (var profile in store.Profiles())
        {
            var table = store.SelectProfile(profile.Id);
            SelectPage("japanese-table");
            if (_japaneseKeyboardDiagramName != table.Name || _japaneseKeyboardDiagram is null)
                throw new InvalidOperationException("Wrong keyboard diagram: " + table.Name);
            if (table.Name.Contains("AZIK Test", StringComparison.OrdinalIgnoreCase))
            {
                foreach (var (key, expected) in new[] { ("0", "ou"), ("p", "on"), ("s", "ai") })
                    if (store.KeyDefinition(table.Id, key).Second is null && DescribeDiagramKey(key, table).Second != expected)
                        throw new InvalidOperationException("Custom keyboard key " + key + " does not show " + expected);
            }
            reports.Add(table.Name + ": PASS; Q=" + DescribeDiagramKey("q", table).First + "/" + DescribeDiagramKey("q", table).Second);
        }
        store.SetEnabled(false);
        SelectPage("japanese-table");
        if (_japaneseKeyboardDiagramName != "默认罗马字") throw new InvalidOperationException("Disabled diagram did not reset");
        if (original is not null) store.SelectProfile(original.Id);
        store.SetEnabled(wasEnabled);
        SelectPage("japanese-table");
        File.WriteAllText(Path.Combine(Path.GetTempPath(), "ZhongriSettingsV2-keyboard-diagrams-test.txt"), string.Join("\n", reports));
    }

    // Labels are derived from the active rules, including pending-input rules.
    // Never describe one exceptional phrase as a universal vowel shortcut.
    private static readonly Dictionary<string, string> KeyboardRomaji = BuildKeyboardRomaji();
    private static Dictionary<string, string> BuildKeyboardRomaji()
    {
        var result = new Dictionary<string, string>();
        var rows = new[] {
            ("", "あいうえお"), ("k", "かきくけこ"), ("g", "がぎぐげご"),
            ("s", "さしすせそ"), ("z", "ざじずぜぞ"), ("t", "たちつてと"),
            ("d", "だぢづでど"), ("n", "なにぬねの"), ("h", "はひふへほ"),
            ("b", "ばびぶべぼ"), ("p", "ぱぴぷぺぽ"), ("m", "まみむめも"),
            ("r", "らりるれろ") };
        foreach (var (prefix, kana) in rows)
            for (var i = 0; i < 5; i++) result[kana[i].ToString()] = prefix + "aiueo"[i];
        foreach (var (kana, prefix) in new[] { ("き", "ky"), ("ぎ", "gy"), ("し", "sh"),
            ("じ", "j"), ("ち", "ch"), ("に", "ny"), ("ひ", "hy"), ("び", "by"), ("ぴ", "py"), ("み", "my"), ("り", "ry") })
            for (var i = 0; i < 3; i++) result[kana + "ゃゅょ"[i]] = prefix + "auo"[i];
        result["や"] = "ya"; result["ゆ"] = "yu"; result["よ"] = "yo";
        result["わ"] = "wa"; result["を"] = "wo"; result["ん"] = "n"; result["ー"] = "ー";
        result["し"] = "shi"; result["ち"] = "chi"; result["つ"] = "tsu";
        result["ふ"] = "fu"; result["じ"] = "ji";
        return result;
    }

    private static string? DiagramReading(string text)
    {
        var reading = new System.Text.StringBuilder();
        var kana = string.Concat(text.Select(c => c >= 'ァ' && c <= 'ヶ' ? (char)(c - 0x60) : c));
        for (var i = 0; i < kana.Length; i++)
        {
            if (i + 1 < kana.Length && KeyboardRomaji.TryGetValue(kana.Substring(i, 2), out var pair))
            { reading.Append(pair); i++; }
            else if (KeyboardRomaji.TryGetValue(kana[i].ToString(), out var single)) reading.Append(single);
            else return null;
        }
        return reading.ToString();
    }

    private static (string First, string Second, string Details) DescribeDiagramKey(string key, JapaneseInputTable? table)
    {
        if (table is null)
            return ("aeiou".Contains(key, StringComparison.Ordinal) ? key : "普通罗马字", "", "默认罗马字输入");
        var rules = table.Rules;
        string? ConvertExact(string code)
        {
            var value = JapaneseInputTableStore.Convert(code, rules, table.Rgt);
            return value.Pending.Length == 0 && value.Text.Length > 0 && value.Text != code ? value.Text : null;
        }
        var details = new List<string>();
        var firstRole = JapaneseInputTableStore.InferKey(key, table).First;
        var first = firstRole == "" ? "—" : firstRole;
        var tails = new List<string>();
        foreach (var prefix in new[] { "k", "h", "m", "r", "n", "t", "s" })
        {
            var output = ConvertExact(prefix + key);
            if (output is null) continue;
            details.Add(prefix + key + " → " + output);
            var reading = DiagramReading(output);
            if (reading is not null && reading.StartsWith(prefix, StringComparison.Ordinal) && reading.Length > prefix.Length)
                tails.Add(reading[prefix.Length..]);
        }
        var common = tails.GroupBy(v => v).OrderByDescending(g => g.Count()).FirstOrDefault();
        var second = common is not null && common.Count() >= 3 ? common.Key : "";
        var matching = rules.Where(r => r.Input.StartsWith(key, StringComparison.Ordinal)).Take(40).ToList();
        details.Insert(0, "第一键声母：" + first + "；第二键韵尾：" + second + "；单键输出：" + JapaneseInputTableStore.InferKey(key, table).Single);
        details.AddRange(matching.Select(r => r.Input + " → " + r.Output + (r.Pending.Length > 0 ? "（保留 " + r.Pending + "）" : "")));
        return (first, second, string.Join("\n", details.Distinct()));
    }

    private static string? ShiftedJapaneseKey(string key)
    {
        if (key.Length != 1) return null;
        if (char.IsAsciiLetterLower(key[0])) return key.ToUpperInvariant();
        const string normal = "`1234567890-=[]\\;',./";
        const string shifted = "~!@#$%^&*()_+{}|:\"<>?";
        var index = normal.IndexOf(key[0]);
        return index < 0 ? null : shifted[index].ToString();
    }

    private FrameworkElement BuildJapaneseKeyboardDiagram(JapaneseInputTable? table)
    {
        _japaneseKeyboardDiagramName = table?.Name ?? "默认罗马字";
        var body = new StackPanel { Spacing = 14 };
        body.Children.Add(new TextBlock { Text = _japaneseKeyboardDiagramName + " · 键盘速查图", FontSize = 24,
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold, TextWrapping = TextWrapping.Wrap });
        body.Children.Add(new TextBlock { Text = "点击一个键，统一编辑普通和大写 / Shift 的第一键、第二键、单独输出及翻行功能。\n上：第一键　中：键帽　下：第二键与单独输出；⇧ 表示已自定义的 Shift 层。空值不显示。",
            TextWrapping = TextWrapping.Wrap, Foreground = Brush(180, 185, 198) });
        var keyboard = new StackPanel { Spacing = 6, Width = 1134 };
        var keyDefinitions = table is null ? null : new JapaneseInputTableStore(_store.RimeDirectory, _store.HotSettingsRegistryPath).KeyProfile(table.Id).Keys;
        var rows = new (string Key, string Caption, double Units)[][]
        {
            "`1234567890-=".Select(c => (c.ToString(), c.ToString(), 1d)).Append(("BackSpace", "退格", 2d)).ToArray(),
            new[] { ("Tab", "Tab", 1.5d) }.Concat("qwertyuiop[]".Select(c => (c.ToString(), c.ToString().ToUpperInvariant(), 1d))).Append(("\\", "\\", 1.5d)).ToArray(),
            new[] { ("CapsLock", "Caps", 1.75d) }.Concat("asdfghjkl;'".Select(c => (c.ToString(), c.ToString().ToUpperInvariant(), 1d))).Append(("Enter", "回车", 2.25d)).ToArray(),
            new[] { ("ShiftLeft", "Shift", 2.25d) }.Concat("zxcvbnm,./".Select(c => (c.ToString(), c.ToString().ToUpperInvariant(), 1d))).Append(("ShiftRight", "Shift", 2.75d)).ToArray(),
            [("CtrlLeft", "Ctrl", 1.25d), ("WinLeft", "Win", 1.25d), ("AltLeft", "Alt", 1.25d), ("Space", "空格", 6.25d),
                ("AltRight", "Alt", 1.25d), ("WinRight", "Win", 1.25d), ("Menu", "菜单", 1.25d), ("CtrlRight", "Ctrl", 1.25d)]
        };
        static string VisibleRole(string value) => value == "—" ? "" : value;
        foreach (var keys in rows)
        {
            var bottomRow = keys.Any(k => k.Key == "Space");
            var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6 };
            foreach (var (key, caption, units) in keys)
            {
                var editable = key.Length == 1 || key is "Space" or "Enter" or "BackSpace";
                var label = key.Length == 1 ? DescribeDiagramKey(key, table) : (First: "", Second: "", Details: caption);
                if (table is not null && editable)
                {
                    var definition = keyDefinitions?.GetValueOrDefault(key) ?? new JapaneseKeyDefinition();
                    label = (definition.First ?? label.First, definition.Second ?? label.Second, label.Details);
                    if (definition.EmptyFirst) label.First = "∅ 空声母";
                    if (definition.Action == "next") label.First = "下一行";
                    if (definition.Action == "previous") label.First = "上一行";
                    if (definition.Action == "literal") label.First = "直出";
                }
                label.First = VisibleRole(label.First); label.Second = VisibleRole(label.Second);
                var single = table is null || !editable ? "" : keyDefinitions?.GetValueOrDefault(key)?.Single ?? JapaneseInputTableStore.InferKey(key, table).Single;
                label.Details = "声母：" + label.First + "；韵尾：" + label.Second + "；单键：" + single + "\n" + label.Details;
                var width = units * 76 - 6;
                var content = new StackPanel { Spacing = 3, VerticalAlignment = VerticalAlignment.Center, Width = width - 8 };
                var shiftedKey = ShiftedJapaneseKey(key);
                content.Children.Add(new TextBlock { Text = shiftedKey is not null && !char.IsAsciiLetter(key[0]) ? shiftedKey : "",
                    FontSize = 11, Height = 14, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 0, 5, 0), Foreground = Brush(186, 178, 211) });
                content.Children.Add(new TextBlock { Text = label.First, FontSize = 14, HorizontalAlignment = HorizontalAlignment.Center,
                    Height = 19, Foreground = Brush(163, 195, 226), TextTrimming = TextTrimming.CharacterEllipsis, MaxWidth = width - 8 });
                content.Children.Add(new TextBlock { Text = caption, FontSize = key.Length == 1 ? 23 : 17,
                    FontWeight = Microsoft.UI.Text.FontWeights.SemiBold, HorizontalAlignment = HorizontalAlignment.Center });
                content.Children.Add(new TextBlock { Text = label.Second, FontSize = 14, HorizontalAlignment = HorizontalAlignment.Center,
                    Height = 19, TextTrimming = TextTrimming.CharacterEllipsis, MaxWidth = width - 8, Foreground = Brush(152, 222, 183) });
                content.Children.Add(new TextBlock { Text = single, FontSize = 11, Height = 15,
                    HorizontalAlignment = HorizontalAlignment.Center, MaxWidth = width - 8, TextTrimming = TextTrimming.CharacterEllipsis, Foreground = Brush(186, 178, 211) });
                var shiftedDefinition = shiftedKey is null ? null : keyDefinitions?.GetValueOrDefault(shiftedKey);
                var shiftedRoles = shiftedDefinition is null ? "" : string.Join(" · ", new[] { shiftedDefinition.First, shiftedDefinition.Second, shiftedDefinition.Single,
                    string.IsNullOrEmpty(shiftedDefinition.LongPress) ? null : "长按 " + shiftedDefinition.LongPress,
                    shiftedDefinition.Action switch { "next" => "下一行", "previous" => "上一行", "literal" => "直出", _ => null } }.Where(s => !string.IsNullOrEmpty(s)));
                content.Children.Add(new TextBlock { Text = shiftedRoles.Length == 0 ? "" : "⇧ " + shiftedRoles, Height = 14, FontSize = 10,
                    MaxWidth = width - 8, TextTrimming = TextTrimming.CharacterEllipsis, HorizontalAlignment = HorizontalAlignment.Center, Foreground = Brush(212, 180, 234) });
                if (shiftedKey is not null) label.Details += "\nShift + " + caption + " = " + shiftedKey + "\n" + (shiftedRoles.Length == 0 ? "Shift 层未自定义" : shiftedRoles);
                var hold = keyDefinitions?.GetValueOrDefault(key);
                if (!string.IsNullOrEmpty(hold?.LongPress))
                {
                    label.Details += "\n长按 " + hold.LongPressMilliseconds + " 毫秒后松手 → " + hold.LongPress;
                    ((TextBlock)content.Children[0]).Text = "长按 " + hold.LongPress;
                    ((TextBlock)content.Children[0]).TextTrimming = TextTrimming.CharacterEllipsis;
                    ((TextBlock)content.Children[0]).MaxWidth = width - 8;
                }
                var cap = new Button { Width = width, Height = bottomRow ? 72 : 138, CornerRadius = new CornerRadius(10), Padding = new Thickness(0),
                    BorderThickness = new Thickness(1), BorderBrush = Brush(70, 78, 95),
                    Background = label.Second == "" ? Brush(32, 34, 39) : Brush(35, 45, 59), Content = content, IsEnabled = table is not null && editable };
                cap.Resources["ButtonBackgroundDisabled"] = cap.Background;
                cap.Resources["ButtonForegroundDisabled"] = Brush(200, 205, 216);
                if (!editable) cap.Content = new TextBlock { Text = caption, FontSize = 17, Foreground = Brush(200, 205, 216) };
                if (key == "Space") cap.Content = new TextBlock { Text = caption + (label.First.Length == 0 ? "" : " · " + label.First), FontSize = 17 };
                Microsoft.UI.Xaml.Automation.AutomationProperties.SetAutomationId(cap, "EditJapaneseKey_" + (key.Length == 1 ? ((int)key[0]).ToString() : key));
                if (table is not null && editable) cap.Click += async (_, _) => await OpenJapaneseKeyEditorAsync(key, table);
                ToolTipService.SetToolTip(cap, new TextBlock { Text = label.Details, TextWrapping = TextWrapping.Wrap, MaxWidth = 420 });
                if (editable) row.Children.Add(cap);
                else row.Children.Add(new Border { Width = width, Height = bottomRow ? 72 : 138, CornerRadius = new CornerRadius(10),
                    BorderThickness = new Thickness(1), BorderBrush = Brush(70, 78, 95), Background = Brush(32, 34, 39),
                    Child = new TextBlock { Text = caption, FontSize = 17, HorizontalAlignment = HorizontalAlignment.Center,
                        VerticalAlignment = VerticalAlignment.Center, Foreground = Brush(200, 205, 216) } });
            }
            keyboard.Children.Add(row);
        }
        body.Children.Add(new Viewbox { Child = keyboard, Stretch = Microsoft.UI.Xaml.Media.Stretch.Uniform,
            HorizontalAlignment = HorizontalAlignment.Stretch });
        var functions = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        foreach (var (key, title) in new[] { ("PageUp", "PageUp 上一页"), ("PageDown", "PageDown 下一页") })
        {
            var button = new Button { Content = new TextBlock { Text = title, Foreground = Brush(230, 232, 240) },
                Background = Brush(32, 34, 39), BorderBrush = Brush(70, 78, 95), BorderThickness = new Thickness(1),
                Padding = new Thickness(12, 9, 12, 9), IsEnabled = table is not null };
            if (table is not null) button.Click += async (_, _) => await OpenJapaneseKeyEditorAsync(key, table);
            functions.Children.Add(button);
        }
        body.Children.Add(functions);
        if (table is null)
        {
            var create = new Button { Content = "新建可编辑罗马字方案" };
            create.Click += (_, _) =>
            {
                try { if (!_uiTestMode) new JapaneseInputTableStore(_store.RimeDirectory, _store.HotSettingsRegistryPath).CreateEditableRomaji(); SelectPage("japanese-table"); }
                catch (Exception error) { SetStatus(error.Message, true); }
            };
            body.Children.Add(create);
        }
        var examples = table?.Rules.Where(r => r.Input.Length >= 2 && r.Input.Length <= 4 && r.Pending.Length == 0 && r.Output.Length > 0)
            .Take(10).Select(r => r.Input + " → " + r.Output) ?? new[] { "ka → か", "kya → きゃ", "shi → し" };
        body.Children.Add(new TextBlock { Text = "实际规则示例：" + string.Join("　 /　", examples), TextWrapping = TextWrapping.Wrap });
        return body;
    }
}
