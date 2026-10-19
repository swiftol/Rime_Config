#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <msctf.h>
#include <sddl.h>
#ifndef TF_IPPMF_FORPROCESS
// Windows SDK msctf.h; older bundled MinGW headers omit this constant.
#define TF_IPPMF_FORPROCESS 0x10000000
#endif
#include <tlhelp32.h>
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

#pragma comment(lib, "ole32.lib")
#ifndef ZHONGRI_POWERSHELL_PATH
#define ZHONGRI_POWERSHELL_PATH L""
#endif

namespace {
constexpr wchar_t kClass[] = L"ZhongriTestBenchWindow";
constexpr wchar_t kClsidKey[] =
    L"CLSID\\{760B5BB8-680D-4D4C-920A-D32940AD6144}\\InprocServer32";
constexpr UINT_PTR kTimer = 1;
constexpr int kEdit = 100;
constexpr int kRestart = 101;
constexpr int kClear = 102;
constexpr int kBackground = 103;

DWORD RunBackgroundTests(bool wait, bool environment_check=false) {
  // __FILE__ is absolute in the maintained build script; no shell, keystroke,
  // global language activation or foreground-window requirement.
  const std::string source(__FILE__);
  const int count=MultiByteToWideChar(CP_UTF8,0,source.data(),int(source.size()),nullptr,0);
  std::wstring project(count,L' ');
  MultiByteToWideChar(CP_UTF8,0,source.data(),int(source.size()),&project[0],count);
  for(int i=0;i<3;++i){const auto slash=project.find_last_of(L"\\/");if(slash==std::wstring::npos)return ERROR_PATH_NOT_FOUND;project.resize(slash);}
  const auto script=project+L"\\tools\\test-zhongri-test-bench.ps1";
  if(GetFileAttributesW(script.c_str())==INVALID_FILE_ATTRIBUTES)return ERROR_FILE_NOT_FOUND;
  wchar_t executable[32768] = {};
  const std::wstring bundled=ZHONGRI_POWERSHELL_PATH;
  if(!bundled.empty() && GetFileAttributesW(bundled.c_str())!=INVALID_FILE_ATTRIBUTES) {
    if(bundled.size()>=_countof(executable))return ERROR_FILENAME_EXCED_RANGE;
    std::copy(bundled.begin(),bundled.end(),executable);
  } else if(!SearchPathW(nullptr,L"pwsh.exe",nullptr,_countof(executable),executable,nullptr))return GetLastError();
  std::wstring command=L"\""+std::wstring(executable)+L"\" -NoProfile -File \""+script+L"\"";
  if(environment_check)command+=L" -LauncherCheckOnly";
  STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
  PROCESS_INFORMATION process{};
  if(!CreateProcessW(executable,&command[0],nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,project.c_str(),&startup,&process))return GetLastError();
  CloseHandle(process.hThread);DWORD result=0;
  if(wait){WaitForSingleObject(process.hProcess,INFINITE);GetExitCodeProcess(process.hProcess,&result);}
  CloseHandle(process.hProcess);return result;
}

HWND g_window = nullptr, g_edit = nullptr, g_status = nullptr;
HWND g_details = nullptr, g_hint = nullptr;
HANDLE g_repair_process = nullptr;
bool g_check_restart=false;
std::wstring g_checked_restart_command;
HFONT g_font = nullptr, g_title_font = nullptr;
HBRUSH g_dark_brush = nullptr;
std::wstring g_loaded_dll, g_initial_registered;
std::wstring g_latest_dll;
FILETIME g_initial_write{};
bool g_initial_write_valid = false;
bool g_relaunching = false;
bool g_diagnostic_mode = false;
HRESULT g_tsf_bootstrap_result = S_OK;
bool g_started_from_relaunch = false;
int g_change_ticks = 0;
HANDLE g_component_context = INVALID_HANDLE_VALUE;
ULONG_PTR g_component_cookie = 0;
std::wstring g_component_manifest;
DWORD g_component_error = 0;
std::wstring g_startup_registry, g_diagnostic_path, g_check_directory;

std::wstring CheckDirectory() {
  if(!g_check_directory.empty())return g_check_directory+L"\\";
  wchar_t temp[MAX_PATH]={};GetTempPathW(MAX_PATH,temp);return temp;
}

bool WriteDiagnostic(const std::wstring& path,const std::wstring& text,DWORD disposition) {
  HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,
                         disposition,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file==INVALID_HANDLE_VALUE)return false;
  const wchar_t bom=0xfeff;DWORD written=0;
  const bool ok=WriteFile(file,&bom,2,&written,nullptr) &&
      WriteFile(file,text.data(),DWORD(text.size()*2),&written,nullptr);
  CloseHandle(file);return ok;
}

std::wstring ReadRegistryDll(HKEY root, const std::wstring& subkey, REGSAM view) {
  HKEY key=nullptr;wchar_t value[32768]={};DWORD bytes=sizeof(value),type=0;
  LONG error=RegOpenKeyExW(root,subkey.c_str(),0,KEY_QUERY_VALUE|view,&key);
  if(error==ERROR_SUCCESS){error=RegQueryValueExW(key,nullptr,nullptr,&type,
      reinterpret_cast<BYTE*>(value),&bytes);RegCloseKey(key);}
  if(error!=ERROR_SUCCESS || type!=REG_SZ)return {};
  value[_countof(value)-1]=0;return value;
}

