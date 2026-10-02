using System;
using System.Diagnostics;
using System.IO;

internal static class MozcV2Launcher
{
    private static string Quote(string value) { return "\"" + value + "\""; }

    [STAThread]
    private static int Main()
    {
        try
        {
            string installRoot = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');
            string mozcRoot = Path.Combine(installRoot, "mozc");
            string bridge = Path.Combine(mozcRoot, "MozcBridge.exe");
            string converter = Path.Combine(mozcRoot, "converter", "converter_main.exe");
            string romanTable = Path.Combine(mozcRoot, "romanji-hiragana.tsv");
            if (!File.Exists(bridge) || !File.Exists(converter) || !File.Exists(romanTable)) return 2;

            foreach (Process process in Process.GetProcessesByName("MozcBridge"))
            {
                try
                {
                    if (String.Equals(process.MainModule.FileName, bridge,
                                      StringComparison.OrdinalIgnoreCase)) return 0;
                }
                catch { }
            }

            string profile = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "RimeChineseJapanese", "mozc-v2-profile");
            string mailbox = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "Rime", "mozc_v2_mailbox");
            Directory.CreateDirectory(profile);
            Directory.CreateDirectory(mailbox);
            string arguments = String.Join(" ", new[]
            {
                Quote(converter), Quote(mozcRoot), Quote(profile), Quote(romanTable), Quote(mailbox)
            });
            Process.Start(new ProcessStartInfo(bridge, arguments)
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                WindowStyle = ProcessWindowStyle.Hidden
            });
            return 0;
        }
        catch { return 1; }
    }
}
