using System.Linq;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using RimeSettings;

namespace ZhongriSettingsV2;

public sealed partial class MainWindow
{
    private void ShowCustomFuzzyRules()
    {
        AddHeading("自定义模糊匹配", "罗马字规则默认双向匹配；日语和中文分别管理。");
        var store = new CustomFuzzyRuleStore(_store.RimeDirectory);
        var japanese = store.Load(FuzzyLanguage.Japanese);
        var chinese = store.Load(FuzzyLanguage.Chinese);
        var current = FuzzyLanguage.Japanese;
        var toolbar = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10 };
        var ja = new Button { Content = "日语规则" };
        var cn = new Button { Content = "中文规则" };
        var add = new Button { Content = "新增规则" };
        toolbar.Children.Add(ja);
        toolbar.Children.Add(cn);
        toolbar.Children.Add(add);
        PageContent.Children.Add(toolbar);
        _saveStatus = new TextBlock { Text = "修改后自动保存，日语和中文规则均热更新，无需部署。",
            Foreground = Brush(165, 166, 177), TextWrapping = TextWrapping.Wrap };
        PageContent.Children.Add(_saveStatus);
        var editorHost = new StackPanel();
        var list = new StackPanel { Spacing = 12 };
        PageContent.Children.Add(editorHost);
        PageContent.Children.Add(list);

        var saveJapanese = AutoSave(() => {
            store.Save(FuzzyLanguage.Japanese, japanese);
            _store.PublishJapaneseFuzzyReload();
        });
        var saveChinese = AutoSave(() => { store.Save(FuzzyLanguage.Chinese, chinese); _store.PublishChineseFuzzyReload(); });
        void Changed(FuzzyLanguage language) { if (language == FuzzyLanguage.Japanese) saveJapanese(); else saveChinese(); }

        List<CustomFuzzyRule> Active() => current == FuzzyLanguage.Japanese ? japanese : chinese;

        async Task<bool> ConfirmDelete(CustomFuzzyRule rule)
        {
            var dialog = new ContentDialog
            {
                XamlRoot = Root.XamlRoot,
                Title = "删除模糊规则？",
                Content = $"将删除 {rule.Left} ↔ {rule.Right}，确认后自动保存。",
                PrimaryButtonText = "删除",
                CloseButtonText = "取消",
                DefaultButton = ContentDialogButton.Close
            };
            return await dialog.ShowAsync() == ContentDialogResult.Primary;
        }

