using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace RimeSettings;

internal sealed class MainForm : Form
{
    private const int WmClipboardUpdate = 0x031D;

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool AddClipboardFormatListener(IntPtr hwnd);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool RemoveClipboardFormatListener(IntPtr hwnd);

    private readonly PhraseStore _phraseStore = new();
    private readonly SettingsStore _settings;
    private readonly ChineseCorrectionStore _chineseCorrections;
    private readonly ClipboardHistoryStore _clipboardStore;
    private readonly List<CommonPhrase> _phrases;
    private readonly List<ClipboardEntry> _clipboardEntries;
    private readonly Dictionary<string, FlatButton> _navButtons = [];
    private readonly Panel _content = new() { Dock = DockStyle.Fill, BackColor = Theme.Window };
    private FlowLayoutPanel? _phraseList;
    private FlowLayoutPanel? _clipboardList;
    private Panel? _phraseHost;
    private Panel? _phraseHeader;
    private Panel? _phraseTabs;
    private Panel? _phraseToolbar;
    private Label? _pageStatus;
    private bool _clipboardListenerRegistered;
    private bool _capturingClipboard;

    public MainForm()
    {
        _settings = new SettingsStore(_phraseStore.RimeDirectory);
        _chineseCorrections = new ChineseCorrectionStore(_phraseStore.RimeDirectory);
        _clipboardStore = new ClipboardHistoryStore(_phraseStore.RimeDirectory);
        _phrases = _phraseStore.Load();
        _clipboardEntries = _clipboardStore.Load();

        Text = "雾凇拼音·中日混输输入法设置";
        ClientSize = new Size(1100, 720);
        MinimumSize = new Size(920, 620);
        BackColor = Theme.Window;
        ForeColor = Theme.Text;
        Font = Theme.Font(10);
        StartPosition = FormStartPosition.CenterScreen;
        FormBorderStyle = FormBorderStyle.Sizable;
        DoubleBuffered = true;

        Controls.Add(_content);
        Controls.Add(BuildSidebar());
        ShowSettings();
    }

    protected override void OnHandleCreated(EventArgs e)
    {
        base.OnHandleCreated(e);
        _clipboardListenerRegistered = AddClipboardFormatListener(Handle);
    }

    protected override void OnHandleDestroyed(EventArgs e)
    {
        if (_clipboardListenerRegistered)
            RemoveClipboardFormatListener(Handle);
        _clipboardListenerRegistered = false;
        base.OnHandleDestroyed(e);
    }

    protected override void WndProc(ref Message m)
    {
        base.WndProc(ref m);
        if (m.Msg == WmClipboardUpdate)
            BeginInvoke(CaptureClipboard);
    }

    private Panel BuildSidebar()
    {
        var sidebar = new Panel
        {
            Dock = DockStyle.Left,
            Width = 236,
            BackColor = Theme.Sidebar,
            Padding = new Padding(16, 18, 16, 18)
        };
        var logo = new Label
        {
            Text = "❄  雾凇中日",
            ForeColor = Theme.Text,
            BackColor = Theme.Sidebar,
            Font = Theme.Font(14, FontStyle.Bold),
            Height = 58,
            Dock = DockStyle.Top,
            TextAlign = ContentAlignment.MiddleLeft
        };
        var nav = new FlowLayoutPanel
        {
            Dock = DockStyle.Top,
            Height = 390,
            FlowDirection = FlowDirection.TopDown,
            WrapContents = false,
            BackColor = Theme.Sidebar,
            Padding = new Padding(0, 16, 0, 0)
        };
        AddNav(nav, "⌨   输入", "settings", ShowSettings);
        AddNav(nav, "≈   中文模糊纠错", "chinese-fuzzy", ShowChineseCorrections);
        AddNav(nav, "◐   外观", "appearance", ShowAppearance);
        AddNav(nav, "↻   维护", "maintenance", ShowMaintenance);
        AddMoreNav(nav);

        var footer = new Label
        {
            Dock = DockStyle.Bottom,
            Height = 62,
            Text = "当前开发版\n配置：用户 Rime 目录",
            ForeColor = Theme.Muted,
            TextAlign = ContentAlignment.MiddleLeft,
            Font = Theme.Font(8.5f)
        };
        sidebar.Controls.Add(footer);
        sidebar.Controls.Add(nav);
        sidebar.Controls.Add(logo);
        return sidebar;
    }

    private void AddNav(FlowLayoutPanel parent, string text, string key, Action action)
    {
        var button = new FlatButton
        {
            Text = text,
            TextAlign = ContentAlignment.MiddleLeft,
            Width = 204,
            Height = 48,
            Margin = new Padding(0, 3, 0, 3),
            Padding = new Padding(14, 0, 0, 0),
            BackColor = Theme.Sidebar
        };
        button.Click += (_, _) => { SelectNav(key); action(); };
        _navButtons[key] = button;
        parent.Controls.Add(button);
    }

    private void AddMoreNav(FlowLayoutPanel parent)
    {
        var button = new FlatButton
        {
            Text = "更多（开发中）",
            TextAlign = ContentAlignment.MiddleLeft,
            Width = 204,
            Height = 48,
            Margin = new Padding(0, 3, 0, 3),
            Padding = new Padding(14, 0, 0, 0),
            BackColor = Theme.Sidebar
        };
        var menu = new ContextMenuStrip
        {
            BackColor = Theme.Surface,
            ForeColor = Theme.Text,
            Font = Theme.Font(10),
            ShowImageMargin = false,
            Padding = new Padding(4)
        };
        var phrases = menu.Items.Add("常用语与剪贴板");
        phrases.Click += (_, _) => ShowPhrases();
        var hotkeys = menu.Items.Add("快捷键");
        hotkeys.Click += (_, _) => ShowHotkeys();
        var sentenceTranslation = menu.Items.Add("实时句子翻译");
        sentenceTranslation.Click += (_, _) => ShowSentenceTranslation();
        button.Click += (_, _) => menu.Show(button, new Point(button.Width - menu.Width, button.Height));
        _navButtons["more"] = button;
        parent.Controls.Add(button);
    }

