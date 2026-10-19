using System.Linq;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using RimeSettings;

namespace ZhongriSettingsV2;

public sealed partial class MainWindow
{
    private void ShowPhrases()
    {
        AddHeading("常用语", "管理固定短语及输入码；保存后立即作用于已打开的输入窗口。");
        var store = new PhraseStore(_store.RimeDirectory);
        var phrases = store.Load();
        var header = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 12 };
        var add = new Button { Content = "添加常用语",
            Style = (Style)Microsoft.UI.Xaml.Application.Current.Resources["AccentButtonStyle"] };
        header.Children.Add(add);
        _saveStatus = new TextBlock { Text = $"{phrases.Count} 条常用语 · {store.PhraseFile}",
            VerticalAlignment = VerticalAlignment.Center, TextWrapping = TextWrapping.Wrap,
            Foreground = Brush(165, 166, 177) };
        header.Children.Add(_saveStatus);
        PageContent.Children.Add(header);
        var editorHost = new StackPanel();
        var list = new StackPanel { Spacing = 12 };
        PageContent.Children.Add(editorHost);
        PageContent.Children.Add(list);

        async Task<bool> Confirm(string title, string message)
        {
            var dialog = new ContentDialog
            {
                XamlRoot = Root.XamlRoot,
                Title = title,
                Content = message,
                PrimaryButtonText = "继续",
                CloseButtonText = "取消",
                DefaultButton = ContentDialogButton.Close
            };
            return await dialog.ShowAsync() == ContentDialogResult.Primary;
        }

        async Task Persist()
        {
            if (_uiTestMode)
            {
                SetStatus("测试模式：已验证编辑，未保存或重启", false);
                Render();
                return;
            }
            try
            {
                SetStatus("正在保存并刷新候选…", false);
                store.Save(phrases);
                _store.PublishPhraseReload();
                await Task.CompletedTask;
                SetStatus($"已生效 · {phrases.Count} 条常用语", false);
                Render();
            }
            catch (Exception error)
            {
                phrases = store.Load();
                Render();
                SetStatus("保存失败：" + error.Message, true);
            }
        }

        void ShowEditor(int? index)
        {
            FlushPendingAutoSaves();
            editorHost.Children.Clear();
            var existing = index is null ? null : phrases[index.Value];
            var fields = new StackPanel { Spacing = 12 };
            var content = new TextBox { Header = "内容", Text = existing?.Content ?? "",
                AcceptsReturn = true, Height = 96, TextWrapping = TextWrapping.Wrap };
            var code = new TextBox { Header = "输入码", Text = existing?.Code ?? "",
                PlaceholderText = "留空可自动生成前三字首字母" };
            WatchExplicitEdit(content);
            WatchExplicitEdit(code);
            fields.Children.Add(content);
            fields.Children.Add(code);
            var actions = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10 };
            var save = new Button { Content = "添加",
                Style = (Style)Microsoft.UI.Xaml.Application.Current.Resources["AccentButtonStyle"] };
            var cancel = new Button { Content = index is null ? "取消" : "关闭编辑" };
            if (index is null) actions.Children.Add(save);
            actions.Children.Add(cancel);
            fields.Children.Add(actions);
            editorHost.Children.Add(Card(index is null ? "添加常用语" : "编辑常用语",
                "输入码只使用字母、数字、短横线或下划线。", fields));
            cancel.Click += (_, _) => { FlushPendingAutoSaves(); editorHost.Children.Clear(); };
            if (index is not null)
            {
                var changed = AutoSave(() => {
                    var value=content.Text.Replace('\r',' ').Replace('\n',' ').Trim();
                    var inputCode=code.Text.Trim().ToLowerInvariant();
                    if(value.Length==0) throw new ArgumentException("常用语不能为空，原内容未改变。");
                    if(inputCode.Length==0) inputCode=PhraseCodeGenerator.Generate(value,store.RimeDirectory);
                    if(inputCode.Length==0 || inputCode.Any(c=> c is not (>= 'a' and <= 'z' or >= '0' and <= '9' or '-' or '_')))
                        throw new ArgumentException("请填写有效输入码，原内容未改变。");
                    if(phrases.Where((_,i)=>i!=index.Value).Any(p=>p.Code.Equals(inputCode,StringComparison.OrdinalIgnoreCase)) && inputCode!=existing!.Code)
                        throw new ArgumentException("输入码已被其他常用语使用，原内容未改变。");
                    phrases[index.Value]=new CommonPhrase(value,inputCode,existing!.Weight);
                    store.Save(phrases); _store.PublishPhraseReload(); Render();
                }, delay:650, values:()=>content.Text+"|"+code.Text);
                content.TextChanged+=(_,_)=>changed(); code.TextChanged+=(_,_)=>changed();
            }
            save.Click += async (_, _) =>
            {
                var value = content.Text.Replace('\r', ' ').Replace('\n', ' ').Trim();
                var inputCode = code.Text.Trim().ToLowerInvariant();
                if (value.Length == 0) { SetStatus("常用语内容不能为空", true); return; }
                if (inputCode.Length == 0)
                    inputCode = PhraseCodeGenerator.Generate(value, store.RimeDirectory);
                if (inputCode.Length == 0 || inputCode.Any(c =>
                        c is not (>= 'a' and <= 'z' or >= '0' and <= '9' or '-' or '_')))
                {
                    SetStatus("无法生成输入码；请手动填写英文字母、数字、- 或 _", true);
                    return;
                }
                if (phrases.Where((_, i) => index != i).Any(p =>
                        p.Code.Equals(inputCode, StringComparison.OrdinalIgnoreCase)) &&
                    !await Confirm("输入码已存在", "继续添加相同输入码的常用语吗？")) return;
                var phrase = new CommonPhrase(value, inputCode, existing?.Weight ?? 1000000);
                if (index is null) phrases.Insert(0, phrase);
                else phrases[index.Value] = phrase;
                editorHost.Children.Clear();
                await Persist();
            };
        }

        void Render()
        {
            list.Children.Clear();
            if (phrases.Count == 0)
            {
                list.Children.Add(Info("还没有常用语。点击“添加常用语”开始。"));
                return;
            }
            for (var i = 0; i < phrases.Count; i++)
            {
                var index = i;
                var phrase = phrases[i];
                var row = new Grid { ColumnSpacing = 12 };
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                var labels = new StackPanel { Spacing = 5 };
                labels.Children.Add(new TextBlock { Text = phrase.Content, TextWrapping = TextWrapping.Wrap,
                    FontSize = 15 });
                labels.Children.Add(new TextBlock { Text = "输入码  " + phrase.Code,
                    Foreground = Brush(165, 166, 177) });
                var edit = new Button { Content = "编辑" };
                var delete = new Button { Content = "删除" };
                Grid.SetColumn(labels, 0);
                Grid.SetColumn(edit, 1);
                Grid.SetColumn(delete, 2);
                row.Children.Add(labels);
                row.Children.Add(edit);
                row.Children.Add(delete);
                list.Children.Add(Card("", "", row));
                edit.Click += (_, _) => ShowEditor(index);
                delete.Click += async (_, _) =>
                {
                    if (!await Confirm("删除常用语", $"确定删除“{phrase.Content}”吗？")) return;
                    FlushPendingAutoSaves(); editorHost.Children.Clear();
                    phrases.RemoveAt(index);
                    await Persist();
                };
            }
        }

        add.Click += (_, _) => ShowEditor(null);
        Render();
    }
}
