#include "stdafx.h"
#include <resource.h>
#include <thread>
#include <shellapi.h>
#include "WeaselTSF.h"
#include "LanguageBar.h"
#include "CandidateList.h"
#include <WeaselUtility.h>
#include <ZhongriSettingsMenu.h>
#include <InputFeatureIcon.h>
#include <map>
static thread_local std::map<UINT_PTR, CLangBarItemButton*> featureButtons;

static const DWORD LANGBARITEMSINK_COOKIE = 0x42424242;

static void HMENU2ITfMenu(HMENU hMenu, ITfMenu* pTfMenu) {
  /* NOTE: Only limited functions are supported */
  int N = GetMenuItemCount(hMenu);
  for (int i = 0; i < N; i++) {
    MENUITEMINFO mii;
    mii.cbSize = sizeof(MENUITEMINFO);
    mii.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STRING | MIIM_STATE;
    mii.dwTypeData = NULL;
    if (GetMenuItemInfo(hMenu, i, TRUE, &mii)) {
      UINT id = mii.wID;
      if (mii.fType == MFT_SEPARATOR)
        pTfMenu->AddMenuItem(id, TF_LBMENUF_SEPARATOR, NULL, NULL, NULL, 0,
                             NULL);
      else if (mii.fType == MFT_STRING) {
        mii.dwTypeData = (LPWSTR)malloc(sizeof(WCHAR) * (mii.cch + 1));
        mii.cch++;
        if (GetMenuItemInfo(hMenu, i, TRUE, &mii))
          pTfMenu->AddMenuItem(id, (mii.fState&MFS_CHECKED)?TF_LBMENUF_CHECKED:0, NULL, NULL, mii.dwTypeData, mii.cch,
                               NULL);
        free(mii.dwTypeData);
      }
    }
  }
}

static LPCWSTR GetWeaselRegName() {
  LPCWSTR WEASEL_REG_NAME_;
  if (is_wow64())
    WEASEL_REG_NAME_ = L"Software\\WOW6432Node\\ZhongriInputMethod";
  else
    WEASEL_REG_NAME_ = L"Software\\ZhongriInputMethod";

  return WEASEL_REG_NAME_;
}

static bool open(const std::wstring& path) {
  return (uintptr_t)ShellExecuteW(NULL, L"open", path.c_str(), NULL, NULL,
                                  SW_SHOWNORMAL) > 32;
}

CLangBarItemButton::CLangBarItemButton(com_ptr<WeaselTSF> pTextService,
                                       REFGUID guid,
                                       weasel::UIStyle& style)
    : _status(0),
      _style(style),
      _current_schema_zhung_icon(),
      _current_schema_ascii_icon() {
  DllAddRef();

  _pLangBarItemSink = NULL;
  _cRef = 1;
  _pTextService = pTextService;
  _guid = guid;
  ascii_mode = false;
  _featureTimer = SetTimer(nullptr,0,300,[](HWND,UINT,UINT_PTR id,DWORD) {
    auto found=featureButtons.find(id);
    if(found!=featureButtons.end()) found->second->RefreshFeatureStatus();
  });
  if(_featureTimer) featureButtons[_featureTimer]=this;
}

CLangBarItemButton::~CLangBarItemButton() {
  if(_featureTimer) { KillTimer(nullptr,_featureTimer); featureButtons.erase(_featureTimer); }
  DllRelease();
}

STDAPI CLangBarItemButton::QueryInterface(REFIID riid, void** ppvObject) {
  if (ppvObject == NULL)
    return E_INVALIDARG;

  *ppvObject = NULL;
  if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfLangBarItem) ||
      IsEqualIID(riid, IID_ITfLangBarItemButton))
    *ppvObject = (ITfLangBarItemButton*)this;
  else if (IsEqualIID(riid, IID_ITfSource))
    *ppvObject = (ITfSource*)this;

  if (*ppvObject) {
    AddRef();
    return S_OK;
  }
  return E_NOINTERFACE;
}

STDAPI_(ULONG) CLangBarItemButton::AddRef() {
  return ++_cRef;
}

STDAPI_(ULONG) CLangBarItemButton::Release() {
  LONG cr = --_cRef;
  assert(_cRef >= 0);
  if (_cRef == 0)
    delete this;
  return cr;
}

STDAPI CLangBarItemButton::GetInfo(TF_LANGBARITEMINFO* pInfo) {
  pInfo->clsidService = c_clsidTextService;
  pInfo->guidItem = _guid;
  pInfo->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_BTN_MENU |
                   TF_LBI_STYLE_SHOWNINTRAY;
  pInfo->ulSort = 1;
  lstrcpyW(pInfo->szDescription, L"WeaselTSF Button");
  return S_OK;
}

STDAPI CLangBarItemButton::GetStatus(DWORD* pdwStatus) {
  *pdwStatus = _status;
  return S_OK;
}