        void ShowEditor(int? index)
        {
            FlushPendingAutoSaves();
            editorHost.Children.Clear();
            var rules = Active();
            var language = current;
            var existing = index is null ? null : rules[index.Value];
            var fields = new StackPanel { Spacing = 12 };
            var left = new TextBox { Header = "第一组罗马字", Text = existing?.Left ?? "",
                PlaceholderText = "例如 hu" };
            var right = new TextBox { Header = "第二组罗马字", Text = existing?.Right ?? "",
                PlaceholderText = "例如 fu" };
            WatchExplicitEdit(left);
            WatchExplicitEdit(right);
            fields.Children.Add(left);
            fields.Children.Add(right);
            var buttons = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10 };
            var save = new Button { Content = "添加",
                Style = (Style)Microsoft.UI.Xaml.Application.Current.Resources["AccentButtonStyle"] };
            var cancel = new Button { Content = index is null ? "取消" : "关闭编辑" };
            if (index is null) buttons.Children.Add(save);
            buttons.Children.Add(cancel);
            fields.Children.Add(buttons);
            editorHost.Children.Add(Card(index is null ? "新增规则" : "编辑规则",
                "仅接受 1–16 位英文字母，不能填写相同的两组拼写。", fields));
            cancel.Click += (_, _) => { FlushPendingAutoSaves(); editorHost.Children.Clear(); };
            if (index is not null)
            {
                var changed = AutoSave(() => {
                    var a = CustomFuzzyRuleStore.Normalize(left.Text);
                    var b = CustomFuzzyRuleStore.Normalize(right.Text);
                    if (!CustomFuzzyRuleStore.IsValid(a,b)) throw new ArgumentException("请填写不同的 1–16 位英文字母，原规则未改变。");
                    if (rules.Where((_,i)=> i!=index.Value).Any(rule => rule.Left==a && rule.Right==b || rule.Left==b && rule.Right==a))
                        throw new ArgumentException("这条双向规则已经存在。");
                    rules[index.Value] = rules[index.Value] with { Left=a, Right=b };
                    if (language==FuzzyLanguage.Japanese) { store.Save(language,rules); _store.PublishJapaneseFuzzyReload(); }
                    else store.Save(language,rules);
                    Render();
                }, delay:650, deploy:language==FuzzyLanguage.Chinese, values:()=>left.Text+"|"+right.Text);
                left.TextChanged += (_,_)=>changed(); right.TextChanged += (_,_)=>changed();
            }
            save.Click += (_, _) =>
            {
                var a = CustomFuzzyRuleStore.Normalize(left.Text);
                var b = CustomFuzzyRuleStore.Normalize(right.Text);
                if (!CustomFuzzyRuleStore.IsValid(a, b))
                {
                    SetStatus("规则只能使用 1–16 位英文字母，且两边不能相同。", true);
                    return;
                }
                if (rules.Where((_, i) => index != i).Any(rule =>
                    rule.Left == a && rule.Right == b || rule.Left == b && rule.Right == a))
                {
                    SetStatus("这条双向规则已经存在。", true);
                    return;
                }
                var rule = new CustomFuzzyRule(existing?.Enabled ?? true, a, b);
                if (index is null) rules.Add(rule);
                else rules[index.Value] = rule;
                editorHost.Children.Clear();
                Render();
                Changed(language);
            };
        }

        void Render()
        {
            list.Children.Clear();
            ja.Background = current == FuzzyLanguage.Japanese ? Brush(79, 74, 112) : Brush(44, 44, 48);
            cn.Background = current == FuzzyLanguage.Chinese ? Brush(79, 74, 112) : Brush(44, 44, 48);
            var rules = Active();
            if (rules.Count == 0)
            {
                list.Children.Add(Info("还没有自定义规则。点击“新增规则”开始。"));
                return;
            }
            for (var i = 0; i < rules.Count; i++)
            {
                var index = i;
                var rule = rules[i];
                var row = new Grid { ColumnSpacing = 12 };
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                var label = new TextBlock { Text = $"{rule.Left}  ↔  {rule.Right}",
                    VerticalAlignment = VerticalAlignment.Center, FontSize = 15 };
                var enabled = new ToggleSwitch { IsOn = rule.Enabled, OnContent = "", OffContent = "" };
                var edit = new Button { Content = "编辑" };
                var delete = new Button { Content = "删除" };
                Grid.SetColumn(label, 0);
                Grid.SetColumn(enabled, 1);
                Grid.SetColumn(edit, 2);
                Grid.SetColumn(delete, 3);
                row.Children.Add(label);
                row.Children.Add(enabled);
                row.Children.Add(edit);
                row.Children.Add(delete);
                list.Children.Add(Card("", "", row));
                enabled.Toggled += (_, _) =>
                {
                    rules[index] = rules[index] with { Enabled = enabled.IsOn };
                    Changed(current);
                };
                edit.Click += (_, _) => ShowEditor(index);
                delete.Click += async (_, _) =>
                {
                    if (!await ConfirmDelete(rule)) return;
                    FlushPendingAutoSaves(); editorHost.Children.Clear();
                    rules.RemoveAt(index);
                    Render();
                    Changed(current);
                };
            }
        }

        ja.Click += (_, _) => { FlushPendingAutoSaves(); current = FuzzyLanguage.Japanese; editorHost.Children.Clear(); Render(); };
        cn.Click += (_, _) => { FlushPendingAutoSaves(); current = FuzzyLanguage.Chinese; editorHost.Children.Clear(); Render(); };
        add.Click += (_, _) => ShowEditor(null);
        Render();
    }
}