std::wstring RegistrySnapshot() {
  const std::wstring key=L"Software\\Classes\\"+std::wstring(kClsidKey);
  std::wstring result;HKEY current=nullptr;
  const LONG opened=RegOpenCurrentUser(KEY_READ,&current);
  result=L"RegOpenCurrentUser.error="+std::to_wstring(opened)+L"\r\n";
  for(REGSAM view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY}) {
    const auto suffix=view==KEY_WOW64_64KEY?L"64":L"32";
    result+=L"HKCU"+std::wstring(suffix)+L"="+ReadRegistryDll(HKEY_CURRENT_USER,key,view)+L"\r\n";
    if(current)result+=L"CurrentUserFresh"+std::wstring(suffix)+L"="+ReadRegistryDll(current,key,view)+L"\r\n";
    result+=L"HKLM"+std::wstring(suffix)+L"="+ReadRegistryDll(HKEY_LOCAL_MACHINE,key,view)+L"\r\n";
  }
  if(current)RegCloseKey(current);
  HANDLE token=nullptr;
  if(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) {
    DWORD bytes=0;GetTokenInformation(token,TokenUser,nullptr,0,&bytes);
    std::vector<BYTE> buffer(bytes);
    if(GetTokenInformation(token,TokenUser,buffer.data(),bytes,&bytes)) {
      LPWSTR sid=nullptr;
      if(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid,&sid)) {
        result+=L"UserSID="+std::wstring(sid)+L"\r\n";
        result+=L"ExplicitUser64="+ReadRegistryDll(HKEY_USERS,std::wstring(sid)+L"\\"+key,KEY_WOW64_64KEY)+L"\r\n";
        LocalFree(sid);
      }
    }
    TOKEN_ELEVATION elevation{};
    if(GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&bytes))
      result+=L"Elevated="+std::to_wstring(elevation.TokenIsElevated)+L"\r\n";
    CloseHandle(token);
  }
  return result;
}

std::wstring Lower(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
  return value;
}

std::wstring UserRegisteredDll();

std::wstring RegisteredDll() {
  // HKCR can resolve to the machine-wide (older) registration in some TSF
  // clients.  The per-user registration is the intended installed version.
  const std::wstring user = UserRegisteredDll();
  if (!user.empty()) return user;
  wchar_t path[32768] = {};
  DWORD bytes = sizeof(path);
  if (RegGetValueW(HKEY_CLASSES_ROOT, kClsidKey, nullptr, RRF_RT_REG_SZ,
                   nullptr, path, &bytes) != ERROR_SUCCESS)
    return {};
  return path;
}

std::wstring UserRegisteredDll() {
  // Resolve a fresh current-user handle rather than assuming a predefined
  // handle seen in another process proves this launch sees the same key.
  HKEY current=nullptr;
  if(RegOpenCurrentUser(KEY_READ,&current)!=ERROR_SUCCESS)return {};
  const auto value=ReadRegistryDll(current,L"Software\\Classes\\"+std::wstring(kClsidKey),KEY_WOW64_64KEY);
  RegCloseKey(current);return value;
}

bool BindRegisteredComponent() {
  // Process-local COM redirection: the test bench must exercise the user's
  // registered component, not a cached machine-wide/unversioned alias. This
  // does not change any global COM/TSF registration or other application.
  const std::wstring dll = RegisteredDll();
  const size_t slash = dll.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return false;
  const std::wstring directory = dll.substr(0, slash + 1);
  const std::wstring name = dll.substr(slash + 1);
  if (name.find_first_of(L"&<>\"'") != std::wstring::npos) return false;
  wchar_t path[MAX_PATH] = {};
  // The root assembly's file paths are relative to the manifest itself.
  if (!GetTempFileNameW(directory.c_str(), L"zrt", 0, path))
    return false;
  g_component_manifest = path;
  SetDllDirectoryW(directory.c_str());
  const std::wstring xml =
      L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>"
      L"<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
      L"<assemblyIdentity type=\"win32\" name=\"Zhongri.TestBench.Component\" "
      L"version=\"1.0.0.0\" processorArchitecture=\"amd64\"/>"
      L"<file name=\"" + name + L"\"><comClass "
      L"clsid=\"{760B5BB8-680D-4D4C-920A-D32940AD6144}\" "
      L"threadingModel=\"Apartment\"/></file></assembly>";
  HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_TEMPORARY, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  const wchar_t bom = 0xfeff;
  DWORD written = 0;
  bool saved = WriteFile(file, &bom, sizeof(bom), &written, nullptr) &&
      WriteFile(file, xml.data(), static_cast<DWORD>(xml.size() * sizeof(wchar_t)),
                &written, nullptr);
  CloseHandle(file);
  if (!saved) return false;
  ACTCTXW context{};
  context.cbSize = sizeof(context);
  context.dwFlags = ACTCTX_FLAG_ASSEMBLY_DIRECTORY_VALID;
  context.lpSource = path;
  context.lpAssemblyDirectory = directory.c_str();
  g_component_context = CreateActCtxW(&context);
  const bool activated = g_component_context != INVALID_HANDLE_VALUE &&
                         ActivateActCtx(g_component_context, &g_component_cookie);
  if (!activated) g_component_error = GetLastError();
  return activated;
}

void ReleaseRegisteredComponent() {
  if (g_component_cookie) DeactivateActCtx(0, g_component_cookie);
  if (g_component_context != INVALID_HANDLE_VALUE)
    ReleaseActCtx(g_component_context);
  if (!g_component_manifest.empty()) DeleteFileW(g_component_manifest.c_str());
}

