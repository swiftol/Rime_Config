using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32;
using System.Windows.Forms;
using System.Web.Script.Serialization;

internal static class ZhongriLauncher {
  const string Tip = "0804:{760B5BB8-680D-4D4C-920A-D32940AD6144}{9DFBE6A5-7503-462F-A056-7F262CBDE722}";
  [DllImport("input.dll", CharSet=CharSet.Unicode)] static extern bool InstallLayoutOrTip(string value, uint flags);
  static readonly string Root = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');
  static readonly string User = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),"ZhongriInputMethod");
  static string Q(string s) { return "\""+s+"\""; }
  static Process Run(string exe,string args) { return Process.Start(new ProcessStartInfo(exe,args){UseShellExecute=false,CreateNoWindow=true,WindowStyle=ProcessWindowStyle.Hidden}); }
  static bool Same(Process p,string path) { try { return String.Equals(p.MainModule.FileName,path,StringComparison.OrdinalIgnoreCase); } catch {return false;} }
  static void Stop(string name,string path) { foreach(var p in Process.GetProcessesByName(name)) {using(p) { if(Same(p,path)){ p.Kill(); p.WaitForExit(5000); } } } }
  static bool Running(string name,string path) { foreach(var p in Process.GetProcessesByName(name)) {using(p) {if(Same(p,path)) return true;}} return false; }
  static void SeedHotSettings(string registryPath) {
    string file=Path.Combine(Root,"default-hot-settings.json");
    if(!File.Exists(file)) return;
    var values=new JavaScriptSerializer().Deserialize<System.Collections.Generic.Dictionary<string,object>>(File.ReadAllText(file));
    using(var key=Registry.CurrentUser.CreateSubKey(registryPath)) {
      foreach(var pair in values) {
        // Only non-secret primitive preferences are accepted. Never import
        // installation paths, provider credentials or diagnostic generations.
        if(pair.Key.IndexOf("Generation",StringComparison.OrdinalIgnoreCase)>=0 ||
           pair.Key.IndexOf("ApiKey",StringComparison.OrdinalIgnoreCase)>=0 ||
           pair.Key.IndexOf("Protected",StringComparison.OrdinalIgnoreCase)>=0 ||
           pair.Key.IndexOf("Token",StringComparison.OrdinalIgnoreCase)>=0 ||
           pair.Key.Equals("ZhongriRoot",StringComparison.OrdinalIgnoreCase) ||
           pair.Key.Equals("ZhongriUserDir",StringComparison.OrdinalIgnoreCase)) continue;
        if(key.GetValue(pair.Key)!=null) continue;
        if(pair.Value is int) key.SetValue(pair.Key,pair.Value,RegistryValueKind.DWord);
        else if(pair.Key.StartsWith("ShortcutText_",StringComparison.Ordinal) && pair.Value is string)
          key.SetValue(pair.Key,pair.Value,RegistryValueKind.String);
      }
    }
  }
  static bool PersonalConfig(string relative) {
    string value=relative.Replace('/','\\');
    return value.StartsWith("japanese_input_tables\\",StringComparison.OrdinalIgnoreCase) ||
      value.EndsWith(".custom.yaml",StringComparison.OrdinalIgnoreCase) ||
      value.Equals("user.yaml",StringComparison.OrdinalIgnoreCase) ||
      value.Equals("global-settings-profiles.json",StringComparison.OrdinalIgnoreCase) ||
      value.StartsWith("japanese_input_table.",StringComparison.OrdinalIgnoreCase) ||
      value.EndsWith(".flag",StringComparison.OrdinalIgnoreCase) ||
      value.Equals("emoji_second_position.txt",StringComparison.OrdinalIgnoreCase) ||
      value.Equals("chinese_correction_rules.tsv",StringComparison.OrdinalIgnoreCase) ||
      value.StartsWith("custom_",StringComparison.OrdinalIgnoreCase);
  }
  static void SeedProfiles(string userDirectory) {
    string bundled=Path.Combine(Root,"data","japanese_input_tables");
    if(!Directory.Exists(bundled)) return;
    string target=Path.Combine(userDirectory,"japanese_input_tables");
    Directory.CreateDirectory(target);
    var json=new JavaScriptSerializer();
    var names=new System.Collections.Generic.HashSet<string>(StringComparer.OrdinalIgnoreCase);
    foreach(string file in Directory.GetFiles(target,"*.json")) {
      if(file.EndsWith(".reserved.json",StringComparison.OrdinalIgnoreCase)) continue;
      var profile=json.Deserialize<System.Collections.Generic.Dictionary<string,object>>(File.ReadAllText(file));
      if(profile.ContainsKey("Name")) names.Add((string)profile["Name"]);
    }
    foreach(string file in Directory.GetFiles(bundled,"*.json")) {
      var profile=json.Deserialize<System.Collections.Generic.Dictionary<string,object>>(File.ReadAllText(file));
      string name=(string)profile["Name"], id=(string)profile["Id"];
      Guid parsed; if(!Guid.TryParseExact(id,"N",out parsed)) throw new Exception("内置日语方案标识无效。");
      if(names.Contains(name)) continue;
      string table=Path.Combine(target,id+".tsv");
      if(File.Exists(table)) continue;
      File.Copy(Path.Combine(bundled,id+".tsv"),table,false);
      File.Copy(file,Path.Combine(target,id+".json"),false);
      names.Add(name);
    }
  }
  static void Start() {
    string mozc=Path.Combine(Root,"mozc"), bridge=Path.Combine(mozc,"ZhongriMozcBridge.exe");
    if(!Running("ZhongriMozcBridge",bridge)) {
      string profile=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"ZhongriInputMethod","MozcProfile");
      string mailbox=Path.Combine(User,"mozc_v2_mailbox");
      Directory.CreateDirectory(profile); Directory.CreateDirectory(mailbox);
      Run(bridge,Q(Path.Combine(mozc,"converter","converter_main.exe"))+" "+Q(mozc)+" "+Q(profile)+" "+Q(Path.Combine(mozc,"romanji-hiragana.tsv"))+" "+Q(mailbox));
    }
    string server=Path.Combine(Root,"ZhongriServer.exe");
    if(!Running("ZhongriServer",server)) Run(server,"");
  }
  [STAThread] static int Main(string[] args) {
    try {
      string mode=args.Length>0?args[0]:"/start";
      if(mode=="/seed-settings-test" && args.Length==2) {
        if(!args[1].StartsWith(@"Software\ZhongriReleaseTests\",StringComparison.Ordinal)) throw new Exception("测试注册表路径不在隔离范围内。");
        SeedHotSettings(args[1]);return 0;
      }
      if(mode=="/seed-profiles-test" && args.Length==2) { SeedProfiles(Path.GetFullPath(args[1])); return 0; }
      if(mode=="/stop") {Stop("ZhongriServer",Path.Combine(Root,"ZhongriServer.exe")); Stop("ZhongriMozcBridge",Path.Combine(Root,"mozc","ZhongriMozcBridge.exe")); Stop("converter_main",Path.Combine(Root,"mozc","converter","converter_main.exe")); return 0;}
      if(mode=="/disable") {return InstallLayoutOrTip(Tip,1)?0:3;}
      Directory.CreateDirectory(User);
      SeedHotSettings(@"Software\ZhongriInputMethod");
      SeedProfiles(User);
      using(var key=Registry.CurrentUser.CreateSubKey(@"Software\ZhongriInputMethod")) {key.SetValue("ZhongriRoot",Root);key.SetValue("ZhongriUserDir",User);}
      string version=File.ReadAllText(Path.Combine(Root,"package-version.txt")).Trim();
      string marker=Path.Combine(User,"package-version.txt");
      if(mode=="/init" || !File.Exists(marker) || File.ReadAllText(marker).Trim()!=version) {
        Stop("ZhongriServer",Path.Combine(Root,"ZhongriServer.exe"));
        string config=Path.Combine(Root,"config");
        foreach(string file in Directory.GetFiles(config,"*",SearchOption.AllDirectories)) {
          string dest=Path.Combine(User,file.Substring(config.Length+1));
          Directory.CreateDirectory(Path.GetDirectoryName(dest));
          // Personal override files are preserved on every upgrade.
          if(File.Exists(dest) && PersonalConfig(file.Substring(config.Length+1))) continue;
          File.Copy(file,dest,true);
        }
        using(var p=Run(Path.Combine(Root,"ZhongriDeployer.exe"),"/deploy")) {
          if(!p.WaitForExit(1800000) || p.ExitCode!=0) throw new Exception("词库部署未成功，请从开始菜单重新运行中日输入法。");
        }
        if(!InstallLayoutOrTip(Tip,0)) throw new Exception("Windows 未能启用中日输入法，请重新登录后重试。");
        File.WriteAllText(marker,version);
      }
      Start(); return 0;
    } catch(Exception ex) { MessageBox.Show(ex.Message,"中日输入法",MessageBoxButtons.OK,MessageBoxIcon.Error); return 1; }
  }
}
