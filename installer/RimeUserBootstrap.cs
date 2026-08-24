using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using Microsoft.Win32;

internal static class Program
{
    private const string Version = "1.0.0";
    private static string _logFile = "";

    private static void Log(string message)
    {
        string line = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + "  " + message;
        Console.WriteLine(line);
        File.AppendAllText(_logFile, line + Environment.NewLine, new UTF8Encoding(false));
    }

    private static void WriteUserDir(RegistryView view, string value)
    {
        using (var root = RegistryKey.OpenBaseKey(RegistryHive.CurrentUser, view))
        using (var key = root.CreateSubKey(@"Software\Rime\Weasel"))
            key.SetValue("RimeUserDir", value, RegistryValueKind.String);
    }

    private static string ReadUserDir(RegistryView view)
    {
        using (var root = RegistryKey.OpenBaseKey(RegistryHive.CurrentUser, view))
        using (var key = root.OpenSubKey(@"Software\Rime\Weasel"))
            return Convert.ToString(key == null ? null : key.GetValue("RimeUserDir"));
    }

    private static void CopyDirectory(string source, string target)
    {
        Directory.CreateDirectory(target);
        foreach (string directory in Directory.GetDirectories(source, "*", SearchOption.AllDirectories))
            Directory.CreateDirectory(directory.Replace(source, target));
        foreach (string file in Directory.GetFiles(source, "*", SearchOption.AllDirectories))
            File.Copy(file, file.Replace(source, target), true);
    }

    private static void RestorePersonalData(string backup, string target)
    {
        if (String.IsNullOrEmpty(backup) || !Directory.Exists(backup)) return;
        foreach (string directoryName in new[] { "sync", "clipboard" })
        {
            string source = Path.Combine(backup, directoryName);
            if (Directory.Exists(source)) CopyDirectory(source, Path.Combine(target, directoryName));
        }
        foreach (string name in new[] { "custom_phrase.txt", "user.yaml", "installation.yaml" })
        {
            string source = Path.Combine(backup, name);
            if (File.Exists(source)) File.Copy(source, Path.Combine(target, name), true);
        }
        string luaSource = Path.Combine(backup, "lua", "common_phrase_data.lua");
        if (File.Exists(luaSource))
        {
            Directory.CreateDirectory(Path.Combine(target, "lua"));
            File.Copy(luaSource, Path.Combine(target, "lua", "common_phrase_data.lua"), true);
        }
        foreach (string userDb in Directory.GetDirectories(backup, "*.userdb", SearchOption.TopDirectoryOnly))
            CopyDirectory(userDb, Path.Combine(target, Path.GetFileName(userDb)));
        foreach (string userDbFile in Directory.GetFiles(backup, "*.userdb*", SearchOption.TopDirectoryOnly))
            File.Copy(userDbFile, Path.Combine(target, Path.GetFileName(userDbFile)), true);
    }

    private static int RunWithProgress(string file, string args, int timeoutMinutes)
    {
        var info = new ProcessStartInfo(file, args) { UseShellExecute = false, CreateNoWindow = true };
        using (var process = Process.Start(info))
        {
            DateTime started = DateTime.Now;
            while (!process.WaitForExit(2000))
            {
                TimeSpan elapsed = DateTime.Now - started;
                Console.Write("\r正在编译中日词库：{0:mm\\:ss}（首次安装可能需要数分钟）", elapsed);
                if (elapsed.TotalMinutes >= timeoutMinutes)
                {
                    try { process.Kill(); } catch { }
                    Console.WriteLine();
                    return -2;
                }
            }
            Console.WriteLine();
            return process.ExitCode;
        }
    }

    public static int Main(string[] args)
    {
        Console.OutputEncoding = new UTF8Encoding(false);
        bool quiet = args.Any(a => a.Equals("--quiet", StringComparison.OrdinalIgnoreCase));
        string installRoot = args.FirstOrDefault(a => !a.StartsWith("--")) ?? AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');
        string rimeDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Rime");
        string stateDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "RimeChineseJapanese");
        Directory.CreateDirectory(stateDir);
        _logFile = Path.Combine(stateDir, "install.log");
        try
        {
            string template = Path.Combine(installRoot, "config");
            string deployer = Path.Combine(installRoot, "WeaselDeployer.exe");
            string server = Path.Combine(installRoot, "WeaselServer.exe");
            if (!Directory.Exists(template) || !File.Exists(deployer) || !Directory.Exists(Path.Combine(installRoot, "data")))
                throw new InvalidOperationException("安装运行库不完整，请重新下载安装包。安装目录：" + installRoot);

            Log("开始配置 V" + Version + "，当前用户：" + Environment.UserName);
            WriteUserDir(RegistryView.Registry32, rimeDir);
            WriteUserDir(RegistryView.Registry64, rimeDir);
            if (!String.Equals(ReadUserDir(RegistryView.Registry32), rimeDir, StringComparison.OrdinalIgnoreCase) ||
                !String.Equals(ReadUserDir(RegistryView.Registry64), rimeDir, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("用户目录注册表回读验证失败。");
            Log("用户目录已统一为：" + rimeDir);

            foreach (string processName in new[] { "WeaselDeployer", "WeaselServer" })
                foreach (Process process in Process.GetProcessesByName(processName))
                    try { process.Kill(); process.WaitForExit(3000); } catch { }

            string backup = null;
            if (Directory.Exists(rimeDir))
            {
                backup = Path.Combine(Path.GetDirectoryName(rimeDir), "Rime_Backup_before_CNJP_1_0_" + DateTime.Now.ToString("yyyyMMdd_HHmmss"));
                Directory.Move(rimeDir, backup);
                string oldBuild = Path.Combine(backup, "build");
                if (Directory.Exists(oldBuild)) try { Directory.Delete(oldBuild, true); } catch { }
                File.WriteAllText(Path.Combine(stateDir, "last-backup.txt"), backup, new UTF8Encoding(false));
                Log("原配置已备份：" + backup);
            }

            CopyDirectory(template, rimeDir);
            RestorePersonalData(backup, rimeDir);
            Log("项目配置已更新，个人词频、常用语和同步数据已保留。");

            int exit = RunWithProgress(deployer, "/deploy", 45);
            if (exit != 0) throw new InvalidOperationException("部署器退出代码：" + exit);
            File.WriteAllText(Path.Combine(stateDir, "configured-1.0.0.txt"), DateTime.Now.ToString("o"), new UTF8Encoding(false));
            if (File.Exists(server)) Process.Start(new ProcessStartInfo(server) { UseShellExecute = true });
            Log("V1.0.0 部署完成，无需重启电脑。");
            if (!quiet) { Console.WriteLine("安装完成，按 Enter 关闭窗口。"); Console.ReadLine(); }
            return 0;
        }
        catch (Exception ex)
        {
            Log("失败：" + ex.Message);
            if (!quiet) { Console.WriteLine("请把日志发给开发者：" + _logFile); Console.WriteLine("按 Enter 关闭窗口。"); Console.ReadLine(); }
            return 1;
        }
    }
}