bool FileStamp(const std::wstring& path, FILETIME* stamp) {
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (path.empty() ||
      !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
    return false;
  *stamp = data.ftLastWriteTime;
  return true;
}

bool SameFileIdentity(const std::wstring& left, const std::wstring& right) {
  if (left.empty() || right.empty()) return false;
  HANDLE first = CreateFileW(left.c_str(), FILE_READ_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE |
                                 FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                             nullptr);
  HANDLE second = CreateFileW(right.c_str(), FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE |
                                  FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
  BY_HANDLE_FILE_INFORMATION one{}, two{};
  const bool same = first != INVALID_HANDLE_VALUE &&
                    second != INVALID_HANDLE_VALUE &&
                    GetFileInformationByHandle(first, &one) &&
                    GetFileInformationByHandle(second, &two) &&
                    one.dwVolumeSerialNumber == two.dwVolumeSerialNumber &&
                    one.nFileIndexHigh == two.nFileIndexHigh &&
                    one.nFileIndexLow == two.nFileIndexLow;
  if (first != INVALID_HANDLE_VALUE) CloseHandle(first);
  if (second != INVALID_HANDLE_VALUE) CloseHandle(second);
  return same;
}

bool SameFileContents(const std::wstring& left, const std::wstring& right) {
  if (SameFileIdentity(left, right)) return true;
  HANDLE first = CreateFileW(left.c_str(), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE |
                                 FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                             nullptr);
  HANDLE second = CreateFileW(right.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE |
                                  FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
  bool equal = false;
  LARGE_INTEGER first_size{}, second_size{};
  if (first != INVALID_HANDLE_VALUE && second != INVALID_HANDLE_VALUE &&
      GetFileSizeEx(first, &first_size) &&
      GetFileSizeEx(second, &second_size) &&
      first_size.QuadPart == second_size.QuadPart) {
    BYTE first_block[65536], second_block[65536];
    equal = true;
    for (;;) {
      DWORD first_read = 0, second_read = 0;
      if (!ReadFile(first, first_block, sizeof(first_block), &first_read,
                    nullptr) ||
          !ReadFile(second, second_block, sizeof(second_block), &second_read,
                    nullptr) ||
          first_read != second_read ||
          memcmp(first_block, second_block, first_read) != 0) {
        equal = false;
        break;
      }
      if (first_read == 0) break;
    }
  }
  if (first != INVALID_HANDLE_VALUE) CloseHandle(first);
  if (second != INVALID_HANDLE_VALUE) CloseHandle(second);
  return equal;
}

struct RepairPlan { std::wstring powershell,script,delivery,command; };
bool ReadRepairPlan(RepairPlan& plan) {
  wchar_t self[32768]={},value[32768]={};
  if(!GetModuleFileNameW(nullptr,self,_countof(self)))return false;
  std::wstring directory(self);const auto slash=directory.find_last_of(L"\\/");
  if(slash==std::wstring::npos)return false;directory.resize(slash+1);
  const auto config=directory+L"中日输入法测试台.runtime.ini";
  auto read=[&](const wchar_t* key){GetPrivateProfileStringW(L"Repair",key,L"",value,_countof(value),config.c_str());return std::wstring(value);};
  plan.powershell=read(L"PowerShell");plan.script=read(L"Script");plan.delivery=read(L"DeliveryDirectory");
  for(const auto& path:{plan.powershell,plan.script,plan.delivery})
    if(path.size()<3 || path[1]!=L':' || path.find_first_of(L"\"\r\n")!=std::wstring::npos || GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES)return false;
  plan.command=L"\""+plan.powershell+L"\" -NoProfile -File \""+plan.script+
      L"\" -DeliveryDirectory \""+plan.delivery+L"\" -RepairUserRegistration";
  return true;
}

bool AllRegistrationsCurrent() {
  RepairPlan plan;if(!ReadRepairPlan(plan))return false;
  HKEY current=nullptr;if(RegOpenCurrentUser(KEY_READ,&current)!=ERROR_SUCCESS)return false;
  bool valid=true;const auto key=L"Software\\Classes\\"+std::wstring(kClsidKey);
  std::vector<std::pair<std::wstring,std::wstring>> pairs;
  std::wstring signature=plan.delivery;
  for(REGSAM view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY}) {
    const auto user=ReadRegistryDll(current,key,view),machine=ReadRegistryDll(HKEY_LOCAL_MACHINE,key,view);
    const auto expected=plan.delivery+(view==KEY_WOW64_64KEY?L"\\weaselx64.dll":L"\\weasel.dll");
    for(const auto& path:{user,machine,expected}){
      FILETIME stamp{};FileStamp(path,&stamp);signature+=L"|"+path+L":"+std::to_wstring(stamp.dwLowDateTime)+L":"+std::to_wstring(stamp.dwHighDateTime);
    }
    pairs.push_back({user,expected});pairs.push_back({machine,expected});
  }
  RegCloseKey(current);
  static std::wstring previous;static bool previous_valid=false;
  if(signature==previous)return previous_valid;
  for(const auto& pair:pairs)if(!SameFileContents(pair.first,pair.second))valid=false;
  previous=signature;previous_valid=valid;return valid;
}

void BeginRestrictedRepair() {
  if(g_repair_process)return;
  RepairPlan plan;
  if(!ReadRepairPlan(plan)){SetWindowTextW(g_hint,L"维修配置缺失或失效，未修改注册。请保留本窗口与草稿并联系维护者。");return;}
  STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
  PROCESS_INFORMATION process{};
  auto project=plan.script;
  for(int i=0;i<2;++i){const auto slash=project.find_last_of(L"\\/");if(slash==std::wstring::npos)return;project.resize(slash);}
  if(!CreateProcessW(plan.powershell.c_str(),&plan.command[0],nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,project.c_str(),&startup,&process)) {
    SetWindowTextW(g_hint,L"无法启动维修，未关闭本窗口。请保留草稿。");return;
  }
  CloseHandle(process.hThread);g_repair_process=process.hProcess;
  EnableWindow(GetDlgItem(g_window,kRestart),FALSE);
  SetWindowTextW(g_hint,L"正在从本测试台修复注册。请确认 Windows 管理员授权；取消不会关闭本窗口或清空草稿。成功后另开新版窗口。");
}

bool AllLoadedComponentsMatch(const std::wstring& expected) {
  HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());
  if(snapshot==INVALID_HANDLE_VALUE)return false;
  MODULEENTRY32W module{};module.dwSize=sizeof(module);bool found=false,match=true;
  if(Module32FirstW(snapshot,&module))do{
    const std::wstring name=Lower(module.szModule);
    if(name.rfind(L"zhongrix64",0)==0 && name.size()>=4 && name.substr(name.size()-4)==L".dll"){
      found=true;if(!SameFileContents(module.szExePath,expected))match=false;
    }
  }while(Module32NextW(snapshot,&module));
  CloseHandle(snapshot);return found&&match;
}

std::wstring LatestInstalledDll(const std::wstring& registered) {
  const size_t slash = registered.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return {};
  const std::wstring directory = registered.substr(0, slash + 1);
  const std::wstring pattern = directory + L"zhongrix64*.dll";
  WIN32_FIND_DATAW found{};
  HANDLE search = FindFirstFileW(pattern.c_str(), &found);
  if (search == INVALID_HANDLE_VALUE) return {};
  std::wstring latest;
  FILETIME latest_write{};
  do {
    if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    const std::wstring name = Lower(found.cFileName);
    if (name != L"zhongrix64.dll" &&
        (name.rfind(L"zhongrix64_", 0) != 0 ||
         name.size() < 16 || name.substr(name.size() - 4) != L".dll"))
      continue;
    const int order = CompareFileTime(&found.ftLastWriteTime, &latest_write);
    const std::wstring previous_name =
        latest.empty() ? std::wstring()
                       : Lower(latest.substr(latest.find_last_of(L"\\/") + 1));
    // File copies may preserve identical modification times.  Prefer the
    // newest dated version name rather than whichever file enumeration finds
    // first; the unversioned compatibility alias always ranks last.
    if (latest.empty() || order > 0 ||
        (order == 0 && name != L"zhongrix64.dll" &&
         (previous_name == L"zhongrix64.dll" || name > previous_name))) {
      latest = directory + found.cFileName;
      latest_write = found.ftLastWriteTime;
    }
  } while (FindNextFileW(search, &found));
  FindClose(search);
  return latest;
}

std::wstring ProcessModuleWithPrefix(const wchar_t* prefix) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                         GetCurrentProcessId());
  if (snap == INVALID_HANDLE_VALUE) return {};
  MODULEENTRY32W item{};
  item.dwSize = sizeof(item);
  std::wstring result;
  if (Module32FirstW(snap, &item)) {
    do {
      const std::wstring module = Lower(item.szModule);
      if (module.rfind(prefix, 0) == 0) {
        result = item.szExePath;
        break;
      }
    } while (Module32NextW(snap, &item));
  }
  CloseHandle(snap);
  return result;
}

