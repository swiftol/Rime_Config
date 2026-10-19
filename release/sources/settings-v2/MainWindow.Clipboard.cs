using System.IO;
using System.Linq;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media.Imaging;
using RimeSettings;
using Windows.ApplicationModel.DataTransfer;
using Windows.Graphics.Imaging;
using Windows.Storage;
using Windows.Storage.Streams;

namespace ZhongriSettingsV2;

public sealed partial class MainWindow
{
    private EventHandler<object>? _clipboardListener;
    private bool _capturingClipboard;
    private bool _ignoreNextClipboardChange;

    private void StopClipboardListener()
    {
        if (_clipboardListener is null) return;
        Windows.ApplicationModel.DataTransfer.Clipboard.ContentChanged -= _clipboardListener;
        _clipboardListener = null;
    }

    private void ShowClipboard()
    {
        AddHeading("剪贴板", "设置页打开期间记录最近复制的文字和图片，最多保留 50 条。");
        var store = new ClipboardHistoryStore(_store.RimeDirectory);
        var entries = store.Load();
        var toolbar = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 12 };
        var clear = new Button { Content = "清空历史" };
        toolbar.Children.Add(clear);
        _saveStatus = new TextBlock { Text = $"{entries.Count} 条记录",
            VerticalAlignment = VerticalAlignment.Center, TextWrapping = TextWrapping.Wrap,
            Foreground = Brush(165, 166, 177) };
        toolbar.Children.Add(_saveStatus);
        PageContent.Children.Add(toolbar);
        var list = new StackPanel { Spacing = 12 };
        PageContent.Children.Add(list);

        async Task<bool> Confirm(string title, string message)
        {
            var dialog = new ContentDialog
            {
                XamlRoot = Root.XamlRoot,
                Title = title,
                Content = message,
                PrimaryButtonText = "删除",
                CloseButtonText = "取消",
                DefaultButton = ContentDialogButton.Close
            };
            return await dialog.ShowAsync() == ContentDialogResult.Primary;
        }

        async Task CopyEntry(ClipboardEntry entry)
        {
            try
            {
                var package = new DataPackage();
                if (entry.Kind == "text" && entry.Text is not null)
                    package.SetText(entry.Text);
                else if (entry.Kind == "image" && entry.ImagePath is not null && File.Exists(entry.ImagePath))
                {
                    var file = await StorageFile.GetFileFromPathAsync(entry.ImagePath);
                    package.SetBitmap(RandomAccessStreamReference.CreateFromFile(file));
                }
                else throw new FileNotFoundException("剪贴板记录的原始内容已不存在。", entry.ImagePath);
                _ignoreNextClipboardChange = true;
                Windows.ApplicationModel.DataTransfer.Clipboard.SetContent(package);
                Windows.ApplicationModel.DataTransfer.Clipboard.Flush();
                SetStatus("已复制，可在其他程序中粘贴", false);
            }
            catch (Exception error) { SetStatus("复制失败：" + error.Message, true); }
        }

        void Render()
        {
            list.Children.Clear();
            if (entries.Count == 0)
            {
                list.Children.Add(Info("剪贴板历史为空。保持此页打开，复制文字或图片即可记录。"));
                return;
            }
            foreach (var entry in entries.ToList())
            {
                var row = new Grid { ColumnSpacing = 12 };
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                var detail = new StackPanel { Spacing = 5 };
                if (entry.Kind == "image" && entry.ImagePath is not null && File.Exists(entry.ImagePath))
                {
                    detail.Children.Add(new Image { Source = new BitmapImage(new Uri(entry.ImagePath)),
                        Width = 112, Height = 76, HorizontalAlignment = HorizontalAlignment.Left });
                }
                detail.Children.Add(new TextBlock { Text = entry.Preview, MaxLines = 2,
                    TextTrimming = TextTrimming.CharacterEllipsis, TextWrapping = TextWrapping.Wrap,
                    FontSize = 15 });
                detail.Children.Add(new TextBlock { Text = entry.CreatedAt.ToString("MM-dd HH:mm"),
                    Foreground = Brush(165, 166, 177) });
                var copy = new Button { Content = "复制" };
                var delete = new Button { Content = "删除" };
                Grid.SetColumn(detail, 0);
                Grid.SetColumn(copy, 1);
                Grid.SetColumn(delete, 2);
                row.Children.Add(detail);
                row.Children.Add(copy);
                row.Children.Add(delete);
                list.Children.Add(Card("", "", row));
                copy.Click += async (_, _) => await CopyEntry(entry);
                delete.Click += async (_, _) =>
                {
                    if (!await Confirm("删除剪贴板记录", "这条记录及其保存的图片将被删除。")) return;
                    if (_uiTestMode) { SetStatus("测试模式：未删除记录", false); return; }
                    store.Delete(entries, entry);
                    Render();
                    SetStatus($"已删除 · 剩余 {entries.Count} 条", false);
                };
            }
        }

        async Task Capture()
        {
            if (_capturingClipboard || _uiTestMode ||
                Environment.GetCommandLineArgs().Contains("--self-test", StringComparer.Ordinal)) return;
            _capturingClipboard = true;
            try
            {
                var content = Windows.ApplicationModel.DataTransfer.Clipboard.GetContent();
                if (content.Contains(StandardDataFormats.Text))
                {
                    var value = await content.GetTextAsync();
                    store.AddText(entries, value);
                }
                else if (content.Contains(StandardDataFormats.Bitmap))
                {
                    var reference = await content.GetBitmapAsync();
                    using var stream = await reference.OpenReadAsync();
                    if (stream.Size > 20_000_000) return;
                    var decoder = await BitmapDecoder.CreateAsync(stream);
                    using var bitmap = await decoder.GetSoftwareBitmapAsync();
                    using var pngStream = new InMemoryRandomAccessStream();
                    var encoder = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, pngStream);
                    encoder.SetSoftwareBitmap(bitmap);
                    await encoder.FlushAsync();
                    if (pngStream.Size > 20_000_000) return;
                    pngStream.Seek(0);
                    using var reader = new DataReader(pngStream);
                    await reader.LoadAsync((uint)pngStream.Size);
                    var bytes = new byte[(int)pngStream.Size];
                    reader.ReadBytes(bytes);
                    store.AddImagePng(entries, bytes, (int)decoder.PixelWidth, (int)decoder.PixelHeight);
                }
                else return;
                Render();
                SetStatus($"已记录 · {entries.Count} 条", false);
            }
            catch (Exception error) { SetStatus("剪贴板读取失败：" + error.Message, true); }
            finally { _capturingClipboard = false; }
        }

        clear.Click += async (_, _) =>
        {
            if (entries.Count == 0) return;
            if (!await Confirm("清空剪贴板历史", "所有文字记录和保存的图片都会删除，无法撤销。")) return;
            if (_uiTestMode) { SetStatus("测试模式：未清空历史", false); return; }
            store.Clear(entries);
            Render();
            SetStatus("历史已清空", false);
        };
        Render();
        if (_uiTestMode || Environment.GetCommandLineArgs().Contains("--self-test", StringComparer.Ordinal))
            return;
        _clipboardListener = (_, _) =>
        {
            if (_ignoreNextClipboardChange) { _ignoreNextClipboardChange = false; return; }
            DispatcherQueue.TryEnqueue(async () => await Capture());
        };
        Windows.ApplicationModel.DataTransfer.Clipboard.ContentChanged += _clipboardListener;
    }
}
