using System;
using System.Drawing;
using System.Windows.Forms;

namespace RimeSettings;

internal sealed class FuzzyRuleDialog : Form
{
    private readonly TextBox _left = new();
    private readonly TextBox _right = new();
    public CustomFuzzyRule? Result { get; private set; }

    public FuzzyRuleDialog(string language, CustomFuzzyRule? existing = null)
    {
        Text = $"{language}自定义模糊匹配";
        ClientSize = new Size(470, 255);
        MinimumSize = MaximumSize = Size;
        StartPosition = FormStartPosition.CenterParent;
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = MinimizeBox = false;
        BackColor = Theme.Window;
        ForeColor = Theme.Text;
        Font = Theme.Font(10);

        Controls.Add(MakeLabel("输入 A", 30, 28));
        Controls.Add(MakeLabel("匹配 B（默认双向）", 250, 28));
        Configure(_left, 30, existing?.Left ?? "");
        Configure(_right, 250, existing?.Right ?? "");
        Controls.Add(_left);
        Controls.Add(_right);
        Controls.Add(new Label { Left = 30, Top = 100, Width = 390, Height = 48,
            ForeColor = Theme.Muted, Text = "仅支持 1～16 个小写罗马字母。例如：sho ↔ shu。\n日语规则不会影响中文候选。" });
        var save = new FlatButton { Text = "保存", Left = 250, Top = 175, Width = 88, Height = 38,
            BackColor = Theme.Accent, ForeColor = Color.White };
        var cancel = new FlatButton { Text = "取消", Left = 348, Top = 175, Width = 88, Height = 38,
            BackColor = Theme.Input, ForeColor = Theme.Text };
        save.Click += (_, _) => Save();
        cancel.Click += (_, _) => { DialogResult = DialogResult.Cancel; Close(); };
        Controls.Add(save);
        Controls.Add(cancel);
        AcceptButton = save;
        CancelButton = cancel;
    }

    private static Label MakeLabel(string text, int left, int top) => new()
        { Text = text, Left = left, Top = top, Width = 190, Height = 24, ForeColor = Theme.Text };

    private static void Configure(TextBox box, int left, string text)
    {
        box.Left = left; box.Top = 57; box.Width = 190; box.Height = 34;
        box.Text = text; box.BackColor = Theme.Input; box.ForeColor = Theme.Text;
        box.BorderStyle = BorderStyle.FixedSingle; box.CharacterCasing = CharacterCasing.Lower;
    }

    private void Save()
    {
        var left = CustomFuzzyRuleStore.Normalize(_left.Text);
        var right = CustomFuzzyRuleStore.Normalize(_right.Text);
        if (!CustomFuzzyRuleStore.IsValid(left, right))
        {
            MessageBox.Show(this, "两边必须是不同的 1～16 位英文字母。", "规则无效",
                MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }
        Result = new(true, left, right);
        DialogResult = DialogResult.OK;
        Close();
    }
}