struct ServerState {
  bool running = false;
  bool current = false;
  DWORD pid = 0;
  std::wstring path;
};

ServerState GetServerState(const std::wstring& expected_path) {
  ServerState result;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return result;
  PROCESSENTRY32W item{};
  item.dwSize = sizeof(item);
  if (Process32FirstW(snap, &item)) {
    do {
      if (Lower(item.szExeFile) != L"zhongriserver.exe") continue;
      HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                   item.th32ProcessID);
      if (!process) continue;
      wchar_t path[32768] = {};
      DWORD length = _countof(path);
      FILETIME created{}, exited{}, kernel{}, user{}, installed{};
      const bool has_path = QueryFullProcessImageNameW(process, 0, path, &length);
      const bool has_times =
          GetProcessTimes(process, &created, &exited, &kernel, &user);
      CloseHandle(process);
      if (!has_path || Lower(path) != Lower(expected_path)) continue;
      result.running = true;
      result.pid = item.th32ProcessID;
      result.path = path;
      result.current = has_times && FileStamp(expected_path, &installed) &&
                       CompareFileTime(&created, &installed) >= 0;
      break;
    } while (Process32NextW(snap, &item));
  }
  CloseHandle(snap);
  return result;
}

bool ActivateZhongri() {
  const CLSID clsid = {
      0x760B5BB8, 0x680D, 0x4D4C,
      {0x92, 0x0A, 0xD3, 0x29, 0x40, 0xAD, 0x61, 0x44}};
  const GUID profile = {
      0x9DFBE6A5, 0x7503, 0x462F,
      {0xA0, 0x56, 0x7F, 0x26, 0x2C, 0xBD, 0xE7, 0x22}};
  ITfInputProcessorProfiles* profiles = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_ITfInputProcessorProfiles,
                                reinterpret_cast<void**>(&profiles));
  if (FAILED(hr) || !profiles) return false;
  profiles->ChangeCurrentLanguage(0x0804);
  hr = profiles->ActivateLanguageProfile(clsid, 0x0804, profile);
  profiles->Release();
  return SUCCEEDED(hr);
}

// Own the TSF thread manager before EDIT creates its lazy input context.
// Otherwise a direct COM probe can load the new component while the actual
// edit's later activation follows a different, previously cached path.
struct BenchTextServices {
  ITfThreadMgr* manager=nullptr;
  ITfDocumentMgr* document=nullptr;
  ITfContext* context=nullptr;
  HRESULT Start() {
    HRESULT hr=CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,
                                IID_ITfThreadMgr,reinterpret_cast<void**>(&manager));
    TfClientId client=0;TfEditCookie cookie=0;
    if(SUCCEEDED(hr))hr=manager->Activate(&client);
    if(SUCCEEDED(hr) && !ActivateZhongri())hr=E_FAIL;
    if(SUCCEEDED(hr))hr=manager->CreateDocumentMgr(&document);
    if(SUCCEEDED(hr))hr=document->CreateContext(client,0,nullptr,&context,&cookie);
    if(SUCCEEDED(hr))hr=document->Push(context);
    if(SUCCEEDED(hr))hr=manager->SetFocus(document);
    return hr;
  }
  void Reset(){if(manager)manager->SetFocus(nullptr);if(document)document->Pop(TF_POPF_ALL);if(context){context->Release();context=nullptr;}if(document){document->Release();document=nullptr;}if(manager){manager->Deactivate();manager->Release();manager=nullptr;}}
  ~BenchTextServices(){Reset();}
};

std::wstring InstalledServer(const std::wstring& registered) {
  const size_t slash = registered.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return {};
  return registered.substr(0, slash + 1) + L"ZhongriServer.exe";
}