    private void SelectNav(string key)
    {
        if (key is "phrases" or "hotkeys" or "sentence-translation") key = "more";
        foreach (var pair in _navButtons)
        {
            pair.Value.BackColor = pair.Key == key ? Theme.AccentDark : Theme.Sidebar;
            pair.Value.ForeColor = pair.Key == key ? Color.White : Theme.Text;
        }
    }

    private void ClearContent()
    {
        _content.Controls.Clear();
        _content.AutoScroll = false;
        _content.AutoScrollMinSize = Size.Empty;
        _content.Padding = new Padding(38, 24, 38, 28);
        _pageStatus = null;
        _phraseList = null;
        _clipboardList = null;
        _phraseHost = null;
        _phraseHeader = null;
        _phraseTabs = null;
        _phraseToolbar = null;
    }

    private static Panel CreateHeading(string title, string subtitle)
    {
        // Fixed 38px title labels clip the bottom of 20pt CJK glyphs at
        // 125%/150% Windows scaling.  Reserve the actual scaled line height.
        var panel = new Panel { Height = 96, BackColor = Theme.Window };
        panel.Controls.Add(new Label
        {
            Text = title,
            Left = 0,
            Top = 0,
            Width = 680,
            Height = 52,
            Font = Theme.Font(20, FontStyle.Bold),
            ForeColor = Theme.Text
        });
        panel.Controls.Add(new Label
        {
            Text = subtitle,
            Left = 1,
            Top = 53,
            Width = 760,
            Height = 31,
            Font = Theme.Font(9.5f),
            ForeColor = Theme.Muted
        });
        return panel;
    }

    private void BuildHeading(string title, string subtitle)
    {
        var panel = CreateHeading(title, subtitle);
        panel.Dock = DockStyle.Top;
        _content.Controls.Add(panel);
        panel.BringToFront();
    }

    private void ShowPhrases() => ShowPhraseSection(false);

    private void ShowPhraseSection(bool clipboard)
    {
        SelectNav("phrases");
        ClearContent();
        _phraseHost = new Panel { Dock = DockStyle.Fill, BackColor = Theme.Window };
        _content.Controls.Add(_phraseHost);
        _phraseHeader = CreateHeading("常用语和剪贴板", "像微信输入法一样管理固定短语，并保存最近复制的文字和图片。");

        var tabs = new Panel { Height = 52, BackColor = Theme.Window };
        var phrasesTab = TabButton("常用语", 0, !clipboard);
        var clipboardTab = TabButton("剪贴板", 132, clipboard);
        phrasesTab.Click += (_, _) => ShowPhraseSection(false);
        clipboardTab.Click += (_, _) => ShowPhraseSection(true);
        tabs.Controls.AddRange([phrasesTab, clipboardTab]);
        _phraseTabs = tabs;
        _phraseHost.Controls.AddRange([_phraseHeader, tabs]);

        if (clipboard)
            BuildClipboardPage();
        else
            BuildPhrasePage();
        LayoutPhraseHost();
        _phraseHost.Resize += (_, _) => LayoutPhraseHost();
    }

    private static FlatButton TabButton(string text, int left, bool selected) => new()
    {
        Text = text,
        Left = left,
        Top = 5,
        Width = 122,
        Height = 38,
        BackColor = selected ? Theme.Input : Theme.Surface,
        ForeColor = selected ? Theme.Text : Theme.Muted
    };

    private void BuildPhrasePage()
    {
        var toolbar = BuildBottomToolbar();
        var add = new FlatButton
        {
            Text = "＋  添加",
            BackColor = Theme.Accent,
            Width = 108,
            Height = 38,
            Anchor = AnchorStyles.Right | AnchorStyles.Top
        };
        add.Location = new Point(toolbar.Width - add.Width, 10);
        toolbar.Resize += (_, _) => add.Left = toolbar.ClientSize.Width - add.Width;
        add.Click += (_, _) => AddPhrase();
        _pageStatus = BuildStatusLabel($"{_phrases.Count} 条常用语  ·  输入码会优先显示在候选栏");
        toolbar.Controls.AddRange([_pageStatus, add]);

        _phraseList = BuildList();
        _phraseToolbar = toolbar;
        _phraseHost!.Controls.AddRange([_phraseList, toolbar]);
        RenderPhrases();
    }

    private void RenderPhrases()
    {
        if (_phraseList is null) return;
        _phraseList.SuspendLayout();
        _phraseList.Controls.Clear();
        var width = Math.Max(620, _phraseList.ClientSize.Width - 28);
        if (_phrases.Count == 0)
            _phraseList.Controls.Add(BuildEmptyState("还没有常用语", "点击右下角“添加”，设置内容和输入码。", width));
        for (var i = 0; i < _phrases.Count; i++)
            _phraseList.Controls.Add(BuildPhraseCard(i, _phrases[i], width));
        _phraseList.ResumeLayout();
        if (_pageStatus is not null)
            _pageStatus.Text = $"{_phrases.Count} 条常用语  ·  {_phraseStore.PhraseFile}";
    }

