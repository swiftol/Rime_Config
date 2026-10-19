using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Dispatching;

namespace ZhongriSettingsV2;

public sealed partial class MainWindow
{
    private readonly List<AutoSaveBinding> _autoSaves = [];
    private Task _automaticDeployment = Task.CompletedTask;
    private Task _keyEditorWrites = Task.CompletedTask;
    private Func<Task> _deploySettings = RimeSettings.RimeRuntime.DeployAsync;

    // The write runs on the UI thread: values are captured before navigation,
    // writes cannot overlap, and closing never drops the last pending change.
    private Action AutoSave(Action write, int delay = 350, bool deploy = false, Func<string>? values = null)
    {
        var status = _saveStatus;
        var timer = DispatcherQueue.CreateTimer();
        timer.Interval = TimeSpan.FromMilliseconds(delay);
        timer.IsRepeating = false;
        var pending = false;
        var observed=values?.Invoke();
        AutoSaveBinding? binding=null;
        void Status(string text, bool error = false)
        {
            if (status is null) return;
            status.Text = text;
            status.Foreground = error ? Brush(239, 139, 139) : Brush(143, 205, 177);
        }
        void Flush()
        {
            timer.Stop();
            if (binding is null || !_autoSaves.Contains(binding)) return;
            var latest=values?.Invoke();
            if (values is not null && latest==observed) { pending=false; return; }
            if (!pending && latest==observed) return;
            pending = false;
            observed=latest;
            try
            {
                if (_uiTestMode) { Status("测试模式：修改已响应，未写入配置"); return; }
                write();
                Status(deploy ? "已保存，正在自动部署…" : "已自动保存并生效");
                if (deploy)
                    _automaticDeployment = DeployAfterAsync(_automaticDeployment, Status);
            }
            catch (Exception error) { Status("未保存：" + error.Message, true); }
        }
        timer.Tick += (_, _) => Flush();
        binding=new AutoSaveBinding(Flush); _autoSaves.Add(binding);
        return () => {
            if (!_autoSaves.Contains(binding) || values is not null && values()==observed) return;
            pending = true; timer.Stop(); timer.Start(); Status("正在自动保存…");
        };
    }

    private async Task DeployAfterAsync(Task previous, Action<string, bool> status)
    {
        await previous;
        try { await _deploySettings(); DispatcherQueue.TryEnqueue(()=>status("已自动保存并部署生效", false)); }
        catch (Exception error) { DispatcherQueue.TryEnqueue(()=>status("已保存，但部署失败：" + error.Message, true)); }
    }

    private void FlushAutoSaves()
    {
        FlushPendingAutoSaves();
        _autoSaves.Clear();
    }

    private void FlushPendingAutoSaves()
    {
        foreach (var binding in _autoSaves.ToArray()) binding.Flush();
    }

    private void AutoSaveStatus(string text = "修改后自动保存并生效")
    {
        _saveStatus = new TextBlock { Text = text, Foreground = Brush(143, 205, 177),
            TextWrapping = Microsoft.UI.Xaml.TextWrapping.Wrap };
        PageContent.Children.Add(_saveStatus);
    }

    private static int ValidNumber(TextBox box, int minimum, int maximum)
    {
        if (!int.TryParse(box.Text, out var value) || value < minimum || value > maximum)
            throw new ArgumentException($"请输入 {minimum}–{maximum} 范围内的整数；已保留原设置。");
        return value;
    }

    private sealed record AutoSaveBinding(Action Flush);

    private void AutoSaveDialog(ContentDialog dialog, Microsoft.UI.Xaml.FrameworkElement body,
        Action write, TextBlock error, Func<Action>? prepareBackground = null, Action? backgroundSaved = null)
    {
        var timer = DispatcherQueue.CreateTimer();
        timer.Interval = TimeSpan.FromMilliseconds(600); timer.IsRepeating = false;
        var pending = false;
        var readers=new List<Func<string>>();
        string Signature()=>string.Join("\u001f",readers.Select(read=>read()));
        var observed="";
        var revision=0;
        async Task WriteInBackgroundAsync(Task previous, Action commit, int version)
        {
            await previous;
            try {
                await Task.Run(commit);
                backgroundSaved?.Invoke();
                if(version==revision)error.Text=_uiTestMode?"测试模式：未写入配置":"已自动保存并生效";
            } catch(Exception failure) { if(version==revision)error.Text="未保存："+failure.Message; }
        }
        AutoSaveBinding? binding=null;
        void Changed() { if(binding is null || !_autoSaves.Contains(binding) || Signature()==observed) return; revision++;pending = true; timer.Stop(); timer.Start(); }
        void Flush()
        {
            timer.Stop();
            var latest=Signature();
            if ((!pending && latest==observed) || readers.Count==0) return;
            pending = false; observed=latest;
            try {
                if(prepareBackground is not null) {
                    // Snapshot controls on UI; serialize only immutable captured
                    // values on a worker. Never let an older write finish last.
                    var commit=prepareBackground();
                    error.Text="正在自动保存…";
                    _keyEditorWrites=WriteInBackgroundAsync(_keyEditorWrites,commit,++revision);
                } else { write(); error.Text = _uiTestMode ? "测试模式：未写入配置" : "已自动保存并生效"; }
            }
            catch (Exception failure) { error.Text = "未保存：" + failure.Message; }
        }
        void Attach(Microsoft.UI.Xaml.DependencyObject parent)
        {
            if (parent is TextBox text) { readers.Add(()=>text.Text); text.TextChanged += (_, _) => Changed(); return; }
            if (parent is ComboBox combo) { readers.Add(()=>combo.SelectedIndex.ToString()); combo.SelectionChanged += (_, _) => Changed(); return; }
            if (parent is ToggleSwitch toggle) { readers.Add(()=>toggle.IsOn.ToString()); toggle.Toggled += (_, _) => Changed(); return; }
            if (parent is CheckBox check) { readers.Add(()=>check.IsChecked.ToString() ?? ""); check.Checked += (_, _) => Changed(); check.Unchecked += (_, _) => Changed(); check.Indeterminate += (_, _) => Changed(); return; }
            for (var i = 0; i < Microsoft.UI.Xaml.Media.VisualTreeHelper.GetChildrenCount(parent); i++)
                Attach(Microsoft.UI.Xaml.Media.VisualTreeHelper.GetChild(parent, i));
        }
        binding = new AutoSaveBinding(Flush); _autoSaves.Add(binding);
        timer.Tick += (_, _) => Flush();
        dialog.Opened += (_, _) => { Attach(body); observed=Signature(); };
        if(prepareBackground is not null) dialog.Closing += async (_, args) => {
            var deferral=args.GetDeferral();
            dialog.IsEnabled=false;
            try { Flush(); await _keyEditorWrites; } finally { dialog.IsEnabled=true;deferral.Complete(); }
        };
        dialog.Closed += (_, _) => { Flush(); _autoSaves.Remove(binding); };
    }
}