void RestartSelf() {
  if (g_relaunching) return;
  g_relaunching = true;
  wchar_t self[32768] = {};
  if (!GetModuleFileNameW(nullptr, self, _countof(self))) {
    g_relaunching = false;
    return;
  }
  std::wstring executable(self);
  const auto slash=executable.find_last_of(L"\\/");
  if(slash!=std::wstring::npos){
    const auto canonical=executable.substr(0,slash+1)+L"中日输入法测试台.exe";
    if(GetFileAttributesW(canonical.c_str())!=INVALID_FILE_ATTRIBUTES)executable=canonical;
  }
  std::wstring command = L"\"";
  command += executable;
  command += L"\" --relaunch";
  if (GetForegroundWindow() != g_window) command += L" --no-activate";
  std::vector<wchar_t> mutable_command(command.begin(), command.end());
  mutable_command.push_back(0);
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if(g_check_restart)g_checked_restart_command=command;
  if (g_check_restart || CreateProcessW(executable.c_str(), mutable_command.data(), nullptr, nullptr, FALSE,
                     0, nullptr, nullptr, &startup, &process)) {
    if(!g_check_restart){CloseHandle(process.hThread);CloseHandle(process.hProcess);}
    // This is a new validation window, never a destructive draft restart.
    SetWindowTextW(g_hint,L"已另开新版测试台。本窗口与原测试文字保留；请在新窗口确认版本就绪后手动验收。");
    g_relaunching=false;
  } else {
    g_relaunching = false;
    SetWindowTextW(g_status, L"无法重启测试台；请手动关闭后重新打开。");
  }
}

void RefreshStatus() {
  const std::wstring registered = RegisteredDll();
  const std::wstring latest = LatestInstalledDll(registered);
  g_latest_dll = latest;
  if (g_loaded_dll.empty())
    g_loaded_dll = ProcessModuleWithPrefix(L"zhongrix64");
  FILETIME current_write{};
  const bool file_exists = FileStamp(registered, &current_write);
  const bool dll_matches =
      !g_loaded_dll.empty() && !registered.empty() &&
      (Lower(g_loaded_dll) == Lower(registered) ||
       SameFileIdentity(g_loaded_dll, registered));
  if (dll_matches && !g_initial_write_valid) {
    g_initial_registered = registered;
    g_initial_write = current_write;
    g_initial_write_valid = file_exists;
  }
  const bool changed_since_load =
      g_initial_write_valid &&
      ((Lower(registered) != Lower(g_initial_registered) &&
        !SameFileIdentity(registered, g_initial_registered)) ||
       !file_exists || CompareFileTime(&current_write, &g_initial_write) != 0);
  const ServerState server = GetServerState(InstalledServer(registered));

  std::wstring status;
  if(FAILED(g_tsf_bootstrap_result))
    status=L"不可验收：测试台自身的 TSF 启动失败，版本检查不能代替此步骤。";
  else if (registered.empty() || !file_exists)
    status = L"不可验收：没有找到已注册的中日输入法 DLL。";
  else if (latest.empty() || !SameFileContents(registered,latest) || !AllRegistrationsCurrent())
    status = L"暂不能验收：注册尚未更新。请点下方“修复注册并新开”。";
  else if (g_loaded_dll.empty())
    status = L"正在加载输入法；请在下方输入框点一下。";
  else if (!dll_matches || changed_since_load)
    status = L"不可验收：本窗口仍加载旧组件；请保存草稿后重新打开测试台。";
  else if(!AllLoadedComponentsMatch(registered))
    status=L"不可验收：本窗口同时驻留不同版本组件，不能以直接加载检查代替输入法激活检查。";
  else if (!server.running)
    status = L"不可验收：中日输入法后台服务没有运行。";
  else if (!server.current)
    status = L"不可验收：后台服务仍是旧进程；请先更新并重启后台服务。";
  else
    status = L"版本检查通过：可在此手动测试，功能仍需你验收。";
  SetWindowTextW(g_status, status.c_str());
  HWND restart = GetDlgItem(g_window, kRestart);
  SetWindowTextW(restart,AllRegistrationsCurrent()?L"另开新版测试台（保留本窗口）":L"修复注册并新开");
  EnableWindow(restart, !g_repair_process && !registered.empty() && file_exists);

  std::wstring details = L"本窗口加载：";
  details += g_loaded_dll.empty() ? L"尚未加载" : g_loaded_dll;
  details += L"\r\n用户注册（64位）：";
  details += registered.empty() ? L"未注册" : registered;
  details += L"\r\n机器注册（64位）："+ReadRegistryDll(HKEY_LOCAL_MACHINE,L"Software\\Classes\\"+std::wstring(kClsidKey),KEY_WOW64_64KEY);
  details += L"\r\n安装目录最新：";
  details += latest.empty() ? L"未找到" : latest;
  details += L"\r\n后台服务：";
  details += server.running
                 ? (L"PID " + std::to_wstring(server.pid) +
                    (server.current ? L" · 当前安装文件" : L" · 旧进程"))
                 : L"未运行";
  SetWindowTextW(g_details, details.c_str());

  if (dll_matches && !changed_since_load) {
    g_change_ticks = 0;
    g_started_from_relaunch = false;
  } else if (!registered.empty() && file_exists &&
             !g_loaded_dll.empty()) {
    // Do not restart a draft window merely because installation changed.
    // The explicit reload button remains available after the user saves it.
    ++g_change_ticks;
  }
}

