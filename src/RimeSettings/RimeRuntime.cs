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
        var root = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Rime\Weasel")?.GetValue("WeaselRoot") as string;
        if (string.IsNullOrWhiteSpace(root))
            root = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\WOW6432Node\Rime\Weasel")?.GetValue("WeaselRoot") as string;
        if (string.IsNullOrWhiteSpace(root))
            root = Registry.CurrentUser.OpenSubKey(@"SOFTWARE\Rime\Weasel")?.GetValue("WeaselRoot") as string;
        if (!string.IsNullOrWhiteSpace(root) && Directory.Exists(root)) return root;

        var packaged = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, ".."));
        if (File.Exists(Path.Combine(packaged, "WeaselServer.exe"))) return packaged;

        foreach (var candidate in new[]
        {
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "RimeChineseJapanese"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Rime")
        })
            if (File.Exists(Path.Combine(candidate, "WeaselServer.exe"))) return candidate;

        throw new DirectoryNotFoundException("找不到小狼毫安装目录，请重新安装雾凇拼音·中日。");
    }

    public static async Task DeployAsync()
    {
        await Task.Run(() =>
        {
            var root = LocateRoot();
            var deployer = Path.Combine(root, "WeaselDeployer.exe");
            var server = Path.Combine(root, "WeaselServer.exe");
            if (!File.Exists(deployer)) throw new FileNotFoundException("找不到小狼毫部署器。", deployer);
            if (!File.Exists(server)) throw new FileNotFoundException("找不到小狼毫服务程序。", server);

            StopServer();
            try
            {
                using var process = Process.Start(new ProcessStartInfo(deployer, "/deploy")
                {
                    UseShellExecute = false,
                    CreateNoWindow = true
                });
                if (process is null) throw new InvalidOperationException("无法启动小狼毫部署器。");
                if (!process.WaitForExit(180000))
                {
                    process.Kill();
                    throw new TimeoutException("小狼毫部署超过 3 分钟，已停止本次部署。");
                }
                if (process.ExitCode != 0)
                    throw new InvalidOperationException($"小狼毫部署失败，退出代码：{process.ExitCode}");
            }
            finally
            {
                StartServer(server);
            }
        });
    }

    public static async Task RestartServerAsync()
    {
        await Task.Run(() =>
        {
            var server = Path.Combine(LocateRoot(), "WeaselServer.exe");
            if (!File.Exists(server)) throw new FileNotFoundException("找不到小狼毫服务程序。", server);
            StopServer();
            StartServer(server);
        });
    }

    public static void OpenRimeDirectory(string rimeDirectory) =>
        Process.Start(new ProcessStartInfo("explorer.exe", $"\"{rimeDirectory}\"") { UseShellExecute = true });

    private static void StopServer()
    {
        foreach (var process in Process.GetProcessesByName("WeaselServer"))
            try { process.Kill(); process.WaitForExit(3000); } catch { }
    }

    private static void StartServer(string server) =>
        Process.Start(new ProcessStartInfo(server) { UseShellExecute = true, WindowStyle = ProcessWindowStyle.Hidden });
}
