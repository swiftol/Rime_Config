using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.Win32;

namespace ZhongriSettingsV2;

public sealed partial class MainWindow
{
    private FrameworkElement BuildImeShortcuts()
    {
        var body = new StackPanel { Spacing = 10 };
        var fields = new List<(string Name, TextBox Editor)>();
        using var registry = Registry.CurrentUser.OpenSubKey(_store.HotSettingsRegistryPath);
        foreach (var (name, label, fallback) in new[] { ("Learning", "切换日语学习模式", "Ctrl+Alt+L"),
            ("NextProfile", "下一套日语配置", "Ctrl+Alt+PageDown"), ("PreviousProfile", "上一套日语配置", "Ctrl+Alt+PageUp") })
        {
            var editor = new TextBox { Text = registry?.GetValue("ShortcutText_" + name) as string ?? fallback, PlaceholderText = "留空禁用" };
            WatchExplicitEdit(editor);
            fields.Add((name, editor));
            body.Children.Add(ControlRow(label, "", editor));
        }
        body.Children.Add(new TextBlock { Text = "输入 Ctrl+Alt+字母 / F1～F12 / PageUp / PageDown，可加 Shift；留空禁用。切换配置时先上屏当前候选，再切换，含默认罗马字。", TextWrapping = TextWrapping.Wrap });
        var changed = AutoSave(() =>
        {
                var bindings = fields.Select(f => (f.Name, Text: f.Editor.Text.Trim(), Code: ParseImeShortcut(f.Editor.Text))).ToArray();
                var enabled = bindings.Where(b => b.Code != 0).ToArray();
                if (enabled.Select(b => b.Code).Distinct().Count() != enabled.Length) throw new ArgumentException("快捷键不能重复。");
                foreach (var binding in enabled)
                    if (new[] { 'E', 'J', 'F', 'T' }.Any(c => binding.Code == ((12 << 16) | c))) throw new ArgumentException("与现有 Ctrl+Alt+E/J/F/T 快捷键冲突。");
                if (!_uiTestMode)
                {
                    using var key = Registry.CurrentUser.CreateSubKey(_store.HotSettingsRegistryPath);
                    foreach (var binding in bindings) { key.SetValue("Shortcut_" + binding.Name, binding.Code, RegistryValueKind.DWord); key.SetValue("ShortcutText_" + binding.Name, binding.Text); }
                }
        }, delay: 650, values:()=>string.Join("|",fields.Select(f=>f.Editor.Text)));
        foreach (var field in fields) field.Editor.TextChanged += (_, _) => changed();
        return body;
    }
    internal static int ParseImeShortcut(string text)
    {
        if (string.IsNullOrWhiteSpace(text)) return 0;
        var parts = text.Replace(" ", "").Split('+');
        if (parts.Length < 3 || !parts.Any(p => p.Equals("Ctrl", StringComparison.OrdinalIgnoreCase)) || !parts.Any(p => p.Equals("Alt", StringComparison.OrdinalIgnoreCase)))
            throw new ArgumentException("请使用 Ctrl+Alt 组合快捷键。");
        var mask = 0;
        foreach (var part in parts[..^1])
        {
            var bit = part.ToLowerInvariant() switch { "ctrl" => 4, "alt" => 8, "shift" => 1, _ => throw new ArgumentException("不支持的修饰键。") };
            if ((mask & bit) != 0) throw new ArgumentException("修饰键不能重复。");
            mask |= bit;
        }
        var last = parts[^1].ToUpperInvariant();
        var code = last == "PAGEUP" ? 0xff55 : last == "PAGEDOWN" ? 0xff56 : last.Length == 1 && char.IsAsciiLetter(last[0]) ? last[0] :
            last.StartsWith('F') && int.TryParse(last[1..], out var number) && number is >= 1 and <= 12 ? 0xffbd + number : throw new ArgumentException("按键须为字母、F1～F12、PageUp 或 PageDown。");
        return (mask << 16) | code;
    }
}