void SaveStartupDiagnostic() {
  wchar_t executable[32768]={},local[32768]={},status[512]={};
  GetModuleFileNameW(nullptr,executable,_countof(executable));
  GetWindowTextW(g_status,status,_countof(status));
  std::wstring directory;
  if(!g_check_directory.empty())directory=g_check_directory;
  else if(g_diagnostic_mode)directory=CheckDirectory();
  else if(GetEnvironmentVariableW(L"LOCALAPPDATA",local,_countof(local))) {
    directory=std::wstring(local)+L"\\ZhongriInputMethod";
    CreateDirectoryW(directory.c_str(),nullptr);
    directory+=L"\\TestBenchDiagnostics";CreateDirectoryW(directory.c_str(),nullptr);
  }
  if(directory.empty())return;
  g_diagnostic_path=directory+L"\\startup-"+std::to_wstring(GetCurrentProcessId())+
      L"-"+std::to_wstring(GetTickCount64())+L".txt";
  const std::wstring text=L"Build=20261018-user-acceptance-repair-1\r\nPID="+
      std::to_wstring(GetCurrentProcessId())+L"\r\nExecutable="+executable+
      L"\r\nRelaunch="+std::to_wstring(g_started_from_relaunch)+
      L"\r\n[BeforeCOM]\r\n"+g_startup_registry+
      L"[AfterTSF]\r\n"+RegistrySnapshot()+L"Loaded="+
      ProcessModuleWithPrefix(L"zhongrix64")+L"\r\nStatus="+status+
      L"\r\nCoverage=Startup only; no typed text, keystrokes or candidate acceptance.\r\n";
  if(!WriteDiagnostic(g_diagnostic_path,text,CREATE_NEW))g_diagnostic_path.clear();
}

void Layout(int width, int height) {
  const int margin = 20;
  const int content = std::max(200, width - 2 * margin);
  MoveWindow(g_status, margin, 16, content, 34, TRUE);
  MoveWindow(g_details, margin, 55, content, 140, TRUE);
  MoveWindow(g_hint, margin, 204, content, 48, TRUE);
  MoveWindow(g_edit, margin, 260, content, std::max(100, height - 329), TRUE);
  MoveWindow(GetDlgItem(g_window, kRestart), margin, height - 55, 280, 32, TRUE);
  MoveWindow(GetDlgItem(g_window, kClear), margin + 290, height - 55, 100, 32,
             TRUE);
  MoveWindow(GetDlgItem(g_window, kBackground), margin + 400, height - 55, 200, 32, TRUE);
}

