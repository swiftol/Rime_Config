#pragma once
#include <windows.h>
#include <resource.h>
constexpr UINT ID_ZHONGRI_LEARNING_MODE = 40019;
inline bool JapaneseLearningModeEnabled() {
  DWORD value=0, bytes=sizeof(value);
  RegGetValueW(HKEY_CURRENT_USER,L"Software\\ZhongriInputMethod",L"Option_japanese_learning_mode",RRF_RT_REG_DWORD,nullptr,&value,&bytes);
  return value!=0;
}
inline bool ToggleJapaneseLearningMode() {
  HKEY key=nullptr;
  if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\ZhongriInputMethod",0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS) return false;
  DWORD enabled=0, generation=0, bytes=sizeof(DWORD);
  RegQueryValueExW(key,L"Option_japanese_learning_mode",nullptr,nullptr,reinterpret_cast<BYTE*>(&enabled),&bytes);
  enabled=!enabled;
  const LONG saved=RegSetValueExW(key,L"Option_japanese_learning_mode",0,REG_DWORD,reinterpret_cast<const BYTE*>(&enabled),sizeof(enabled));
  bytes=sizeof(DWORD);
  RegQueryValueExW(key,L"InputSettingsGeneration",nullptr,nullptr,reinterpret_cast<BYTE*>(&generation),&bytes);
  ++generation;
  const LONG refreshed=RegSetValueExW(key,L"InputSettingsGeneration",0,REG_DWORD,reinterpret_cast<const BYTE*>(&generation),sizeof(generation));
  RegCloseKey(key);
  return saved==ERROR_SUCCESS && refreshed==ERROR_SUCCESS;
}

// Shared by the backend tray icon and Windows' TSF language-bar menu.
inline void ConfigureZhongriSettingsMenu(HMENU menu) {
  // Remove menu entries only, never user dictionaries or installed files.
  while(::GetMenuItemCount(menu)>0) ::DeleteMenu(menu,0,MF_BYPOSITION);
  ::AppendMenuW(menu,MF_STRING,ID_WEASELTRAY_SETTINGS,L"中日输入法设置（新版） (&S)");
  ::AppendMenuW(menu,MF_STRING|(JapaneseLearningModeEnabled()?MF_CHECKED:MF_UNCHECKED),ID_ZHONGRI_LEARNING_MODE,L"学习模式 (&L)");
  ::AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
  ::AppendMenuW(menu,MF_STRING,ID_WEASELTRAY_DEPLOY,L"重新部署 (&R)");
  ::AppendMenuW(menu,MF_STRING,ID_WEASELTRAY_RERUN_SERVICE,L"重启输入法服务 (&E)");
  ::AppendMenuW(menu,MF_STRING,ID_WEASELTRAY_QUIT,L"退出输入法服务 (&Q)");
}