STDAPI CLangBarItemButton::Show(BOOL fShow) {
  SetLangbarStatus(TF_LBI_STATUS_HIDDEN, fShow ? FALSE : TRUE);
  return S_OK;
}

static LANGID GetActiveProfileLangId() {
  CComPtr<ITfInputProcessorProfileMgr> pInputProcessorProfileMgr;
  HRESULT hr = pInputProcessorProfileMgr.CoCreateInstance(
      CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_ALL);
  if (FAILED(hr))
    return MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);

  TF_INPUTPROCESSORPROFILE profile;
  hr = pInputProcessorProfileMgr->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,
                                                   &profile);
  if (FAILED(hr))
    return MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);
  return profile.langid;
}

STDAPI CLangBarItemButton::GetTooltipString(BSTR* pbstrToolTip) {
  *pbstrToolTip=SysAllocString(InputFeatureTooltip(InputFeatureFlags(ascii_mode)).c_str());
  return *pbstrToolTip ? S_OK : E_OUTOFMEMORY;
#if 0
  LANGID langid = get_language_id();
  if (langid == TEXTSERVICE_LANGID_HANS) {
    *pbstrToolTip = SysAllocString(L"左键切换模式，右键打开菜单");
  } else if (langid == TEXTSERVICE_LANGID_HANT) {
    *pbstrToolTip = SysAllocString(L"左鍵切換模式，右鍵打開菜單");
  } else {
    *pbstrToolTip = SysAllocString(
        L"Left-click to switch modes\n\nRight-click for more options");
  }

  return (*pbstrToolTip == NULL) ? E_OUTOFMEMORY : S_OK;
#endif
}

STDAPI CLangBarItemButton::OnClick(TfLBIClick click,
                                   POINT pt,
                                   const RECT* prcArea) {
  if (click == TF_LBI_CLK_LEFT) {
    _pTextService->_HandleLangBarMenuSelect(
        ascii_mode ? ID_WEASELTRAY_DISABLE_ASCII : ID_WEASELTRAY_ENABLE_ASCII);
    ascii_mode = !ascii_mode;
    if (_pLangBarItemSink) {
      _pLangBarItemSink->OnUpdate(TF_LBI_STATUS | TF_LBI_ICON);
    }
  } else if (click == TF_LBI_CLK_RIGHT) {
    /* Open menu */
    HWND hwnd = _pTextService->_GetFocusedContextWindow();
    if (hwnd != NULL) {
      LANGID langid = get_language_id();
      HMENU menu;
      if (langid == TEXTSERVICE_LANGID_HANS) {
        menu = LoadMenuW(g_hInst, MAKEINTRESOURCE(IDR_MENU_POPUP_HANS));
      } else if (langid == TEXTSERVICE_LANGID_HANT) {
        menu = LoadMenuW(g_hInst, MAKEINTRESOURCE(IDR_MENU_POPUP_HANT));
      } else {
        menu = LoadMenuW(g_hInst, MAKEINTRESOURCE(IDR_MENU_POPUP));
      }
      HMENU popupMenu = GetSubMenu(menu, 0);
      ConfigureZhongriSettingsMenu(popupMenu);
      UINT wID = TrackPopupMenuEx(
          popupMenu, TPM_NONOTIFY | TPM_RETURNCMD | TPM_HORPOSANIMATION, pt.x,
          pt.y, hwnd, NULL);
      DestroyMenu(menu);
      _pTextService->_HandleLangBarMenuSelect(wID);
    }
  }
  return S_OK;
}

STDAPI CLangBarItemButton::InitMenu(ITfMenu* pMenu) {
  HMENU menu = LoadMenuW(g_hInst, MAKEINTRESOURCE(IDR_MENU_POPUP));
  HMENU popupMenu = GetSubMenu(menu, 0);
  ConfigureZhongriSettingsMenu(popupMenu);
  HMENU2ITfMenu(popupMenu, pMenu);
  DestroyMenu(menu);
  return S_OK;
}

STDAPI CLangBarItemButton::OnMenuSelect(UINT wID) {
  _pTextService->_HandleLangBarMenuSelect(wID);
  return S_OK;
}

STDAPI CLangBarItemButton::GetIcon(HICON* phIcon) {
  *phIcon=MakeInputFeatureIcon(InputFeatureFlags(ascii_mode));
  return *phIcon ? S_OK : E_FAIL;
#if 0
  if (ascii_mode) {
    if (_style.current_ascii_icon.empty())
      *phIcon = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_EN), IMAGE_ICON,
                                  GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    else
      *phIcon =
          (HICON)LoadImageW(NULL, _style.current_ascii_icon.c_str(), IMAGE_ICON,
                            GetSystemMetrics(SM_CXSMICON),
                            GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE);
  } else {
    if (_style.current_zhung_icon.empty())
      *phIcon = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_ZH), IMAGE_ICON,
                                  GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    else
      *phIcon =
          (HICON)LoadImageW(NULL, _style.current_zhung_icon.c_str(), IMAGE_ICON,
                            GetSystemMetrics(SM_CXSMICON),
                            GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE);
  }
  return (*phIcon == NULL) ? E_FAIL : S_OK;
