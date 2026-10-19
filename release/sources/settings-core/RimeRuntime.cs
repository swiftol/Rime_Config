using System;
using System.Diagnostics;
using System.IO;
using System.Threading.Tasks;
using Microsoft.Win32;

namespace RimeSettings;

internal static class RimeRuntime
{
    public static string LocateRoot()
    {
        var root = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\ZhongriInputMethod")?.GetValue("ZhongriRoot") as string;
        if (string.IsNullOrWhiteSpace(root))
            root = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\WOW6432Node\ZhongriInputMethod")?.GetValue("ZhongriRoot") as string;
        if (string.IsNullOrWhiteSpace(root))
            root = Registry.CurrentUser.OpenSubKey(@"SOFTWARE\ZhongriInputMethod")?.GetValue("ZhongriRoot") as string;
        if (!string.IsNullOrWhiteSpace(root) && Directory.Exists(root)) return root;

        var packaged = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, ".."));
        if (File.Exists(Path.Combine(packaged, "ZhongriServer.exe"))) return packaged;

        foreach (var candidate in new[]
        {
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "ZhongriInputMethod"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "ZhongriInputMethod")
        })
            if (File.Exists(Path.Combine(candidate, "ZhongriServer.exe"))) return candidate;

        throw new DirectoryNotFoundException("找不到中日输入法安装目录，请重新安装中日输入法。");
    }

    public static async Task DeployAsync()
    {
        await Task.Run(() =>
        {
            var root = LocateRoot();
            var deployer = Path.Combine(root, "ZhongriDeployer.exe");
            var server = Path.Combine(root, "ZhongriServer.exe");
            if (!File.Exists(deployer)) throw new FileNotFoundException("找不到中日输入法部署器。", deployer);
            if (!File.Exists(server)) throw new FileNotFoundException("找不到中日输入法服务程序。", server);

            StopServer();
            try
            {
                using var process = Process.Start(new ProcessStartInfo(deployer, "/deploy")
                {
                    UseShellExecute = false,
                    CreateNoWindow = true
                });
                if (process is null) throw new InvalidOperationException("无法启动中日输入法部署器。");
                if (!process.WaitForExit(1800000))
                {
                    process.Kill();
                    throw new TimeoutException("中日输入法完整部署超过 30 分钟，已停止本次部署。");
                }
                if (process.ExitCode != 0)
                    throw new InvalidOperationException($"中日输入法部署失败，退出代码：{process.ExitCode}");
            }
            finally
            {
                StartServer(server);
                StartZhongriMozcBridge(root);
            }
        });
    }

    public static async Task RestartServerAsync()
    {
        await Task.Run(() =>
        {
            var root = LocateRoot();
            var server = Path.Combine(root, "ZhongriServer.exe");
            if (!File.Exists(server)) throw new FileNotFoundException("找不到中日输入法服务程序。", server);
            StopServer();
            StartServer(server);
            StartZhongriMozcBridge(root);
        });
    }

    public static void OpenRimeDirectory(string rimeDirectory) =>
        Process.Start(new ProcessStartInfo("explorer.exe", $"\"{rimeDirectory}\"") { UseShellExecute = true });

    private static void StopServer()
    {
        foreach (var process in Process.GetProcessesByName("ZhongriServer"))
            try { process.Kill(); process.WaitForExit(3000); } catch { }
    }

    private static void StartServer(string server) =>
        Process.Start(new ProcessStartInfo(server) { UseShellExecute = true, WindowStyle = ProcessWindowStyle.Hidden });

    private static void StartZhongriMozcBridge(string root)
    {
        var mozcRoot = Path.Combine(root, "mozc");
        var bridge = Path.Combine(mozcRoot, "ZhongriMozcBridge.exe");
        var converter = Path.Combine(mozcRoot, "converter", "converter_main.exe");
        var romanTable = Path.Combine(mozcRoot, "romanji-hiragana.tsv");
        if (!File.Exists(bridge) || !File.Exists(converter) || !File.Exists(romanTable)) return;

        foreach (var process in Process.GetProcessesByName("ZhongriMozcBridge"))
        {
            try
            {
                if (string.Equals(process.MainModule?.FileName, bridge,
                                  StringComparison.OrdinalIgnoreCase))
                    return;
            }
            catch { }
        }

        var profile = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "ZhongriInputMethod", "MozcProfile");
        var mailbox = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "ZhongriInputMethod", "mozc_v2_mailbox");
        Directory.CreateDirectory(profile);
        Directory.CreateDirectory(mailbox);
        string Quote(string value) => "\"" + value + "\"";
        var arguments = string.Join(" ", new[]
        {
            Quote(converter), Quote(mozcRoot), Quote(profile), Quote(romanTable), Quote(mailbox)
        });
        try
        {
            Process.Start(new ProcessStartInfo(bridge, arguments)
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                WindowStyle = ProcessWindowStyle.Hidden
            });
        }
        catch
        {
            // Preserve the normal Rime candidate pipeline: V2 will fall back
            // to its Japanese dictionary when the local bridge cannot start.
        }
    }
}
