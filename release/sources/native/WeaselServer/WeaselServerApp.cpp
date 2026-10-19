#include "stdafx.h"
#include "WeaselServerApp.h"
#include <filesystem>
#include <ZhongriSettingsMenu.h>
namespace { std::function<void()> refreshFeatureTray, pollLongPress; constexpr UINT refreshFeatureCommand = 65001; }

WeaselServerApp::WeaselServerApp()
    : m_handler(std::make_unique<RimeWithWeaselHandler>(&m_ui)),
      tray_icon(m_ui) {
  // m_handler.reset(new RimeWithWeaselHandler(&m_ui));
  m_server.SetRequestHandler(m_handler.get());
  SetupMenuHandlers();
}

WeaselServerApp::~WeaselServerApp() {}

int WeaselServerApp::Run() {
  if (!m_server.Start())
    return -1;

  // win_sparkle_set_appcast_url("http://localhost:8000/weasel/update/appcast.xml");
  win_sparkle_set_registry_path("Software\\ZhongriInputMethod\\Updates");
  if (GetThreadUILanguage() ==
      MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL))
    win_sparkle_set_lang("zh-TW");
  else if (GetThreadUILanguage() ==
           MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED))
    win_sparkle_set_lang("zh-CN");
  else
    win_sparkle_set_lang("en");
  // Independent product: upstream updater disabled.
  m_ui.Create(m_server.GetHWnd());

  m_handler->Initialize();
  const auto refreshTray = [this]() {
    bool learning = false, ascii_punct = false;
    m_handler->GetTrayFeatures(learning, ascii_punct);
    tray_icon.Refresh(learning, ascii_punct);
  };
  // Some legacy tray notifications originate on a helper thread. Marshal
  // feature-state/Rime reads onto the server's owning message thread.
  m_handler->OnUpdateUI([this]() { PostMessage(m_server.GetHWnd(), WM_COMMAND, refreshFeatureCommand, 0); });
  m_handler->OnSentenceTranslationReady([this]() {
    ::PostMessage(m_server.GetHWnd(), WM_COMMAND,
                  ID_WEASEL_SENTENCE_TRANSLATION_READY, 0);
  });

  tray_icon.Create(m_server.GetHWnd());
  refreshTray();
  refreshFeatureTray = refreshTray;
  m_server.AddMenuHandler(refreshFeatureCommand, [] { if (refreshFeatureTray) refreshFeatureTray(); return true; });
  const auto trayTimer = SetTimer(nullptr, 0, 300, [](HWND, UINT, UINT_PTR, DWORD) {
    if (refreshFeatureTray) refreshFeatureTray();
  });
  pollLongPress = [this]() {
    // Request handlers can synchronously call our window from a pipe thread.
    // Never block the window thread waiting for their mutex: retry next tick.
    m_server.TryWithRequestLock([this]() { m_handler->PollLongPress(); });
  };
  const auto holdTimer = SetTimer(nullptr, 0, 25, [](HWND, UINT, UINT_PTR, DWORD) {
    if(pollLongPress)pollLongPress();
  });

  int ret = m_server.Run();
  if (trayTimer) KillTimer(nullptr, trayTimer);
  if (holdTimer) KillTimer(nullptr, holdTimer);
  pollLongPress = {};
  refreshFeatureTray = {};

  m_handler->Finalize();
  m_ui.Destroy();
  tray_icon.RemoveIcon();
  win_sparkle_cleanup();

  return ret;
}

void WeaselServerApp::SetupMenuHandlers() {
  std::filesystem::path dir = install_dir();
  m_server.AddMenuHandler(ID_WEASELTRAY_QUIT,
                          [this] { return m_server.Stop() == 0; });
  m_server.AddMenuHandler(ID_ZHONGRI_LEARNING_MODE, [this] {
    const bool saved=ToggleJapaneseLearningMode();
    if (saved) m_handler->RefreshActiveUI();
    if (refreshFeatureTray) refreshFeatureTray();
    return saved;
  });
  m_server.AddMenuHandler(ID_WEASELTRAY_RERUN_SERVICE, [dir] {
    return (uintptr_t)ShellExecuteW(nullptr,L"open",(dir/L"start_service.bat").c_str(),nullptr,dir.c_str(),SW_HIDE)>32;
  });
  m_server.AddMenuHandler(ID_WEASEL_SENTENCE_TRANSLATION_READY, [this] {
    m_handler->RefreshActiveUI();
    return true;
  });
  m_server.AddMenuHandler(ID_WEASELTRAY_DEPLOY,
                          std::bind(execute, dir / L"ZhongriDeployer.exe",
                                    std::wstring(L"/deploy")));
  // Both settings entries must open the current WinUI panel.  Never fall back
  // to the retired WinForms panel or the deployer's legacy settings window.
  const auto open_settings = [dir] {
    const auto settings = settings_path();
    if (std::filesystem::exists(settings))
      return execute(settings, std::wstring());
    return false;
  };
  m_server.AddMenuHandler(ID_WEASELTRAY_SETTINGS, open_settings);
  m_server.AddMenuHandler(ID_WEASELTRAY_CNJP_SETTINGS, open_settings);
  m_server.AddMenuHandler(
      ID_WEASELTRAY_DICT_MANAGEMENT,
      std::bind(execute, dir / L"ZhongriDeployer.exe", std::wstring(L"/dict")));
  m_server.AddMenuHandler(
      ID_WEASELTRAY_SYNC,
      std::bind(execute, dir / L"ZhongriDeployer.exe", std::wstring(L"/sync")));
  m_server.AddMenuHandler(ID_WEASELTRAY_WIKI,
                          std::bind(open, L"https://rime.im/docs/"));
  m_server.AddMenuHandler(ID_WEASELTRAY_HOMEPAGE,
                          std::bind(open, L"https://rime.im/"));
  m_server.AddMenuHandler(ID_WEASELTRAY_FORUM,
                          std::bind(open, L"https://rime.im/discuss/"));
  // No upstream installer action in the independent product.
  m_server.AddMenuHandler(ID_WEASELTRAY_INSTALLDIR, std::bind(explore, dir));
  m_server.AddMenuHandler(ID_WEASELTRAY_USERCONFIG,
                          std::bind(explore, WeaselUserDataPath()));
  m_server.AddMenuHandler(ID_WEASELTRAY_LOGDIR,
                          std::bind(explore, WeaselLogPath()));
}