#endif
}

void CLangBarItemButton::RefreshFeatureStatus() {
  const DWORD flags=InputFeatureFlags(ascii_mode);
  if(flags==_featureFlags) return;
  _featureFlags=flags;
  if(_pLangBarItemSink) _pLangBarItemSink->OnUpdate(TF_LBI_ICON | TF_LBI_TOOLTIP);
}

STDAPI CLangBarItemButton::GetText(BSTR* pbstrText) {
  *pbstrText = SysAllocString(L"WeaselTSF Button");
  return (*pbstrText == NULL) ? E_OUTOFMEMORY : S_OK;
}

STDAPI CLangBarItemButton::AdviseSink(REFIID riid,
                                      IUnknown* punk,
                                      DWORD* pdwCookie) {
  if (!IsEqualIID(riid, IID_ITfLangBarItemSink))
    return CONNECT_E_CANNOTCONNECT;
  if (_pLangBarItemSink != NULL)
    return CONNECT_E_ADVISELIMIT;

  if (punk->QueryInterface(IID_ITfLangBarItemSink,
                           (LPVOID*)&_pLangBarItemSink) != S_OK) {
    _pLangBarItemSink = NULL;
    return E_NOINTERFACE;
  }
  *pdwCookie = LANGBARITEMSINK_COOKIE;
  return S_OK;
}

STDAPI CLangBarItemButton::UnadviseSink(DWORD dwCookie) {
  if (dwCookie != LANGBARITEMSINK_COOKIE || _pLangBarItemSink == NULL)
    return CONNECT_E_NOCONNECTION;
  _pLangBarItemSink = NULL;
  return S_OK;
}

void CLangBarItemButton::UpdateWeaselStatus(weasel::Status stat) {
  if (stat.ascii_mode != ascii_mode) {
    ascii_mode = stat.ascii_mode;
  }
  if (_current_schema_zhung_icon != _style.current_zhung_icon) {
    _current_schema_zhung_icon = _style.current_zhung_icon;
  }
  if (_current_schema_ascii_icon != _style.current_ascii_icon) {
    _current_schema_ascii_icon = _style.current_ascii_icon;
  }
  if (_pLangBarItemSink) {
    _pLangBarItemSink->OnUpdate(TF_LBI_STATUS | TF_LBI_ICON);
  }
}

void CLangBarItemButton::SetLangbarStatus(DWORD dwStatus, BOOL fSet) {
  BOOL isChange = FALSE;

  if (fSet) {
    if (!(_status & dwStatus)) {
      _status |= dwStatus;
      isChange = TRUE;
    }
  } else {
    if (_status & dwStatus) {
      _status &= ~dwStatus;
      isChange = TRUE;
    }
  }

  if (isChange && _pLangBarItemSink) {
    _pLangBarItemSink->OnUpdate(TF_LBI_STATUS | TF_LBI_ICON);
  }

  return;
}

std::wstring WeaselTSF::_GetRootDir() {
  std::wstring dir{};
  RegGetStringValue(HKEY_LOCAL_MACHINE, GetWeaselRegName(), L"ZhongriRoot", dir);
  return dir;
}