    private Control BuildPhraseCard(int index, CommonPhrase phrase, int width)
    {
        var card = new RoundedPanel
        {
            Width = width,
            Height = 92,
            Radius = 10,
            BackColor = Theme.Surface,
            Margin = new Padding(0, 0, 0, 10)
        };
        var content = new Label
        {
            Text = phrase.Content,
            Left = 18,
            Top = 13,
            Width = width - 155,
            Height = 34,
            AutoEllipsis = true,
            Font = Theme.Font(11),
            ForeColor = Theme.Text
        };
        var code = new Label
        {
            Text = $"输入码   {phrase.Code}",
            Left = 18,
            Top = 54,
            Width = width - 155,
            Height = 24,
            ForeColor = Theme.Muted,
            Font = Theme.Font(9)
        };
        var edit = ActionButton("✎", width - 106, Theme.Input, Theme.Text);
        var delete = ActionButton("×", width - 57, Color.FromArgb(79, 43, 47), Theme.Danger);
        edit.Click += (_, _) => EditPhrase(index);
        delete.Click += (_, _) => DeletePhrase(index);
        card.Controls.AddRange([content, code, edit, delete]);
        return card;
    }

    private void AddPhrase()
    {
        using var dialog = new PhraseDialog();
        if (dialog.ShowDialog(this) != DialogResult.OK || dialog.Result is null) return;
        if (_phrases.Any(p => p.Code.Equals(dialog.Result.Code, StringComparison.OrdinalIgnoreCase)) &&
            MessageBox.Show(this, "这个输入码已经存在，仍然添加吗？", "常用语", MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes)
            return;
        _phrases.Insert(0, dialog.Result);
        PersistPhrases();
    }

    private void EditPhrase(int index)
    {
        using var dialog = new PhraseDialog(_phrases[index]);
        if (dialog.ShowDialog(this) != DialogResult.OK || dialog.Result is null) return;
        _phrases[index] = dialog.Result;
        PersistPhrases();
    }

    private void DeletePhrase(int index)
    {
        if (MessageBox.Show(this, $"确定删除“{_phrases[index].Content}”吗？", "常用语", MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes)
            return;
        _phrases.RemoveAt(index);
        PersistPhrases();
    }

    private async void PersistPhrases()
    {
        try
        {
            _phraseStore.Save(_phrases);
            RenderPhrases();
            if (_pageStatus is not null) _pageStatus.Text = "已保存，正在刷新输入法…";
            await RimeRuntime.RestartServerAsync();
            if (_pageStatus is not null) _pageStatus.Text = $"已生效  ·  {_phrases.Count} 条常用语";
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, "保存失败：" + ex.Message, "常用语", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private void BuildClipboardPage()
    {
        var toolbar = BuildBottomToolbar();
        var clear = new FlatButton
        {
            Text = "清空历史",
            BackColor = Color.FromArgb(79, 43, 47),
            ForeColor = Theme.Danger,
            Width = 108,
            Height = 38,
            Anchor = AnchorStyles.Right | AnchorStyles.Top
        };
        clear.Location = new Point(toolbar.Width - clear.Width, 10);
        toolbar.Resize += (_, _) => clear.Left = toolbar.ClientSize.Width - clear.Width;
        clear.Click += (_, _) => ClearClipboardHistory();
        _pageStatus = BuildStatusLabel($"{_clipboardEntries.Count} 条记录  ·  面板运行时自动记录，最多保留 50 条");
        toolbar.Controls.AddRange([_pageStatus, clear]);

        _clipboardList = BuildList();
        _phraseToolbar = toolbar;
        _phraseHost!.Controls.AddRange([_clipboardList, toolbar]);
        RenderClipboard();
        CaptureClipboard();
    }

    private void RenderClipboard()
    {
        if (_clipboardList is null) return;
        _clipboardList.SuspendLayout();
        _clipboardList.Controls.Clear();
        var width = Math.Max(620, _clipboardList.ClientSize.Width - 28);
        if (_clipboardEntries.Count == 0)
            _clipboardList.Controls.Add(BuildEmptyState("剪贴板还是空的", "复制一段文字或一张图片，它会出现在这里。", width));
        foreach (var entry in _clipboardEntries)
            _clipboardList.Controls.Add(BuildClipboardCard(entry, width));
        _clipboardList.ResumeLayout();
        if (_pageStatus is not null)
            _pageStatus.Text = $"{_clipboardEntries.Count} 条记录  ·  文字和图片均可再次复制";
    }

    private Control BuildClipboardCard(ClipboardEntry entry, int width)
    {
        var card = new RoundedPanel
        {
            Width = width,
            Height = entry.Kind == "image" ? 112 : 92,
            Radius = 10,
            BackColor = Theme.Surface,
            Margin = new Padding(0, 0, 0, 10)
        };
        var textLeft = 18;
        if (entry.Kind == "image" && !string.IsNullOrWhiteSpace(entry.ImagePath) && File.Exists(entry.ImagePath))
        {
            var image = Image.FromFile(entry.ImagePath);
            var preview = new PictureBox
            {
                Left = 14,
                Top = 12,
                Width = 112,
                Height = 88,
                Image = new Bitmap(image),
                SizeMode = PictureBoxSizeMode.Zoom,
                BackColor = Theme.Input
            };
            image.Dispose();
            card.Controls.Add(preview);
            textLeft = 144;
        }
        var previewText = new Label
        {
            Text = entry.Preview,
            Left = textLeft,
            Top = 14,
            Width = width - textLeft - 140,
            Height = entry.Kind == "image" ? 52 : 42,
            AutoEllipsis = true,
            Font = Theme.Font(10.5f),
            ForeColor = Theme.Text
        };
        var time = new Label
        {
            Text = entry.CreatedAt.ToString("MM-dd  HH:mm"),
            Left = textLeft,
            Top = entry.Kind == "image" ? 76 : 60,
            Width = 180,
            Height = 24,
            ForeColor = Theme.Muted,
            Font = Theme.Font(8.5f)
        };
        var copy = ActionButton("复制", width - 120, Theme.AccentDark, Color.White, 58);
        var delete = ActionButton("×", width - 55, Color.FromArgb(79, 43, 47), Theme.Danger);
        copy.Top = (card.Height - copy.Height) / 2;
        delete.Top = (card.Height - delete.Height) / 2;
        copy.Click += (_, _) => CopyClipboardEntry(entry);
        delete.Click += (_, _) => { _clipboardStore.Delete(_clipboardEntries, entry); RenderClipboard(); };
        card.Controls.AddRange([previewText, time, copy, delete]);
        return card;
    }

    private async void CaptureClipboard()
    {
        if (_capturingClipboard) return;
        _capturingClipboard = true;
        try
        {
            for (var attempt = 0; attempt < 3; attempt++)
            {
                try
                {
                    if (Clipboard.ContainsText(TextDataFormat.UnicodeText))
                    {
                        _clipboardStore.AddText(_clipboardEntries, Clipboard.GetText(TextDataFormat.UnicodeText));
                        RenderClipboard();
                    }
                    else if (Clipboard.ContainsImage())
                    {
                        using var image = Clipboard.GetImage();
                        if (image is not null)
                        {
                            _clipboardStore.AddImage(_clipboardEntries, image);
                            RenderClipboard();
                        }
                    }
                    break;
                }
                catch (ExternalException)
                {
                    await Task.Delay(80);
                }
            }
        }
        finally
        {
            _capturingClipboard = false;
        }
    }

    private void CopyClipboardEntry(ClipboardEntry entry)
    {
        try
        {
            if (entry.Kind == "text" && entry.Text is not null)
                Clipboard.SetText(entry.Text);
            else if (entry.Kind == "image" && !string.IsNullOrWhiteSpace(entry.ImagePath) && File.Exists(entry.ImagePath))
            {
                using var image = Image.FromFile(entry.ImagePath);
                Clipboard.SetImage(new Bitmap(image));
            }
            if (_pageStatus is not null) _pageStatus.Text = "已复制，可以在任意程序中粘贴";
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, "复制失败：" + ex.Message, "剪贴板", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private void ClearClipboardHistory()
    {
        if (_clipboardEntries.Count == 0) return;
        if (MessageBox.Show(this, "确定清空所有剪贴板历史吗？", "剪贴板", MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes)
            return;
        _clipboardStore.Clear(_clipboardEntries);
        RenderClipboard();
    }

    private void LayoutPhraseHost()
    {
        if (_phraseHost is null || _phraseHeader is null || _phraseTabs is null || _phraseToolbar is null)
            return;
        var list = (Control?)_phraseList ?? _clipboardList;
        if (list is null) return;
        const int headerHeight = 96;
        const int tabsHeight = 52;
        const int toolbarHeight = 58;
        var width = _phraseHost.ClientSize.Width;
        var height = _phraseHost.ClientSize.Height;
        _phraseHeader.SetBounds(0, 0, width, headerHeight);
        _phraseTabs.SetBounds(0, headerHeight, width, tabsHeight);
        _phraseToolbar.SetBounds(0, Math.Max(headerHeight + tabsHeight, height - toolbarHeight), width, toolbarHeight);
        list.SetBounds(0, headerHeight + tabsHeight, width,
            Math.Max(0, height - headerHeight - tabsHeight - toolbarHeight));
    }

    private void ShowSettings()
    {
        SelectNav("settings");
        ClearContent();
        _content.AutoScroll = true;
        _content.AutoScrollMinSize = new Size(0, 1210);
        BuildHeading("输入设置", "控制中日方案的翻译注释和日语模糊匹配。");
        var values = _settings.ReadInputOptions();
        var card = new RoundedPanel { Dock = DockStyle.Top, Height = 1090, BackColor = Theme.Surface, Radius = 12 };
        var en = AddSwitchRow(card, "显示英文注释", "候选词下方显示简短英文释义", "Ctrl + Alt + E", values.English, 0);
        var ja = AddSwitchRow(card, "显示日文注释", "候选词下方显示日语释义", "Ctrl + Alt + J", values.Japanese, 70);
        var spaceSelectFirst = AddSwitchRow(card, "空格键选择首选词", "开启后与常见输入法一致；关闭时上屏原始字母并补一个空格", "", values.SpaceSelectFirst, 140);
        var expandedCommentWidth = AddSwitchRow(card, "展开时注释参与宽度", "按候选词、英文和日文注释中最宽的一行分配格数", "", values.ExpandedCommentWidth, 210);
        var rareThreshold = AddNumberRow(card, "生僻单字过滤门槛", "0=不过滤；数值越高，隐藏的低频单字越多（建议 4000）", _settings.ReadRareSingleCharThreshold(), 0, 50000, 500, 280);
        var fuzzy = AddSwitchRow(card, "日语模糊匹配（总开关）", "只启用下方已勾选的容错规则", "Ctrl + Alt + F", values.Fuzzy, 350);
        var sokuon = AddSwitchRow(card, "　促音省略", "kitte 输入 kite 也能匹配", "", values.FuzzySokuon, 420);
        var longI = AddSwitchRow(card, "　长音 い", "省略表示长音的 i 也能匹配", "", values.FuzzyLongI, 475);
        var longU = AddSwitchRow(card, "　长音 う", "省略表示长音的 u 也能匹配", "", values.FuzzyLongU, 530);
        var longMark = AddSwitchRow(card, "　片假名长音 ー", "例如 kopii 也能匹配 コピー", "", values.FuzzyLongMark, 585);
        var chiJi = AddSwitchRow(card, "　ち / じ", "chi 与 ji 可互相容错匹配", "", values.FuzzyChiJi, 640);
        var huFu = AddSwitchRow(card, "　ふ：hu / fu", "词首或词中 hu 与 fu 可互相容错", "", values.FuzzyHuFu, 695);
        var shuSho = AddSwitchRow(card, "　しゅ / しょ", "shu 与 sho 可互相容错匹配", "", values.FuzzyShuSho, 750);
        var keKai = AddSwitchRow(card, "　け / かい：ke / kai", "例如 seke 也能匹配 世界（sekai）", "", values.FuzzyKeKai, 805);
        var keKaeGae = AddSwitchRow(card, "　ke / kae / gae", "例如 kikeru 也能匹配 着替える（kigaeru）", "", values.FuzzyKeKaeGae, 860);
        var seiSai = AddSwitchRow(card, "　せい / さい：sei / sai", "sei 与 sai 可互相容错匹配", "", values.FuzzySeiSai, 915);
        var dakuten = AddSwitchRow(card, "　浊音 / 半浊音", "t/d、p/b/h 容错，可与促音组合", "", values.FuzzyDakuten, 970);
        var apply = PrimaryButton("应用设置", 24, 1040, 118);
        async Task SaveAndApplyAsync(bool deploy)
        {
            // Persist before the first await so navigating to another page can
            // never recreate these switches from stale values.
            _settings.SaveInputOptions(new InputOptions(
                en.Checked, ja.Checked, spaceSelectFirst.Checked, expandedCommentWidth.Checked, fuzzy.Checked,
                sokuon.Checked, longI.Checked, longU.Checked,
                longMark.Checked, chiJi.Checked, huFu.Checked,
                shuSho.Checked, keKai.Checked, keKaeGae.Checked,
                seiSai.Checked, dakuten.Checked, values.Sentence));
            if (deploy)
            {
                _settings.SaveRareSingleCharThreshold((int)rareThreshold.Value);
                await RimeRuntime.DeployAsync();
            }
            else
                await RimeRuntime.RestartServerAsync();
        }
        async void SaveImmediately(object? sender, EventArgs e)
        {
            try
            {
                apply.Text = "正在生效…";
                apply.Enabled = false;
                await SaveAndApplyAsync(false);
            }
            finally
            {
                if (!apply.IsDisposed)
                {
                    apply.Text = "已生效";
                    apply.Enabled = true;
                }
            }
        }
        en.CheckedChanged += SaveImmediately;
        ja.CheckedChanged += SaveImmediately;
        spaceSelectFirst.CheckedChanged += async (_, _) =>
        {
            try
            {
                apply.Text = "正在部署…";
                apply.Enabled = false;
                await SaveAndApplyAsync(true);
            }
            finally
            {
                if (!apply.IsDisposed)
                {
                    apply.Text = "已生效";
                    apply.Enabled = true;
                }
            }
        };
        expandedCommentWidth.CheckedChanged += async (_, _) =>
        {
            try
            {
                apply.Text = "正在部署…";
                apply.Enabled = false;
                await SaveAndApplyAsync(true);
            }
            finally
            {
                if (!apply.IsDisposed)
                {
                    apply.Text = "已生效";
                    apply.Enabled = true;
                }
            }
        };
        fuzzy.CheckedChanged += SaveImmediately;
        sokuon.CheckedChanged += SaveImmediately;
        longI.CheckedChanged += SaveImmediately;
        longU.CheckedChanged += SaveImmediately;
        longMark.CheckedChanged += SaveImmediately;
        chiJi.CheckedChanged += SaveImmediately;
        huFu.CheckedChanged += SaveImmediately;
        shuSho.CheckedChanged += SaveImmediately;
        keKai.CheckedChanged += SaveImmediately;
        keKaeGae.CheckedChanged += SaveImmediately;
        seiSai.CheckedChanged += SaveImmediately;
        dakuten.CheckedChanged += SaveImmediately;
        apply.Click += async (_, _) =>
        {
            await RunBusyAsync(apply, "正在应用…", async () =>
            {
                await SaveAndApplyAsync(true);
            });
        };
        card.Controls.Add(apply);
        _content.Controls.Add(card);
        card.BringToFront();
    }

    private void ShowChineseCorrections()
    {
        SelectNav("chinese-fuzzy");
        ClearContent();
        var rules = _chineseCorrections.Load();

        var host = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            BackColor = Theme.Window,
            Padding = new Padding(0, 0, 8, 0),
            ColumnCount = 1,
            RowCount = 2,
            Margin = Padding.Empty
        };
        host.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        host.RowStyles.Add(new RowStyle(SizeType.Absolute, 118));
        host.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        var toolbar = new RoundedPanel { Dock = DockStyle.Fill, BackColor = Theme.Surface, Radius = 12, Margin = Padding.Empty };
        var master = AddSwitchRow(toolbar, "全部中文自动纠错", "统一开启或关闭下方全部规则", "", rules.All(x => x.Enabled), 0);
        var apply = PrimaryButton("应用并部署", 24, 72, 132);
        toolbar.Controls.Add(apply);

        var list = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            AutoScroll = true,
            FlowDirection = FlowDirection.TopDown,
            WrapContents = false,
            BackColor = Theme.Window,
            Padding = new Padding(0, 12, 0, 20)
        };
        var toggles = new List<ToggleSwitch>();
        foreach (var rule in rules)
        {
            var row = new RoundedPanel { Width = 720, Height = 70, BackColor = Theme.Surface, Radius = 8, Margin = new Padding(0, 0, 0, 8) };
            toggles.Add(AddSwitchRow(row, rule.Title, rule.Description, "", rule.Enabled, 0));
            list.Controls.Add(row);
        }
        void FitCorrectionRows()
        {
            var width = Math.Max(520, list.ClientSize.Width -
                list.Padding.Horizontal - SystemInformation.VerticalScrollBarWidth - 8);
            foreach (Control row in list.Controls) row.Width = width;
        }
        list.Resize += (_, _) => FitCorrectionRows();
        var changingAll = false;
        master.CheckedChanged += (_, _) =>
        {
            if (changingAll) return;
            changingAll = true;
            foreach (var toggle in toggles) toggle.Checked = master.Checked;
            changingAll = false;
        };
        apply.Click += async (_, _) => await RunBusyAsync(apply, "正在重新部署…", async () =>
        {
            var updated = rules.Select((rule, i) => rule with { Enabled = toggles[i].Checked }).ToList();
            _chineseCorrections.Save(updated);
            await RimeRuntime.DeployAsync();
        });
        host.Controls.Add(toolbar, 0, 0);
        host.Controls.Add(list, 0, 1);
        var page = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            BackColor = Theme.Window,
            ColumnCount = 1,
            RowCount = 2,
            Margin = Padding.Empty,
            Padding = Padding.Empty
        };
        page.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        page.RowStyles.Add(new RowStyle(SizeType.Absolute, 96));
        page.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        var heading = CreateHeading(
            "中文模糊纠错", $"雾凇自动纠错共 {rules.Count} 条；修改后需要重新部署。");
        heading.Dock = DockStyle.Fill;
        page.Controls.Add(heading, 0, 0);
        page.Controls.Add(host, 0, 1);
        _content.Controls.Add(page);
        BeginInvoke(FitCorrectionRows);
    }

    private void ShowAppearance()
    {
        SelectNav("appearance");
        ClearContent();
        _content.AutoScroll = true;
        _content.AutoScrollMinSize = new Size(0, 760);
        BuildHeading("外观", "调整候选框尺寸，并分别设置中文、日语候选的文字色与底色。");
        var values = _settings.ReadAppearance();
        var card = new RoundedPanel { Dock = DockStyle.Top, Height = 630, BackColor = Theme.Surface, Radius = 12 };
        var width = AddNumberRow(card, "候选框宽度", "普通单行与展开状态使用同一基准宽度", values.Width, 520, 1200, 20, 0);
        var font = AddNumberRow(card, "注释字号", "英文和日语注释使用较小字号，避免重叠", values.CommentSize, 8, 18, 1, 70);
        var spacing = AddNumberRow(card, "候选间距", "词与词之间的横向空隙", values.CandidateSpacing, 4, 36, 1, 140);
        var padding = AddNumberRow(card, "选中项留白", "高亮背景与文字之间的内边距", values.HighlightPadding, 2, 18, 1, 210);
        var chineseText = AddColorRow(card, "中文文字颜色", "可自定义，或选择“保持原版”跟随当前主题", values.ChineseText, 280);
        var chineseBack = AddColorRow(card, "中文底色", "可自定义，或选择“保持原版”不添加额外底色", values.ChineseBackground, 350);
        var japaneseText = AddColorRow(card, "日语文字颜色", "包含仅由汉字组成的日语词；也可跟随原版主题", values.JapaneseText, 420);
        var japaneseBack = AddColorRow(card, "日语底色", "可自定义，或选择“保持原版”不添加额外底色", values.JapaneseBackground, 490);
        var apply = PrimaryButton("应用外观", 24, 570, 118);
        apply.Click += async (_, _) =>
        {
            await RunBusyAsync(apply, "正在部署…", async () =>
            {
                _settings.SaveAppearance(new AppearanceOptions(
                    (int)width.Value, (int)font.Value, (int)spacing.Value, (int)padding.Value,
                    (Color)chineseText.Tag!, (Color)chineseBack.Tag!,
                    (Color)japaneseText.Tag!, (Color)japaneseBack.Tag!));
                await RimeRuntime.DeployAsync();
            });
        };
        card.Controls.Add(apply);
        _content.Controls.Add(card);
        card.BringToFront();
    }

    private void ShowSentenceTranslation()
    {
        SelectNav("sentence-translation");
        ClearContent();
        BuildHeading("实时句子翻译（开发中）", "离线翻译服务仍在开发，默认关闭以避免输入卡顿。");
        var values = _settings.ReadInputOptions();
        var card = new RoundedPanel
        {
            Dock = DockStyle.Top,
            Height = 170,
            BackColor = Theme.Surface,
            Radius = 12
        };
        var sentence = AddSwitchRow(card, "启用实时句子翻译", "逐词更新整句的英文与日文翻译", "Ctrl + Alt + T", values.Sentence, 0);
        var status = new Label
        {
            Text = values.Sentence ? "当前已开启" : "当前已关闭（推荐）",
            Left = 24,
            Top = 92,
            Width = 420,
            Height = 30,
            Font = Theme.Font(9.5f),
            ForeColor = Theme.Muted
        };
        sentence.CheckedChanged += async (_, _) =>
        {
            sentence.Enabled = false;
            try
            {
                var current = _settings.ReadInputOptions();
                _settings.SaveInputOptions(current with { Sentence = sentence.Checked });
                await RimeRuntime.RestartServerAsync();
                status.Text = sentence.Checked ? "当前已开启" : "当前已关闭（推荐）";
            }
            finally
            {
                sentence.Enabled = true;
            }
        };
        card.Controls.Add(status);
        _content.Controls.Add(card);
        card.BringToFront();
    }

    private void ShowHotkeys()
    {
        SelectNav("hotkeys");
        ClearContent();
        BuildHeading("快捷键", "当前快捷键集中展示；开关状态也可以直接在“输入”页面点击。");
        var list = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            FlowDirection = FlowDirection.TopDown,
            WrapContents = false,
            AutoScroll = true,
            BackColor = Theme.Window
        };
        foreach (var item in new[]
        {
            ("方案切换", "F4"),
            ("展开 / 下一行", "Page Down / ] / ="),
            ("上一行", "Page Up / [ / -"),
            ("英文注释", "Ctrl + Alt + E"),
            ("日文注释", "Ctrl + Alt + J"),
            ("日语模糊匹配", "Ctrl + Alt + F"),
            ("实时句子翻译", "Ctrl + Alt + T")
        })
            list.Controls.Add(BuildHotkeyCard(item.Item1, item.Item2));
        _content.Controls.Add(list);
    }

    private void ShowMaintenance()
    {
        SelectNav("maintenance");
        ClearContent();
        BuildHeading("维护", "打开配置目录、重新部署或重新启动输入法服务。");
        var card = new RoundedPanel { Dock = DockStyle.Top, Height = 218, BackColor = Theme.Surface, Radius = 12 };
        var open = SecondaryButton("打开用户文件夹", 24, 28, 150);
        var deploy = PrimaryButton("重新部署", 188, 28, 118);
        var restart = SecondaryButton("重启算法服务", 320, 28, 142);
        var path = new Label
        {
            Text = _phraseStore.RimeDirectory,
            Left = 24,
            Top = 92,
            Width = 680,
            Height = 28,
            ForeColor = Theme.Muted,
            Font = Theme.Font(9)
        };
        var note = new Label
        {
            Text = "当前仅维护 V9；本面板操作的是正在使用的“雾凇拼音·中日”配置。",
            Left = 24,
            Top = 142,
            Width = 680,
            Height = 28,
            ForeColor = Theme.Muted,
            Font = Theme.Font(9.5f)
        };
        open.Click += (_, _) => RimeRuntime.OpenRimeDirectory(_phraseStore.RimeDirectory);
        deploy.Click += async (_, _) => await RunBusyAsync(deploy, "部署中…", RimeRuntime.DeployAsync);
        restart.Click += async (_, _) => await RunBusyAsync(restart, "重启中…", RimeRuntime.RestartServerAsync);
        card.Controls.AddRange([open, deploy, restart, path, note]);
        _content.Controls.Add(card);
        card.BringToFront();
    }

    private static ToggleSwitch AddSwitchRow(Control parent, string title, string description, string shortcut, bool value, int top)
    {
        var toggle = new ToggleSwitch { Top = top + 22, Checked = value, Anchor = AnchorStyles.Top };
        var shortcutLabel = new Label
        {
            Text = shortcut,
            Top = top + 27,
            Width = 150,
            Height = 24,
            TextAlign = ContentAlignment.MiddleRight,
            Font = Theme.Font(8.5f),
            ForeColor = Theme.Muted,
            Anchor = AnchorStyles.Top
        };
        parent.Controls.Add(new Label { Text = title, Left = 24, Top = top + 14, Width = 310, Height = 26, Font = Theme.Font(10.5f, FontStyle.Bold), ForeColor = Theme.Text });
        parent.Controls.Add(new Label { Text = description, Left = 24, Top = top + 40, Width = 430, Height = 23, Font = Theme.Font(9), ForeColor = Theme.Muted });
        parent.Controls.Add(shortcutLabel);
        if (top > 0) parent.Controls.Add(new Divider { Left = 24, Top = top, Width = parent.Width - 48, Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Top });
        parent.Controls.Add(toggle);

        void PositionRightControls()
        {
            toggle.Left = Math.Max(488, parent.ClientSize.Width - 76);
            shortcutLabel.Left = Math.Max(326, toggle.Left - 164);
        }

        parent.SizeChanged += (_, _) => PositionRightControls();
        PositionRightControls();
        return toggle;
    }

    private static NumericUpDown AddNumberRow(Control parent, string title, string description, int value, int min, int max, int increment, int top)
    {
        var number = new NumericUpDown
        {
            Left = parent.Width - 144,
            Top = top + 21,
            Width = 112,
            Height = 32,
            Minimum = min,
            Maximum = max,
            Increment = increment,
            Value = Math.Clamp(value, min, max),
            BackColor = Theme.Input,
            ForeColor = Theme.Text,
            BorderStyle = BorderStyle.FixedSingle,
            Font = Theme.Font(10),
            Anchor = AnchorStyles.Top | AnchorStyles.Right
        };
        parent.Controls.Add(new Label { Text = title, Left = 24, Top = top + 14, Width = 310, Height = 26, Font = Theme.Font(10.5f, FontStyle.Bold), ForeColor = Theme.Text });
        parent.Controls.Add(new Label { Text = description, Left = 24, Top = top + 40, Width = 520, Height = 23, Font = Theme.Font(9), ForeColor = Theme.Muted });
        if (top > 0) parent.Controls.Add(new Divider { Left = 24, Top = top, Width = parent.Width - 48, Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Top });
        parent.Controls.Add(number);
        return number;
    }

    private static FlatButton AddColorRow(Control parent, string title, string description, Color value, int top)
    {
        var swatch = new FlatButton
        {
            Text = value.A == 0 ? "选择颜色" : $"#{value.R:X2}{value.G:X2}{value.B:X2}",
            Left = parent.Width - 144,
            Top = top + 17,
            Width = 112,
            Height = 38,
            BackColor = value.A == 0 ? Theme.Input : value,
            ForeColor = value.A == 0 || value.GetBrightness() < 0.5f ? Color.White : Color.Black,
            Tag = value,
            Anchor = AnchorStyles.Top | AnchorStyles.Right
        };
        var original = new FlatButton
        {
            Text = "保持原版",
            Left = parent.Width - 256,
            Top = top + 17,
            Width = 104,
            Height = 38,
            BackColor = value.A == 0 ? Theme.Accent : Theme.Input,
            ForeColor = Color.White,
            Anchor = AnchorStyles.Top | AnchorStyles.Right
        };
        swatch.Click += (_, _) =>
        {
            using var dialog = new ColorDialog
            {
                Color = ((Color)swatch.Tag!).A == 0 ? Color.White : (Color)swatch.Tag!,
                FullOpen = true
            };
            if (dialog.ShowDialog() != DialogResult.OK) return;
            var chosen = Color.FromArgb(255, dialog.Color);
            swatch.Tag = chosen;
            swatch.Text = $"#{chosen.R:X2}{chosen.G:X2}{chosen.B:X2}";
            swatch.BackColor = chosen;
            swatch.ForeColor = chosen.GetBrightness() < 0.5f ? Color.White : Color.Black;
            original.BackColor = Theme.Input;
        };
        original.Click += (_, _) =>
        {
            var transparent = Color.FromArgb(0, 0, 0, 0);
            swatch.Tag = transparent;
            swatch.Text = "选择颜色";
            swatch.BackColor = Theme.Input;
            swatch.ForeColor = Color.White;
            original.BackColor = Theme.Accent;
        };
        parent.Controls.Add(new Label { Text = title, Left = 24, Top = top + 14, Width = 310, Height = 26, Font = Theme.Font(10.5f, FontStyle.Bold), ForeColor = Theme.Text });
        parent.Controls.Add(new Label { Text = description, Left = 24, Top = top + 40, Width = 520, Height = 23, Font = Theme.Font(9), ForeColor = Theme.Muted });
        parent.Controls.Add(new Divider { Left = 24, Top = top, Width = parent.Width - 48, Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Top });
        parent.Controls.Add(original);
        parent.Controls.Add(swatch);
        return swatch;
    }

    private static Control BuildHotkeyCard(string action, string shortcut)
    {
        var card = new RoundedPanel { Width = 740, Height = 66, BackColor = Theme.Surface, Radius = 10, Margin = new Padding(0, 0, 0, 9) };
        card.Controls.Add(new Label { Text = action, Left = 20, Top = 20, Width = 320, Height = 28, Font = Theme.Font(10.5f), ForeColor = Theme.Text });
        var key = new Label { Text = shortcut, Left = 430, Top = 14, Width = 280, Height = 38, TextAlign = ContentAlignment.MiddleCenter, Font = Theme.Font(9.5f, FontStyle.Bold), ForeColor = Theme.Text, BackColor = Theme.Input };
        card.Controls.Add(key);
        return card;
    }

    private static Panel BuildBottomToolbar() => new() { Height = 58, BackColor = Theme.Window };

    private static FlowLayoutPanel BuildList() => new()
    {
        FlowDirection = FlowDirection.TopDown,
        WrapContents = false,
        AutoScroll = true,
        BackColor = Theme.Window,
        Padding = new Padding(0, 4, 8, 12)
    };

    private static Label BuildStatusLabel(string text) => new()
    {
        Text = text,
        Left = 0,
        Top = 20,
        AutoSize = true,
        ForeColor = Theme.Muted,
        Font = Theme.Font(9)
    };

    private static Label BuildEmptyState(string title, string description, int width) => new()
    {
        Text = title + "\n\n" + description,
        Width = width,
        Height = 180,
        TextAlign = ContentAlignment.MiddleCenter,
        ForeColor = Theme.Muted,
        Font = Theme.Font(10.5f)
    };

    private static FlatButton ActionButton(string text, int left, Color back, Color fore, int width = 42) => new()
    {
        Text = text,
        Width = width,
        Height = 34,
        Left = left,
        Top = 29,
        BackColor = back,
        ForeColor = fore,
        Font = Theme.Font(text == "×" ? 14 : 9.5f, text == "×" ? FontStyle.Bold : FontStyle.Regular)
    };

    private static FlatButton PrimaryButton(string text, int left, int top, int width) => new()
    {
        Text = text,
        Left = left,
        Top = top,
        Width = width,
        Height = 38,
        BackColor = Theme.Accent,
        ForeColor = Color.White
    };

    private static FlatButton SecondaryButton(string text, int left, int top, int width) => new()
    {
        Text = text,
        Left = left,
        Top = top,
        Width = width,
        Height = 38,
        BackColor = Theme.Input,
        ForeColor = Theme.Text
    };

    private async Task RunBusyAsync(FlatButton button, string busyText, Func<Task> action)
    {
        var original = button.Text;
        button.Enabled = false;
        button.Text = busyText;
        try
        {
            await action();
            button.Text = "已完成";
            await Task.Delay(700);
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "中日方案设置", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
        finally
        {
            button.Text = original;
            button.Enabled = true;
        }
    }
}
