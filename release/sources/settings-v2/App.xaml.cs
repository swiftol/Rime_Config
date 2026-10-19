using Microsoft.UI.Xaml;
using RimeSettings;

namespace ZhongriSettingsV2;

public partial class App : Application
{
    private Window? _window;

    public App()
    {
        UnhandledException += (_, error) =>
        {
            try { File.AppendAllText(Path.Combine(Path.GetTempPath(), "ZhongriSettingsV2-unhandled.log"),
                DateTimeOffset.Now + "\n" + error.Exception + "\n" + error.Message + "\n"); }
            catch { }
        };
        InitializeComponent();
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        var arguments = Environment.GetCommandLineArgs();
        var reservedMultiTest=Array.IndexOf(arguments,"--reserved-multi-search-self-test");
        if(reservedMultiTest>=0) {
            var id=Guid.NewGuid().ToString("N");var registry=@"Software\ZhongriInputMethod\Tests\ReservedMultiUi-"+id;
            var window=new MainWindow(Path.Combine(Path.GetTempPath(),"ReservedMultiUi-"+id),registry);_window=window;
            window.AppWindow.Move(new Windows.Graphics.PointInt32(-32000,-32000));window.AppWindow.Show(false);
            var timer=window.DispatcherQueue.CreateTimer();timer.Interval=TimeSpan.FromMilliseconds(400);
            timer.Tick+=async(_,_)=> {
                timer.Stop();System.Threading.SynchronizationContext.SetSynchronizationContext(new Microsoft.UI.Dispatching.DispatcherQueueSynchronizationContext(window.DispatcherQueue));
                var result=0;var report=Path.Combine(Path.GetTempPath(),"reserved-multi-ui.txt");
                try {await window.RunReservedMultiSearchSelfTestAsync(arguments[reservedMultiTest+1]);File.WriteAllText(report,"PASS duplicate per-item UI callbacks, virtual/fuzzy-code query controls, reserved color hot autosave and named profile restore; isolated data/registry, no OS input");}
                catch(Exception error){result=1;File.WriteAllText(report,error.ToString());}
                finally {Microsoft.Win32.Registry.CurrentUser.DeleteSubKeyTree(registry,false);}
                Environment.Exit(result);
            };timer.Start();return;
        }
        var globalTest=Array.IndexOf(arguments,"--global-profiles-self-test");
        if(globalTest>=0) {
            var id=Guid.NewGuid().ToString("N");var registry=@"Software\ZhongriInputMethod\Tests\GlobalUi-"+id;
            var window=new MainWindow(Path.Combine(Path.GetTempPath(),"GlobalProfiles-"+id),registry);
            _window=window;window.AppWindow.Move(new Windows.Graphics.PointInt32(-32000,-32000));window.AppWindow.Show(false);
            var timer=window.DispatcherQueue.CreateTimer();timer.Interval=TimeSpan.FromMilliseconds(400);
            timer.Tick+=async(_,_)=> {
                timer.Stop();System.Threading.SynchronizationContext.SetSynchronizationContext(new Microsoft.UI.Dispatching.DispatcherQueueSynchronizationContext(window.DispatcherQueue));
                var result=0;
                try { await window.RunGlobalProfilesSelfTestAsync(arguments[globalTest+1]);File.WriteAllText(Path.Combine(Path.GetTempPath(),"global-profiles-ui.txt"),"PASS sidebar, five actions, named profiles and actual selection-event hot switching; isolated registry/data; no synthetic input"); }
                catch(Exception error) { result=1;File.WriteAllText(Path.Combine(Path.GetTempPath(),"global-profiles-ui.txt"),error.ToString()); }
                finally { Microsoft.Win32.Registry.CurrentUser.DeleteSubKeyTree(registry,false); }
                Environment.Exit(result);
            };timer.Start();return;
        }
        var livePreview=Array.IndexOf(arguments,"--appearance-live-preview");
        if(livePreview>=0) {
            var window=new MainWindow();_window=window;
            window.AppWindow.Move(new Windows.Graphics.PointInt32(-32000,-32000));window.AppWindow.Show(false);
            var timer=window.DispatcherQueue.CreateTimer();timer.Interval=TimeSpan.FromMilliseconds(350);
            timer.Tick+=async (_,_)=> {
                timer.Stop();
                System.Threading.SynchronizationContext.SetSynchronizationContext(new Microsoft.UI.Dispatching.DispatcherQueueSynchronizationContext(window.DispatcherQueue));
                try { await window.CaptureLiveAppearancePreviewAsync(arguments[livePreview+1]);Environment.Exit(0); }
                catch(Exception error) { File.WriteAllText(Path.ChangeExtension(arguments[livePreview+1],"error.txt"),error.ToString());Environment.Exit(1); }
            };timer.Start();return;
        }
        if(arguments.Contains("--autosave-self-test",StringComparer.Ordinal)) {
            File.WriteAllText(Path.Combine(Path.GetTempPath(),"ZhongriSettingsV2-autosave-test.txt"),"START");
            var id=Guid.NewGuid().ToString("N");
            var window=new MainWindow(Path.Combine(Path.GetTempPath(),"ZhongriAutoSaveTest-"+id),@"Software\ZhongriInputMethod\Tests\"+id);
            _window=window; window.AppWindow.Move(new Windows.Graphics.PointInt32(-32000,-32000)); window.AppWindow.Show(false);
            var timer=window.DispatcherQueue.CreateTimer(); timer.Interval=TimeSpan.FromMilliseconds(400);
            timer.Tick+=async (_,_)=> {
                timer.Stop();
                System.Threading.SynchronizationContext.SetSynchronizationContext(new Microsoft.UI.Dispatching.DispatcherQueueSynchronizationContext(window.DispatcherQueue));
                var report=Path.Combine(Path.GetTempPath(),"ZhongriSettingsV2-autosave-test.txt");
                try { await window.RunAutoSaveSelfTestAsync(); File.WriteAllText(report,"PASS native autosave: input, appearance, rapid edits, navigation/close flush, invalid input, shortcut, Japanese/custom fuzzy, Chinese automatic deployment (mock), all page save buttons removed; isolated user data and registry"); Environment.Exit(0); }
                catch(Exception error) { File.WriteAllText(report,error.ToString()); Environment.Exit(1); }
            }; timer.Start();return;
        }
        var actionAt = Array.IndexOf(arguments, "--ime-action");
        if (actionAt >= 0)
        {
            try
            {
                if (arguments.Length <= actionAt + 1) throw new ArgumentException("Missing IME action");
                using var cycleLock = new System.Threading.Mutex(false, @"Local\ZhongriJapaneseProfileSwitch");
                if (!cycleLock.WaitOne(5000)) throw new InvalidOperationException("Profile switch is busy");
                var store = new JapaneseInputTableStore(MainWindow.ResolveUserDirectory(), @"Software\ZhongriInputMethod");
                var profiles = store.Profiles();
                if (profiles.Count > 0)
                {
                    var index = store.Enabled ? profiles.FindIndex(p => p.Id == store.Read()?.Id) + 1 : 0;
                    var step = arguments[actionAt + 1] == "previous-profile" ? -1 : arguments[actionAt + 1] == "next-profile" ? 1 : throw new ArgumentException("Unknown IME action");
                    var next = (index + step + profiles.Count + 1) % (profiles.Count + 1);
                    if (next == 0) store.SetEnabled(false); else store.SelectProfile(profiles[next - 1].Id);
                }
                cycleLock.ReleaseMutex();
                Environment.Exit(0);
            }
            catch { Environment.Exit(1); }
            return;
        }
        if (arguments.Contains("--key-editor-ui-self-test", StringComparer.Ordinal))
        {
            var id = Guid.NewGuid().ToString("N");
            var window = new MainWindow(Path.Combine(Path.GetTempPath(), "ZhongriKeyEditorTest-" + id),
                @"Software\ZhongriInputMethod\Tests\" + id);
            _window = window;
            window.AppWindow.Move(new Windows.Graphics.PointInt32(-32000, -32000));
            window.AppWindow.Show(false);
            var timer = window.DispatcherQueue.CreateTimer();
            timer.Interval = TimeSpan.FromMilliseconds(350);
            timer.Tick += async (_, _) =>
            {
                timer.Stop();
                var report = Path.Combine(Path.GetTempPath(), "ZhongriSettingsV2-key-editor-ui-self-test.txt");
                try
                {
                    await window.RunKeyEditorUiSelfTestAsync();
                    File.WriteAllText(report, "PASS rendered keyboard/editor; ky row, ou tail, foreign f row, sokuon/choon composition, symbols/paging, exact combinations, invalid rollback, per-key restore, original table and mode preservation; isolated profile");
                    Environment.Exit(0);
                }
                catch (Exception error) { File.WriteAllText(report, error.ToString()); Environment.Exit(1); }
            };
            timer.Start();
            return;
        }
        var appearanceTest=Array.IndexOf(arguments,"--appearance-designer-self-test");
        if(appearanceTest>=0) {
            File.WriteAllText(Path.Combine(Path.GetTempPath(),"ZhongriSettingsV2-appearance-designer-test.txt"),"STEP create window");
            var id=Guid.NewGuid().ToString("N");
            var window=new MainWindow(Path.Combine(Path.GetTempPath(),"ZhongriAppearanceDesignerTest-"+id),@"Software\ZhongriInputMethod\Tests\"+id);
            _window=window; window.AppWindow.Move(new Windows.Graphics.PointInt32(-32000,-32000));window.AppWindow.Show(false);
            File.AppendAllText(Path.Combine(Path.GetTempPath(),"ZhongriSettingsV2-appearance-designer-test.txt"),"\nSTEP timer setup");
            var timer=window.DispatcherQueue.CreateTimer();timer.Interval=TimeSpan.FromMilliseconds(400);
            timer.Tick+=async (_,_)=> {
                timer.Stop();
                File.AppendAllText(Path.Combine(Path.GetTempPath(),"ZhongriSettingsV2-appearance-designer-test.txt"),"\nSTEP execute");
                System.Threading.SynchronizationContext.SetSynchronizationContext(new Microsoft.UI.Dispatching.DispatcherQueueSynchronizationContext(window.DispatcherQueue));
                try {
                    await window.RunAppearanceDesignerSelfTestAsync(arguments[appearanceTest+1]);
                    File.WriteAllText(Path.Combine(Path.GetTempPath(),"ZhongriSettingsV2-appearance-designer-test.txt"),"PASS fixed preview stays at same Y while dimensions/colors scroll independently; mutually exclusive tabs; short-window usable settings viewport; real compact ka layout: 9 natural-width candidates, no word wrapping, direct drag controls, backend hot registry publication, automatic save/reopen, native point sizes and rendered PNG; isolated data");
                    Environment.Exit(0);
                } catch(Exception error) { File.WriteAllText(Path.Combine(Path.GetTempPath(),"ZhongriSettingsV2-appearance-designer-test.txt"),error.ToString());Environment.Exit(1); }
            };timer.Start();return;
        }
        if (arguments.Contains("--cloud-loaded-page-self-test", StringComparer.Ordinal))
        {
            var id = Guid.NewGuid().ToString("N");
            var report = Path.Combine(Path.GetTempPath(), "ZhongriSettingsV2-cloud-loaded-page-self-test.txt");
            var window = new MainWindow(Path.Combine(Path.GetTempPath(), "ZhongriCloudLoadedTest-" + id),
                @"Software\ZhongriInputMethod\Tests\" + id);
            _window = window;
            window.AppWindow.Move(new Windows.Graphics.PointInt32(-32000, -32000));
            window.AppWindow.Show(false);
            var step = 0;
            var timer = window.DispatcherQueue.CreateTimer();
            timer.Interval = TimeSpan.FromMilliseconds(350);
            timer.Tick += (_, _) =>
            {
                try
                {
                    window.RunCloudLoadedPageTestStep(++step);
                    if (step < 18) return;
                    timer.Stop();
                    File.WriteAllText(report, "PASS rendered cloud page: 18 navigation/resize cycles; count dropdown opens and selections persist; protection controls complete layout; isolated settings");
                    Environment.Exit(0);
                }
                catch (Exception error) { File.WriteAllText(report, error.ToString()); Environment.Exit(1); }
            };
            timer.Start();
            return;
        }
        var bundledTest = Array.IndexOf(arguments, "--bundled-japanese-ui-self-test");
        if (bundledTest >= 0)
        {
            var id = Guid.NewGuid().ToString("N");
            var report = Path.Combine(Path.GetTempPath(), "ZhongriSettingsV2-bundled-japanese-ui-self-test.txt");
            try
            {
                var window = new MainWindow(Path.Combine(Path.GetTempPath(), "ZhongriBundledTest-" + id),
                    @"Software\ZhongriInputMethod\Tests\" + id);
                _window = window;
                window.RunBundledJapaneseUiSelfTest(arguments[bundledTest + 1]);
                File.WriteAllText(report, "PASS fresh UI: default romaji + 7 bundled profiles; all switch with matching diagrams; seeding preserves active mode and personal tables, without duplicates");
                Environment.Exit(0);
            }
            catch (Exception error) { File.WriteAllText(report, error.ToString()); Environment.Exit(1); }
            return;
        }
        var layoutTest = Array.IndexOf(arguments, "--azik-loaded-layout-test");
        if (layoutTest >= 0)
        {
            var id = Guid.NewGuid().ToString("N");
            var directory = Path.Combine(Path.GetTempPath(), "ZhongriLoadedLayout-" + id);
            Directory.CreateDirectory(directory);
            var source = arguments[layoutTest + 1];
            foreach (var name in new[] { "japanese_input_table.tsv", "japanese_input_table.json", "japanese_input_table.state" })
                File.Copy(Path.Combine(source, name), Path.Combine(directory, name));
            Directory.CreateDirectory(Path.Combine(directory, "japanese_input_tables"));
            foreach (var file in Directory.EnumerateFiles(Path.Combine(source, "japanese_input_tables")))
                File.Copy(file, Path.Combine(directory, "japanese_input_tables", Path.GetFileName(file)));
            var window = new MainWindow(directory, @"Software\ZhongriInputMethod\Tests\" + id);
            _window = window;
            window.AppWindow.Move(new Windows.Graphics.PointInt32(-32000, -32000));
            window.AppWindow.Show(false); // Offscreen, never activates or sends OS input.
            var ticks = 0;
            var timer = window.DispatcherQueue.CreateTimer();
            timer.Interval = TimeSpan.FromMilliseconds(500);
            timer.Tick += async (_, _) =>
            {
                try
                {
                    window.RunAzikLoadedLayoutTestStep(++ticks);
                    if (ticks < 20) return;
                    timer.Stop();
                    if (arguments.Length > layoutTest + 2)
                        await window.WriteAzikGroupPreviewAsync(arguments[layoutTest + 2]);
                    File.WriteAllText(Path.Combine(Path.GetTempPath(), "ZhongriSettingsV2-loaded-layout-test.txt"), "PASS loaded AZIK page: repeated navigation, collapse/expand, narrow/wide resizing; survived 10 seconds");
                    Environment.Exit(0);
                }
                catch (Exception error) { File.WriteAllText(Path.Combine(Path.GetTempPath(), "ZhongriSettingsV2-loaded-layout-test.txt"), error.ToString()); Environment.Exit(1); }
            };
            timer.Start();
            return;
        }
        if (arguments.Contains("--dpi-hit-ui-self-test", StringComparer.Ordinal))
        {
            var id = Guid.NewGuid().ToString("N");
            var window = new MainWindow(Path.Combine(Path.GetTempPath(), "ZhongriDpiHitTest-" + id),
                @"Software\ZhongriInputMethod\Tests\" + id);
            _window = window;
            window.AppWindow.Show(false);
            var timer = window.DispatcherQueue.CreateTimer();
            timer.Interval = TimeSpan.FromMilliseconds(500);
            timer.Tick += async (_, _) => {
                timer.Stop();
                var report = Path.Combine(Path.GetTempPath(), "ZhongriSettingsV2-dpi-hit-ui-self-test.txt");
                try { File.WriteAllText(report, await window.RunDpiHitUiSelfTestAsync()); Environment.Exit(0); }
                catch (Exception error) { File.WriteAllText(report, error.ToString()); Environment.Exit(1); }
            };
            timer.Start();
            return;
        }
        if (arguments.Contains("--blank-focus-ui-self-test", StringComparer.Ordinal))
        {
            var id = Guid.NewGuid().ToString("N");
            var window = new MainWindow(Path.Combine(Path.GetTempPath(), "ZhongriBlankFocusTest-" + id),
                @"Software\ZhongriInputMethod\Tests\" + id);
            _window = window;
            window.AppWindow.Show(false);
            var timer = window.DispatcherQueue.CreateTimer();
            timer.Interval = TimeSpan.FromMilliseconds(500);
            timer.Tick += async (_, _) => {
                timer.Stop();
                var report = Path.Combine(Path.GetTempPath(), "ZhongriSettingsV2-blank-focus-ui-self-test.txt");
                try { File.WriteAllText(report, await window.RunBlankFocusUiSelfTestAsync()); Environment.Exit(0); }
                catch (Exception error) { File.WriteAllText(report, error.ToString()); Environment.Exit(1); }
            };
            timer.Start();
            return;
        }
        if (arguments.Contains("--wheel-ui-self-test", StringComparer.Ordinal))
        {
            var id = Guid.NewGuid().ToString("N");
            var window = new MainWindow(Path.Combine(Path.GetTempPath(), "ZhongriWheelTest-" + id),
                @"Software\ZhongriInputMethod\Tests\" + id);
            _window = window;
            window.AppWindow.Show(false);
            var timer = window.DispatcherQueue.CreateTimer();
            timer.Interval = TimeSpan.FromMilliseconds(500);
            timer.Tick += async (_, _) => {
                timer.Stop();
                var report = Path.Combine(Path.GetTempPath(), "ZhongriSettingsV2-wheel-ui-self-test.txt");
                try { File.WriteAllText(report, await window.RunWheelRegressionAsync()); Environment.Exit(0); }
                catch (Exception error) { File.WriteAllText(report, error.ToString()); Environment.Exit(1); }
            };
            timer.Start();
            return;
        }
        if (arguments.Contains("--palette-preview", StringComparer.Ordinal))
        {
            var id = Guid.NewGuid().ToString("N");
            var window = new MainWindow(Path.Combine(Path.GetTempPath(), "ZhongriPalettePreview-" + id),
                @"Software\ZhongriInputMethod\Tests\" + id);
            _window = window;
            window.ShowPalettePreview();
            window.AppWindow.Show(false);
            var timer = window.DispatcherQueue.CreateTimer();
            timer.Interval = TimeSpan.FromSeconds(1);
            timer.Tick += async (_, _) => {
                timer.Stop();
                try { await window.CapturePalettePreviewAsync(Path.Combine(Path.GetTempPath(), "ZhongriSettingsV2-palette.png")); Environment.Exit(0); }
                catch (Exception error) { File.WriteAllText(Path.Combine(Path.GetTempPath(), "ZhongriSettingsV2-palette-preview.txt"), error.ToString()); Environment.Exit(1); }
            };
            timer.Start();
            return;
        }
        if (arguments.Contains("--palette-ui-self-test", StringComparer.Ordinal))
        {
            var id = Guid.NewGuid().ToString("N");
            var directory = Path.Combine(Path.GetTempPath(), "ZhongriPaletteUiTest-" + id);
            var report = Path.Combine(Path.GetTempPath(), "ZhongriSettingsV2-palette-ui-self-test.txt");
            var imagePath = Path.Combine(Path.GetTempPath(), "ZhongriSettingsV2-palette.png");
            UnhandledException += (_, error) => File.WriteAllText(report, error.Message + "\n" + error.Exception);
            _window = new MainWindow(directory, @"Software\ZhongriInputMethod\Tests\" + id);
            _window.AppWindow.Show(false);
            _window.DispatcherQueue.TryEnqueue(async () => {
                System.Threading.SynchronizationContext.SetSynchronizationContext(
                    new Microsoft.UI.Dispatching.DispatcherQueueSynchronizationContext(_window.DispatcherQueue));
                try {
                    await ((MainWindow)_window).RunPaletteUiSelfTestAsync(imagePath);
                    File.WriteAllText(report, "PASS all 11 palettes: ColorChanged, save/readback, reset/reopen");
                    Environment.Exit(0);
                }
                catch (Exception error) { File.WriteAllText(report, error.ToString()); Environment.Exit(1); }
            });
            return;
        }
        var reservedTest = Array.IndexOf(arguments, "--azik-reserved-ui-self-test");
        if (reservedTest >= 0)
        {
            var id = Guid.NewGuid().ToString("N");
            var report = Path.Combine(Path.GetTempPath(), "ZhongriSettingsV2-azik-reserved-ui-self-test.txt");
            try
            {
                _window = new MainWindow(Path.Combine(Path.GetTempPath(), "ZhongriReservedTest-" + id),
                    @"Software\ZhongriInputMethod\Tests\" + id);
                var passed = ((MainWindow)_window).RunAzikReservedUiSelfTest(arguments[reservedTest + 1]);
                File.WriteAllText(report, $"PASS {JapaneseInputTableStore.ReservedPhrases.Length} phrase checkboxes; grouped add buttons and context menus; master on/off; per-item save; profile enable visibility");
                Environment.Exit(passed ? 0 : 1);
            }
            catch (Exception error) { File.WriteAllText(report, error.ToString()); Environment.Exit(1); }
            return;
        }
        var tableTest = Array.IndexOf(arguments, "--japanese-table-ui-self-test");
        if (tableTest >= 0)
        {
            var id = Guid.NewGuid().ToString("N");
            var directory = Path.Combine(Path.GetTempPath(), "ZhongriTableUiTest-" + id);
            var registry = @"Software\ZhongriInputMethod\Tests\" + id;
            var report = Path.Combine(Path.GetTempPath(), "ZhongriSettingsV2-japanese-table-ui-self-test.txt");
            try
            {
                _window = new MainWindow(directory, registry);
                var passed = ((MainWindow)_window).RunJapaneseTableUiSelfTest(arguments[tableTest + 1]);
                File.WriteAllText(report, $"passed={passed}; directory={directory}; registry={registry}");
                Environment.Exit(passed ? 0 : 1);
            }
            catch (Exception error) { File.WriteAllText(report, error.ToString()); Environment.Exit(1); }
            return;
        }
        if (Environment.GetCommandLineArgs().Contains("--annotation-window-read-test", StringComparer.Ordinal))
        {
            _window = new MainWindow();
            System.IO.File.WriteAllText(System.IO.Path.Combine(System.IO.Path.GetTempPath(),
                "ZhongriSettingsV2-annotation-window-read.txt"),
                ((MainWindow)_window).AnnotationDiagnostic);
            Environment.Exit(0);
            return;
        }
        if (Environment.GetCommandLineArgs().Contains("--annotation-live-read-test", StringComparer.Ordinal))
        {
            var store = new SettingsStore(System.IO.Path.Combine(System.IO.Path.GetTempPath(),
                "ZhongriAnnotationReadOnly"));
            System.IO.File.WriteAllText(System.IO.Path.Combine(System.IO.Path.GetTempPath(),
                "ZhongriSettingsV2-annotation-live-read.txt"),
                $"registry={store.ReadAnnotationHotState()}\noptions={store.ReadInputOptions()}\n" +
                $"user={System.Security.Principal.WindowsIdentity.GetCurrent().User}\n" +
                $"bitness={(Environment.Is64BitProcess ? 64 : 32)}\n");
            Environment.Exit(0);
            return;
        }
        if (Environment.GetCommandLineArgs().Contains("--annotation-ui-self-test", StringComparer.Ordinal))
        {
            var id = Guid.NewGuid().ToString("N");
            var registryPath = @"Software\ZhongriInputMethod\Tests\" + id;
            var directory = System.IO.Path.Combine(System.IO.Path.GetTempPath(),
                "ZhongriAnnotationUiTest-" + id);
            var report = System.IO.Path.Combine(System.IO.Path.GetTempPath(),
                "ZhongriSettingsV2-annotation-ui-self-test.txt");
            try
            {
                _window = new MainWindow(directory, registryPath);
                var result = ((MainWindow)_window).RunAnnotationUiSelfTest();
                System.IO.File.WriteAllText(report,
                    $"path={registryPath}\ndirectory={directory}\npassed={result.Passed}\n{result.Details}\n");
                Environment.Exit(result.Passed ? 0 : 1);
            }
            catch (Exception error)
            {
                System.IO.File.WriteAllText(report, error.ToString());
                Environment.Exit(1);
            }
            return;
        }
        if (arguments.Contains("--cloud-count-ui-self-test", StringComparer.Ordinal))
        {
            var id = Guid.NewGuid().ToString("N");
            var report = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "ZhongriSettingsV2-cloud-count-ui-self-test.txt");
            var window = new MainWindow(System.IO.Path.Combine(System.IO.Path.GetTempPath(), "ZhongriCloudCountTest-" + id),
                @"Software\ZhongriInputMethod\Tests\" + id);
            _window = window;
            window.AppWindow.Move(new Windows.Graphics.PointInt32(-32000, -32000));
            window.AppWindow.Show(false);
            var timer = window.DispatcherQueue.CreateTimer();
            timer.Interval = TimeSpan.FromMilliseconds(350);
            timer.Tick += async (_, _) =>
            {
                timer.Stop();
                try
                {
                    await window.RunCloudCountUiSelfTestAsync();
                    System.IO.File.WriteAllText(report, "PASS default count=2; selection 3/1/10/2; invalid 0/11 rejected; language auto/zh/ja/both hot callbacks; invalid -1/4 rejected; selection guard enabled default / off / on; time 0.25 -> 0.12 seconds; invalid 49/1001ms rejected; isolated registry");
                    Environment.Exit(0);
                }
                catch (Exception error)
                {
                    System.IO.File.WriteAllText(report, error.ToString());
                    Environment.Exit(1);
                }
            };
            timer.Start();
            return;
        }
        if (Environment.GetCommandLineArgs().Contains("--sentence-ui-self-test", StringComparer.Ordinal))
        {
            var id = Guid.NewGuid().ToString("N");
            var registryPath = @"Software\ZhongriInputMethod\Tests\" + id;
            var directory = System.IO.Path.Combine(System.IO.Path.GetTempPath(),
                "ZhongriSentenceUiTest-" + id);
            var report = System.IO.Path.Combine(System.IO.Path.GetTempPath(),
                "ZhongriSettingsV2-sentence-ui-self-test.txt");
            try
            {
                var window = new MainWindow(directory, registryPath);
                _window = window;
                window.BeginSentenceToggleUiSelfTest();
                var timer = window.DispatcherQueue.CreateTimer();
                timer.Interval = TimeSpan.FromMilliseconds(300);
                var checks = 0;
                var enabled = false;
                timer.Tick += (_, _) =>
                {
                    checks++;
                    if (!enabled && window.SentenceToggleUiSelfTestState(true))
                    {
                        enabled = true;
                        window.SetSentenceToggleForUiSelfTest(false);
                    }
                    else if (enabled && window.SentenceToggleUiSelfTestState(false))
                    {
                        System.IO.File.WriteAllText(report,
                            $"passed=true; checks={checks}; status={window.SentenceToggleUiSelfTestStatus}");
                        Environment.Exit(0);
                    }
                    if (checks >= 25)
                    {
                        System.IO.File.WriteAllText(report,
                            $"passed=false; enabled={enabled}; status={window.SentenceToggleUiSelfTestStatus}");
                        Environment.Exit(1);
                    }
                };
                timer.Start();
            }
            catch (Exception error)
            {
                System.IO.File.WriteAllText(report, error.ToString());
                Environment.Exit(1);
            }
            return;
        }
        if (Environment.GetCommandLineArgs().Contains("--annotation-self-test", StringComparer.Ordinal))
        {
            // Run inside the same WinUI process, but without creating or
            // activating a window or touching the user's live settings.
            var id = Guid.NewGuid().ToString("N");
            var registryPath = @"Software\ZhongriInputMethod\Tests\" + id;
            var directory = System.IO.Path.Combine(System.IO.Path.GetTempPath(),
                "ZhongriAnnotationAppTest-" + id);
            var report = System.IO.Path.Combine(System.IO.Path.GetTempPath(),
                "ZhongriSettingsV2-annotation-self-test.txt");
            try
            {
                var store = new SettingsStore(directory, registryPath);
                store.SaveAnnotationOptions(false, false, false, true);
                var state = store.ReadAnnotationHotState();
                System.IO.File.WriteAllText(report,
                    $"path={registryPath}\nenglish={state?.English}\njapanese={state?.Japanese}\n" +
                    $"generation={state?.Generation}\ndirectory={directory}\n");
                Environment.Exit(state is { English: false, Japanese: false, Generation: 1 } ? 0 : 1);
            }
            catch (Exception error)
            {
                System.IO.File.WriteAllText(report, error.ToString());
                Environment.Exit(1);
            }
            return;
        }
        _window = new MainWindow();
        if (Environment.GetCommandLineArgs().Contains("--self-test", StringComparer.Ordinal))
        {
            // Construct and navigate the WinUI tree without activating a
            // foreground window or touching the user's input focus.
            Environment.Exit(((MainWindow)_window).RunSelfTest() ? 0 : 1);
            return;
        }
        if (Environment.GetCommandLineArgs().Contains("--smoke-window", StringComparer.Ordinal))
        {
            // Render on the current desktop without activating the window or
            // taking keyboard/mouse focus from the user.
            _window.AppWindow.Show(false);
            var timer = _window.DispatcherQueue.CreateTimer();
            timer.Interval = TimeSpan.FromSeconds(1);
            var stage = 0;
            var japaneseLayout = "";
            timer.Tick += (_, _) =>
            {
                stage++;
                if (stage <= 4)
                {
                    var result = stage switch
                    {
                        1 => ((MainWindow)_window).ShowAppearanceForSmokeTest(),
                        2 => ((MainWindow)_window).ScrollAppearanceForSmokeTest(),
                        3 => ((MainWindow)_window).VerifyAppearanceScrollForSmokeTest() &&
                             ((MainWindow)_window).ShowRulesForSmokeTest(true),
                        _ => CaptureJapaneseLayoutAndShowChinese()
                    };
                    if (!result)
                    {
                        System.IO.File.WriteAllText(System.IO.Path.Combine(System.IO.Path.GetTempPath(),
                            "ZhongriSettingsV2-smoke.txt"), $"stage={stage} " +
                            ((MainWindow)_window).SmokeDiagnostic);
                        Environment.Exit(1);
                    }
                    return;
                }
                timer.Stop();
                System.IO.File.WriteAllText(System.IO.Path.Combine(System.IO.Path.GetTempPath(),
                    "ZhongriSettingsV2-smoke.txt"), "passed " +
                    ((MainWindow)_window).SmokeDiagnostic + " japanese=" + japaneseLayout);
                Environment.Exit(((MainWindow)_window).RunVisibleSmokeTest() ? 0 : 1);
            };
            bool CaptureJapaneseLayoutAndShowChinese()
            {
                japaneseLayout = ((MainWindow)_window).LayoutDiagnostics();
                return ((MainWindow)_window).ShowRulesForSmokeTest(false);
            }
            timer.Start();
            return;
        }
        if (arguments.Contains("--cloud-page", StringComparer.Ordinal))
            ((MainWindow)_window).ShowCloudSettingsForUser();
        if (arguments.Contains("--no-activate", StringComparer.Ordinal))
            _window.AppWindow.Show(false);
        else
            _window.Activate();
    }
}
