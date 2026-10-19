// WeaselServer.cpp : main source file for ZhongriServer.exe
//
//	WTL MessageLoop 封装了消息循环. 实现了 getmessage/dispatchmessage....

#include "stdafx.h"
#include "resource.h"
#include "WeaselService.h"
#include <WeaselIPC.h>
#include <WeaselUI.h>
#include <RimeWithWeasel.h>
#include <WeaselUtility.h>
#include <winsparkle.h>
#include <functional>
#include <ShellScalingApi.h>
#include <WinUser.h>
#include <memory>
#include <atlstr.h>
#include <ZhongriSettingsMenu.h>
#pragma comment(lib, "Shcore.lib")
CAppModule _Module;

int WINAPI _tWinMain(HINSTANCE hInstance,
                     HINSTANCE /*hPrevInstance*/,
                     LPTSTR lpstrCmdLine,
                     int nCmdShow) {
  // Read-only acceptance check: no COM, named pipe, tray icon or activation.
  if (!wcscmp(L"/check-settings-menu", lpstrCmdLine)) {
    HMENU menu = ::LoadMenuW(hInstance, MAKEINTRESOURCEW(IDR_MENU_POPUP));
    if (!menu) return 2;
    HMENU popup = ::GetSubMenu(menu, 0);
    WeaselTrayIcon::CustomizeSettingsMenu(popup);
    wchar_t title[256] = {};
    ::GetMenuStringW(popup, ID_WEASELTRAY_SETTINGS, title, _countof(title), MF_BYCOMMAND);
    const bool valid = wcsstr(title, L"中日输入法设置（新版）") &&
        ::GetMenuState(popup, ID_ZHONGRI_LEARNING_MODE, MF_BYCOMMAND) != UINT(-1) &&
        ::GetMenuItemCount(popup)==6 &&
        ::GetMenuState(popup,ID_WEASELTRAY_DICT_MANAGEMENT,MF_BYCOMMAND)==UINT(-1) &&
        std::filesystem::exists(WeaselServerApp::settings_path());
    ::DestroyMenu(menu);
    return valid ? 0 : 3;
  }
  // A shutdown helper must not initialize TSF/COM or open the named pipe:
  // either can wait on the very backend it is trying to close.
  if (!wcscmp(L"/q", lpstrCmdLine) || !wcscmp(L"/quit", lpstrCmdLine)) {
    if (HWND server_window = ::FindWindow(WEASEL_IPC_WINDOW, nullptr))
      ::PostMessage(server_window, WM_CLOSE, 0, 0);
    return 0;
  }
  LANGID langId = get_language_id();
  SetThreadUILanguage(langId);
  SetThreadLocale(langId);

  if (!IsWindowsBlueOrLaterEx()) {
    CString info, cap;
    info.LoadStringW(IDS_STR_SYSTEM_VERSION_WARNING);
    cap.LoadStringW(IDS_STR_SYSTEM_VERSION_WARNING_CAPTION);
    MessageBoxExW(NULL, info, cap, MB_ICONERROR, langId);
    return 0;
  }
  SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

  // 防止服务进程开启输入法
  ImmDisableIME(-1);

  WCHAR user_name[20] = {0};
  DWORD size = _countof(user_name);
  GetUserName(user_name, &size);
  if (!_wcsicmp(user_name, L"SYSTEM")) {
    return 1;
  }

  HRESULT hRes = ::CoInitialize(NULL);
  // If you are running on NT 4.0 or higher you can use the following call
  // instead to make the EXE free threaded. This means that calls come in on a
  // random RPC thread.
  // HRESULT hRes = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
  ATLASSERT(SUCCEEDED(hRes));

  // this resolves ATL window thunking problem when Microsoft Layer for Unicode
  // (MSLU) is used
  ::DefWindowProc(NULL, 0, 0, 0L);

  AtlInitCommonControls(
      ICC_BAR_CLASSES);  // add flags to support other controls

  hRes = _Module.Init(NULL, hInstance);
  ATLASSERT(SUCCEEDED(hRes));

  if (!wcscmp(L"/userdir", lpstrCmdLine)) {
    CreateDirectory(WeaselUserDataPath().c_str(), NULL);
    WeaselServerApp::explore(WeaselUserDataPath());
    return 0;
  }
  if (!wcscmp(L"/weaseldir", lpstrCmdLine)) {
    WeaselServerApp::explore(WeaselServerApp::install_dir());
    return 0;
  }
  if (!wcscmp(L"/ascii", lpstrCmdLine) || !wcscmp(L"/nascii", lpstrCmdLine)) {
    weasel::Client client;
    bool ascii = !wcscmp(L"/ascii", lpstrCmdLine);
    // A client can keep an orphaned named-pipe instance alive after the
    // server exits. Connecting to that busy pipe would wait forever and
    // prevent both /q and a fresh backend from starting. Only connect when
    // the server's own IPC window still exists.
    if (::FindWindow(WEASEL_IPC_WINDOW, nullptr) && client.Connect())
    {
      if (ascii)
        client.TrayCommand(ID_WEASELTRAY_ENABLE_ASCII);
      else
        client.TrayCommand(ID_WEASELTRAY_DISABLE_ASCII);
    }
    return 0;
  }

  // command line option /q stops the running server
  bool quit = !wcscmp(L"/q", lpstrCmdLine) || !wcscmp(L"/quit", lpstrCmdLine);
  // restart if already running
  {
    weasel::Client client;
    if (::FindWindow(WEASEL_IPC_WINDOW, nullptr) && client.Connect())
    {
      client.ShutdownServer();
      if (quit)
        return 0;
      int retry = 0;
      while (client.Connect() && retry < 10) {
        client.ShutdownServer();
        retry++;
        Sleep(50);
      }
      if (retry >= 10)
        return 0;
    } else if (quit)
      return 0;
  }

  bool check_updates = !wcscmp(L"/update", lpstrCmdLine);
  if (check_updates) {
    WeaselServerApp::check_update();
  }

  CreateDirectory(WeaselUserDataPath().c_str(), NULL);

  int nRet = 0;
  try {
    WeaselServerApp app;
    RegisterApplicationRestart(NULL, 0);
    nRet = app.Run();
  } catch (...) {
    // bad luck...
    nRet = -1;
  }

  _Module.Term();
  ::CoUninitialize();

  return nRet;
}
