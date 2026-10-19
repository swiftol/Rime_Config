#pragma once
#include <WeaselUI.h>
#include <WeaselIPC.h>
#include "SystemTraySDK.h"

#define WM_WEASEL_TRAY_NOTIFY (WEASEL_IPC_LAST_COMMAND + 100)

class WeaselTrayIcon : public CSystemTray {
 public:
  enum WeaselTrayMode {
    INITIAL,
    ZHUNG,
    ASCII,
    DISABLED,
  };

  WeaselTrayIcon(weasel::UI& ui);

  BOOL Create(HWND hTargetWnd);
  void Refresh(bool learning = false, bool ascii_punct = false);
  ~WeaselTrayIcon();
  static void CustomizeSettingsMenu(HMENU hMenu);

 protected:
  virtual void CustomizeMenu(HMENU hMenu);

  weasel::UIStyle& m_style;
  weasel::Status& m_status;
  WeaselTrayMode m_mode;
  std::wstring m_schema_zhung_icon;
  std::wstring m_schema_ascii_icon;
  bool m_disabled;
  HICON m_feature_icon = nullptr;
  int m_feature_state = -1;
  std::wstring m_feature_tooltip;
};