void WeaselTSF::_HandleLangBarMenuSelect(UINT wID) {
  std::wstring dir{};
  switch (wID) {
    case ID_ZHONGRI_LEARNING_MODE:
      if(ToggleJapaneseLearningMode()) {
        if(_EnsureServerConnected()) m_client.ProcessKeyEvent(weasel::KeyEvent(0,0));
        if(_pLangBarButton) _pLangBarButton->RefreshFeatureStatus();
      }
      break;
    case ID_WEASELTRAY_SETTINGS:
    case ID_WEASELTRAY_CNJP_SETTINGS: {
      // Resolve relative to this loaded product DLL, avoiding stale registry
      // roots or an unavailable backend pipe. This opens only the WinUI panel.
      wchar_t module_path[32768] = {};
      GetModuleFileNameW(g_hInst, module_path, _countof(module_path));
      const auto settings = fs::path(module_path).parent_path() / L"tools" /
                            L"Settings-WinUI" / L"ZhongriSettingsV2.exe";
      if (fs::exists(settings)) open(settings.wstring());
      else MessageBoxW(NULL, L"新版设置文件缺失，请修复安装。", L"中日输入法设置", MB_ICONERROR | MB_OK);
      break;
    }
    case ID_WEASELTRAY_RERUN_SERVICE:
    case ID_WEASELTRAY_INSTALLDIR:
      if (RegGetStringValue(HKEY_LOCAL_MACHINE, GetWeaselRegName(),
                            L"ZhongriRoot", dir) == ERROR_SUCCESS) {
        if (wID == ID_WEASELTRAY_RERUN_SERVICE) {
          std::thread th([dir]() {
            ShellExecuteW(NULL, L"open", (dir + L"\\start_service.bat").c_str(),
                          NULL, dir.c_str(), SW_HIDE);
          });
          th.detach();
        } else
          open(dir);
      }
      break;
    case ID_WEASELTRAY_USERCONFIG:
      if (FAILED(RegGetStringValue(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod",
                                   L"ZhongriUserDir", dir)) ||
          dir.empty()) {
        WCHAR _path[MAX_PATH] = {0};
        ExpandEnvironmentStringsW(L"%AppData%\\ZhongriInputMethod", _path, _countof(_path));
        dir = std::wstring(_path);
      }
      if (!dir.empty() && fs::exists(dir))
        open(dir);
      else
        MessageBoxW(NULL, (L"Not found: " + dir).c_str(), L"ZhongriUserDir",
                    MB_ICONERROR | MB_OK);
      break;
    case ID_WEASELTRAY_LOGDIR:
      open(WeaselLogPath().wstring());
      break;
    case ID_WEASELTRAY_WIKI:
      open(L"https://rime.im/docs/");
      break;
    case ID_WEASELTRAY_FORUM:
      open(L"https://rime.im/discuss/");
      break;
    default:
      m_client.TrayCommand(wID);
      break;
  }
}

HWND WeaselTSF::_GetFocusedContextWindow() {
  HWND hwnd = NULL;
  ITfDocumentMgr* pDocMgr;
  if (_pThreadMgr->GetFocus(&pDocMgr) == S_OK && pDocMgr != NULL) {
    ITfContext* pContext;
    if (pDocMgr->GetTop(&pContext) == S_OK && pContext != NULL) {
      ITfContextView* pContextView;
      if (pContext->GetActiveView(&pContextView) == S_OK &&
          pContextView != NULL) {
        pContextView->GetWnd(&hwnd);
        pContextView->Release();
      }
      pContext->Release();
    }
    pDocMgr->Release();
  }

  if (hwnd == NULL) {
    HWND hwndForeground = GetForegroundWindow();
    if (GetWindowThreadProcessId(hwndForeground, NULL) == GetCurrentThreadId())
      hwnd = hwndForeground;
  }

  return hwnd;
}

BOOL WeaselTSF::_InitLanguageBar() {
  com_ptr<ITfLangBarItemMgr> pLangBarItemMgr;
  BOOL fRet = FALSE;

  if (_pThreadMgr->QueryInterface(&pLangBarItemMgr) != S_OK)
    return FALSE;

  if ((_pLangBarButton = new CLangBarItemButton(this, GUID_LBI_INPUTMODE,
                                                _cand->style())) == NULL)
    return FALSE;

  if (pLangBarItemMgr->AddItem(_pLangBarButton) != S_OK) {
    _pLangBarButton = NULL;
    return FALSE;
  }

  _pLangBarButton->Show(TRUE);
  fRet = TRUE;

  return fRet;
}

void WeaselTSF::_UninitLanguageBar() {
  com_ptr<ITfLangBarItemMgr> pLangBarItemMgr;

  if (_pLangBarButton == NULL)
    return;

  if (_pThreadMgr->QueryInterface(&pLangBarItemMgr) == S_OK) {
    pLangBarItemMgr->RemoveItem(_pLangBarButton);
  }

  _pLangBarButton = NULL;
}

void WeaselTSF::_UpdateLanguageBar(weasel::Status stat) {
  if (!_pLangBarButton)
    return;
  DWORD flags;
  _GetCompartmentDWORD(flags, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
  if (stat.ascii_mode)
    flags &= (~TF_CONVERSIONMODE_NATIVE);
  else
    flags |= TF_CONVERSIONMODE_NATIVE;
  if (stat.full_shape)
    flags |= TF_CONVERSIONMODE_FULLSHAPE;
  else
    flags &= (~TF_CONVERSIONMODE_FULLSHAPE);
  _SetCompartmentDWORD(flags, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);

  _pLangBarButton->UpdateWeaselStatus(stat);
}

void WeaselTSF::_ShowLanguageBar(BOOL show) {
  if (!_pLangBarButton)
    return;
  _pLangBarButton->Show(show);
}

void WeaselTSF::_EnableLanguageBar(BOOL enable) {
  if (!_pLangBarButton)
    return;
  _pLangBarButton->SetLangbarStatus(TF_LBI_STATUS_DISABLED, !enable);
}