LRESULT CALLBACK WindowProc(HWND window, UINT msg, WPARAM wparam,
                            LPARAM lparam) {
  switch (msg) {
    case WM_CREATE: {
      g_window = window;
      SetWindowTextW(window,L"中日输入法测试台 · 2026-10-18 用户验收版");
      g_dark_brush = CreateSolidBrush(RGB(36, 36, 36));
      g_font = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH, L"Microsoft YaHei UI");
      g_title_font = CreateFontW(-20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH, L"Microsoft YaHei UI");
      g_status = CreateWindowW(L"STATIC", L"正在检查输入法版本…",
                               WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window,
                               nullptr, nullptr, nullptr);
      g_details = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0,
                                0, 0, window, nullptr, nullptr, nullptr);
      g_hint = CreateWindowW(
          L"STATIC",
          L"本窗口供你手动验收输入、选词和翻行；后台自测不能替你验收画面。"
          L"若提示注册旧版，请点“修复注册并新开”，确认 Windows 授权；原窗口与草稿保留。",
          WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, nullptr, nullptr,
          nullptr);
      g_edit = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE |
              ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
          0, 0, 0, 0, window, reinterpret_cast<HMENU>(kEdit), nullptr,
          nullptr);
      CreateWindowW(L"BUTTON", L"重新打开测试台", WS_CHILD | WS_VISIBLE,
                    0, 0, 0, 0, window, reinterpret_cast<HMENU>(kRestart),
                    nullptr, nullptr);
      CreateWindowW(L"BUTTON", L"清空测试区", WS_CHILD | WS_VISIBLE,
                    0, 0, 0, 0, window, reinterpret_cast<HMENU>(kClear),
                    nullptr, nullptr);
      CreateWindowW(L"BUTTON", L"后台自测（不抢焦点）", WS_CHILD | WS_VISIBLE,
                    0, 0, 0, 0, window, reinterpret_cast<HMENU>(kBackground),nullptr,nullptr);
      for (HWND child : {g_details, g_hint, g_edit,
                         GetDlgItem(window, kRestart),
                         GetDlgItem(window, kClear),GetDlgItem(window, kBackground)})
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(g_font),
                     TRUE);
      SendMessageW(g_status, WM_SETFONT,
                   reinterpret_cast<WPARAM>(g_title_font), TRUE);
      SetTimer(window, kTimer, 1000, nullptr);
      return 0;
    }
    case WM_SIZE:
      Layout(LOWORD(lparam), HIWORD(lparam));
      return 0;
    case WM_COMMAND:
      if (LOWORD(wparam) == kBackground && HIWORD(wparam) == BN_CLICKED) {
        const auto error=RunBackgroundTests(false);
        SetWindowTextW(g_hint,error ? L"后台自测未能启动，请检查项目脚本路径。" : L"后台自测已启动，不操作此输入框；结果见项目 outputs/test-reports/INDEX.md。");
        return 0;
      }
      if (LOWORD(wparam) == kRestart && HIWORD(wparam) == BN_CLICKED) {
        if(AllRegistrationsCurrent())RestartSelf();else BeginRestrictedRepair();
        return 0;
      }
      if (LOWORD(wparam) == kClear && HIWORD(wparam) == BN_CLICKED) {
        SetWindowTextW(g_edit, L"");
        SetFocus(g_edit);
        return 0;
      }
      break;
    case WM_TIMER:
      if (wparam == kTimer) {
        if(g_repair_process && WaitForSingleObject(g_repair_process,0)==WAIT_OBJECT_0){
          DWORD result=1;GetExitCodeProcess(g_repair_process,&result);CloseHandle(g_repair_process);g_repair_process=nullptr;
          if(result==0 && AllRegistrationsCurrent())RestartSelf();
          else SetWindowTextW(g_hint,L"维修未完成或已取消；本窗口与草稿保留。请再次点击维修并确认 Windows 授权，不能以后台检查代替此窗口注册检查。");
        }
        RefreshStatus();
      }
      return 0;
    case WM_CTLCOLOREDIT: {
      HDC dc = reinterpret_cast<HDC>(wparam);
      SetTextColor(dc, RGB(238, 238, 238));
      SetBkColor(dc, RGB(36, 36, 36));
      return reinterpret_cast<LRESULT>(g_dark_brush);
    }
    case WM_DESTROY:
      if(g_repair_process){CloseHandle(g_repair_process);g_repair_process=nullptr;}
      KillTimer(window, kTimer);
      DeleteObject(g_dark_brush);
      DeleteObject(g_font);
      DeleteObject(g_title_font);
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(window, msg, wparam, lparam);
}
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command_line,
                    int show) {
  if(command_line && wcsstr(command_line,L"--check-draft-preservation")){
    // Isolated owned EDIT only: no TSF activation, OS input or child launch.
    g_check_restart=true;
    WNDCLASSW cls{};cls.lpfnWndProc=WindowProc;cls.hInstance=instance;cls.lpszClassName=kClass;RegisterClassW(&cls);
    const auto window=CreateWindowExW(WS_EX_NOACTIVATE,kClass,L"隔离草稿检查",WS_OVERLAPPEDWINDOW,-32000,-32000,900,600,nullptr,nullptr,instance,nullptr);
    const std::wstring draft=L"隔离草稿 fixture";SetWindowTextW(g_edit,draft.c_str());
    RestartSelf();wchar_t text[128]={};GetWindowTextW(g_edit,text,_countof(text));
    const bool correct=window && IsWindow(window) && std::wstring(text)==draft &&
        g_checked_restart_command.find(L"--relaunch")!=std::wstring::npos && !g_repair_process;
    WriteDiagnostic(CheckDirectory()+L"ZhongriTestBench-draft-preservation.txt",
      L"pass="+std::to_wstring(correct)+L"\r\nPolicy=Production new-window success branch, child creation replaced by isolated recorder; original owned window and fixture draft survive; no user windows, TSF activation, keystrokes, elevation or new process.\r\n",CREATE_ALWAYS);
    if(window)DestroyWindow(window);return correct?0:2;
  }
  if(command_line && wcsstr(command_line,L"--check-repair-plan")){
    RepairPlan plan;const bool correct=ReadRepairPlan(plan);
    WriteDiagnostic(CheckDirectory()+L"ZhongriTestBench-repair-plan.txt",
      L"valid="+std::to_wstring(correct)+L"\r\ncommand="+plan.command+
      L"\r\nPolicy=Explicit user repair action; normal Windows UAC via restricted helper; no registry writes, elevation, draft export or process launch in this check.\r\n",CREATE_ALWAYS);
    return correct?0:2;
  }
  if(command_line && wcsstr(command_line,L"--check-background-environment"))return int(RunBackgroundTests(true,true));
  if(command_line && wcsstr(command_line,L"--self-test"))return int(RunBackgroundTests(true));
  g_started_from_relaunch =
      command_line && wcsstr(command_line, L"--relaunch");
  if(command_line && wcsstr(command_line,L"--check-")) {
    const wchar_t* argument=wcsstr(command_line,L"--report-directory \"");
    if(argument){argument+=wcslen(L"--report-directory \"");const wchar_t* end=wcschr(argument,L'\"');
      if(end)g_check_directory.assign(argument,end);}
    if(!g_check_directory.empty()) {
      if(GetFileAttributesW(g_check_directory.c_str())==INVALID_FILE_ATTRIBUTES)return 3;
      WriteDiagnostic(g_check_directory+L"\\bench-process.txt",std::to_wstring(GetCurrentProcessId()),CREATE_NEW);
      // Allow the managed launcher to register this self-terminating owned PID.
      Sleep(1000);
    }
  }
  g_startup_registry=RegistrySnapshot();
  const bool bound = BindRegisteredComponent();
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  WNDCLASSW wc{};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = instance;
  wc.lpszClassName = kClass;
  wc.hCursor = LoadCursor(nullptr, IDC_IBEAM);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassW(&wc);
  if(command_line && wcsstr(command_line,L"--check-gui-startup")) {
    g_diagnostic_mode=true;
    BenchTextServices textServices;const HRESULT bootstrap=textServices.Start();g_tsf_bootstrap_result=bootstrap;
    HWND window=CreateWindowExW(WS_EX_NOACTIVATE,kClass,L"隔离启动检查",WS_OVERLAPPEDWINDOW,
                                 -32000,-32000,900,600,nullptr,nullptr,instance,nullptr);
    // Follow ordinary GUI startup, including the same profile activation
    // routine. Never show/foreground the window or send any input text/key.
    SetFocus(g_edit);
    const bool activated=ActivateZhongri();
    const auto deadline=GetTickCount64()+1500;
    while(GetTickCount64()<deadline){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);}
    RefreshStatus();wchar_t status[512]={};GetWindowTextW(g_status,status,_countof(status));
    const auto registered=RegisteredDll(),loaded=ProcessModuleWithPrefix(L"zhongrix64");
    const bool correct=bootstrap==S_OK && activated && window && AllLoadedComponentsMatch(registered) &&
        std::wstring(status).rfind(L"版本检查通过",0)==0;
    SaveStartupDiagnostic();
    const auto path=CheckDirectory()+L"ZhongriTestBench-gui-startup-check.txt";
    HANDLE report=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(report!=INVALID_HANDLE_VALUE){const std::wstring text=L"activated="+std::to_wstring(activated)+L"\r\nbootstrap="+std::to_wstring(static_cast<unsigned long>(bootstrap))+L"\r\nregistered="+registered+L"\r\nloaded="+loaded+L"\r\nstatus="+status+L"\r\npass="+std::to_wstring(correct);const wchar_t bom=0xfeff;DWORD written;WriteFile(report,&bom,2,&written,nullptr);WriteFile(report,text.data(),DWORD(text.size()*2),&written,nullptr);CloseHandle(report);}
    if(window)DestroyWindow(window);textServices.Reset();CoUninitialize();ReleaseRegisteredComponent();return correct?0:2;
  }
  // Noninteractive acceptance check: real COM activation, but no window,
  // keyboard injection, mouse operation or input-language activation.
  if (command_line && (wcsstr(command_line, L"--check-component") ||
                       wcsstr(command_line,L"--check-tsf-activation"))) {
    const bool tsf_check=wcsstr(command_line,L"--check-tsf-activation")!=nullptr;
    CLSID clsid{};
    CLSIDFromString(L"{760B5BB8-680D-4D4C-920A-D32940AD6144}", &clsid);
    IUnknown* service = nullptr;
    const HRESULT hr = tsf_check ? S_OK : CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_IUnknown, reinterpret_cast<void**>(&service));
    ITfThreadMgr* manager=nullptr;ITfDocumentMgr* document=nullptr;ITfContext* context=nullptr;
    HRESULT activation=S_OK;TfClientId client=0;
    if(tsf_check){
      activation=CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,
                                  IID_ITfThreadMgr,reinterpret_cast<void**>(&manager));
      if(SUCCEEDED(activation))activation=manager->Activate(&client);
      ITfInputProcessorProfileMgr* profiles=nullptr;
      if(SUCCEEDED(activation))activation=CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_ITfInputProcessorProfileMgr,reinterpret_cast<void**>(&profiles));
      GUID profile{};CLSIDFromString(L"{9DFBE6A5-7503-462F-A056-7F262CBDE722}",&profile);
      if(SUCCEEDED(activation))activation=profiles->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,0x0804,clsid,profile,nullptr,TF_IPPMF_FORPROCESS);
      if(profiles)profiles->Release();
      if(SUCCEEDED(activation))activation=manager->CreateDocumentMgr(&document);
      TfEditCookie cookie=0;
      if(SUCCEEDED(activation))activation=document->CreateContext(client,0,nullptr,&context,&cookie);
      if(SUCCEEDED(activation))activation=document->Push(context);
      if(SUCCEEDED(activation))activation=manager->SetFocus(document);
      // Only this process's TSF document; no keyboard, mouse, host text or
      // foreground-window operation. Pump activation callbacks, not keys.
      const auto deadline=GetTickCount64()+500;
      while(GetTickCount64()<deadline){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);}
    }
    const std::wstring loaded = ProcessModuleWithPrefix(L"zhongrix64");
    const bool component_correct = bound && SUCCEEDED(hr) && (tsf_check || service) && activation==S_OK &&
        Lower(loaded) == Lower(RegisteredDll()) && AllLoadedComponentsMatch(RegisteredDll());
    HWND check_window = CreateWindowExW(
        WS_EX_NOACTIVATE, kClass, L"中日输入法测试台 · 隔离检查", WS_OVERLAPPEDWINDOW,
        -32000, -32000, 900, 600, nullptr, nullptr, instance, nullptr);
    RefreshStatus();
    wchar_t status[512] = {};
    GetWindowTextW(g_status, status, _countof(status));
    const bool button_enabled = check_window &&
        IsWindowEnabled(GetDlgItem(check_window, kRestart));
    const bool correct = component_correct && button_enabled &&
                         std::wstring(status).rfind(L"版本检查通过", 0) == 0;
    wchar_t temp[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, temp)) {
      const std::wstring path = std::wstring(temp) + (tsf_check?L"ZhongriTestBench-tsf-check.txt":L"ZhongriTestBench-component-check.txt");
      HANDLE report = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                  nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (report != INVALID_HANDLE_VALUE) {
        const std::wstring text = L"bound=" + std::to_wstring(bound) +
            L" error=" + std::to_wstring(g_component_error) + L" hr=" +
            std::to_wstring(static_cast<unsigned long>(hr)) +
            L"\r\nregistered=" + RegisteredDll() + L"\r\nloaded=" + loaded +
            L"\r\ntsf_activation="+std::to_wstring(static_cast<unsigned long>(activation))+
            L"\r\nbutton_enabled=" + std::to_wstring(button_enabled) +
            L"\r\nstatus=" + status + L"\r\npass=" + std::to_wstring(correct);
        DWORD written = 0;
        const wchar_t bom = 0xfeff;
        WriteFile(report, &bom, sizeof(bom), &written, nullptr);
        WriteFile(report, text.data(), static_cast<DWORD>(text.size() * sizeof(wchar_t)),
                  &written, nullptr);
        CloseHandle(report);
      }
    }
    if (check_window) DestroyWindow(check_window);
    if(manager)manager->SetFocus(nullptr);
    if(document)document->Pop(TF_POPF_ALL);
    if(context)context->Release();if(document)document->Release();
    if(manager){manager->Deactivate();manager->Release();}
    if (service) service->Release();
    CoUninitialize();
    ReleaseRegisteredComponent();
    return correct ? 0 : 2;
  }
  BenchTextServices textServices;
  const HRESULT bootstrap=textServices.Start();
  g_tsf_bootstrap_result=bootstrap;
  HWND window = CreateWindowExW(
      0, kClass, L"中日输入法测试台 · 真实输入与版本检查",
      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 600, nullptr,
      nullptr, instance, nullptr);
  if (!window) {
    textServices.Reset();
    CoUninitialize();
    ReleaseRegisteredComponent();
    return 1;
  }
  const bool no_activate = command_line && wcsstr(command_line, L"--no-activate");
  ShowWindow(window, no_activate ? SW_SHOWNOACTIVATE : show);
  UpdateWindow(window);
  if (!no_activate) SetFocus(g_edit);
  ActivateZhongri();
  RefreshStatus();
  SaveStartupDiagnostic();
  if(bootstrap!=S_OK)SetWindowTextW(g_status,L"不可验收：测试台的 TSF 启动失败，不能据后台自测判定本窗口正常。");
  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  textServices.Reset();CoUninitialize();
  ReleaseRegisteredComponent();
  return 0;
}
