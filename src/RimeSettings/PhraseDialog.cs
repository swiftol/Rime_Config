using System;
using System.Drawing;
using System.Windows.Forms;

namespace RimeSettings;

internal sealed class PhraseDialog : Form
{
    private readonly string _rimeDirectory;
    private readonly TextBox _content;
    private readonly TextBox _code;
    public CommonPhrase? Result { get; private set; }

    public PhraseDialog(string rimeDirectory, CommonPhrase? phrase = null)
    {
        _rimeDirectory = rimeDirectory;
        Text = phrase is null ? "添加常用语" : "编辑常用语";
        ClientSize = new Size(540, 350);
        BackColor = Theme.Window;
        ForeColor = Theme.Text;
        Font = Theme.Font(10);
        StartPosition = FormStartPosition.CenterParent;
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        MinimizeBox = false;
        ShowInTaskbar = false;

        var title = new Label { Text = Text, Font = Theme.Font(17, FontStyle.Bold), AutoSize = true, Left = 28, Top = 22 };
        var contentLabel = LabelFor("内容", 31, 74);
        _content = InputFor(28, 98, 484, 112, true);
        var codeLabel = LabelFor("输入码", 31, 226);
        _code = InputFor(28, 250, 484, 42, false);
        _code.PlaceholderText = "可留空：中文取前三字拼音首字母；英文/数字取前三位";
        _content.Text = phrase?.Content ?? "";
        _code.Text = phrase?.Code ?? "";

        var cancel = new FlatButton { Text = "取消", BackColor = Theme.Input, Left = 322, Top = 307, Width = 88, Height = 34 };
        var done = new FlatButton { Text = "完成", BackColor = Theme.Accent, Left = 424, Top = 307, Width = 88, Height = 34 };
        cancel.Click += (_, _) => { DialogResult = DialogResult.Cancel; Close(); };
        done.Click += (_, _) => Commit();
        AcceptButton = done;
        CancelButton = cancel;
        Controls.AddRange([title, contentLabel, _content, codeLabel, _code, cancel, done]);
    }

    private void Commit()
    {
        var content = _content.Text.Replace("\r", " ").Replace("\n", " ").Trim();
        var code = _code.Text.Trim().ToLowerInvariant();
        if (content.Length == 0)
        {
            MessageBox.Show(this, "常用语内容不能为空。", "常用语", MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }
        if (code.Length == 0) code = PhraseCodeGenerator.Generate(content, _rimeDirectory);
        if (code.Length == 0)
        {
            MessageBox.Show(this, "无法自动生成输入码，请手动填写。", "常用语", MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }
        foreach (var c in code)
        {
            if (!char.IsLetterOrDigit(c) && c is not '-' and not '_')
            {
                MessageBox.Show(this, "输入码只能使用字母、数字、短横线或下划线。", "常用语", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }
        }
        Result = new CommonPhrase(content, code);
        DialogResult = DialogResult.OK;
        Close();
    }

    private static Label LabelFor(string text, int left, int top) => new()
    {
        Text = text, Left = left, Top = top, AutoSize = true, ForeColor = Theme.Muted, Font = Theme.Font(9)
    };

    private static TextBox InputFor(int left, int top, int width, int height, bool multiline) => new()
    {
        Left = left, Top = top, Width = width, Height = height, Multiline = multiline,
        BorderStyle = BorderStyle.FixedSingle, BackColor = Theme.Input, ForeColor = Theme.Text,
        Font = Theme.Font(11), ScrollBars = multiline ? ScrollBars.Vertical : ScrollBars.None
    };
}
