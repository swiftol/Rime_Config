#include "stdafx.h"
#include "WeaselTrayIcon.h"
#include <atlstr.h>
#include <ZhongriSettingsMenu.h>
#include <InputFeatureIcon.h>

// nasty
#include <resource.h>

static UINT mode_icon[] = {IDI_ZH, IDI_ZH, IDI_EN, IDI_RELOAD};
static const WCHAR* mode_label[] = {NULL, /*L"中文"*/ NULL, /*L"西文"*/ NULL,
                                    L"Under maintenance"};

WeaselTrayIcon::WeaselTrayIcon(weasel::UI& ui)
    : m_style(ui.style()),
      m_status(ui.status()),
      m_mode(INITIAL),
      m_schema_zhung_icon(),
      m_schema_ascii_icon(),
      m_disabled(false) {}

void WeaselTrayIcon::CustomizeMenu(HMENU hMenu) {
  CustomizeSettingsMenu(hMenu);
}

void WeaselTrayIcon::CustomizeSettingsMenu(HMENU hMenu) {
  // Both settings commands already resolve the installed WinUI panel.
  // Make the user-facing entry unambiguous, independently of resource language.
  ConfigureZhongriSettingsMenu(hMenu);
}

BOOL WeaselTrayIcon::Create(HWND hTargetWnd) {
  HMODULE hModule = GetModuleHandle(NULL);
  CIcon icon;
  icon.LoadIconW(IDI_ZH);
  BOOL bRet =
      CSystemTray::Create(hModule, NULL, WM_WEASEL_TRAY_NOTIFY,
                          get_weasel_ime_name().c_str(), icon, IDR_MENU_POPUP);
  if (hTargetWnd) {
    SetTargetWnd(hTargetWnd);
  }
  if (!m_style.display_tray_icon) {
    RemoveIcon();
  } else {
    AddIcon();
  }
  return bRet;
}

WeaselTrayIcon::~WeaselTrayIcon() {
  if (m_feature_icon) DestroyIcon(m_feature_icon);
}

static HICON FeatureIcon(bool learning, bool ascii, bool ascii_punct) {
  HDC dc = CreateCompatibleDC(nullptr);
  if (!dc) return nullptr;
  BITMAPV5HEADER header = {};
  header.bV5Size = sizeof(header); header.bV5Width = 32; header.bV5Height = -32;
  header.bV5Planes = 1; header.bV5BitCount = 32; header.bV5Compression = BI_BITFIELDS;
  header.bV5RedMask = 0x00ff0000; header.bV5GreenMask = 0x0000ff00;
  header.bV5BlueMask = 0x000000ff; header.bV5AlphaMask = 0xff000000;
  void* pixels = nullptr;
  HBITMAP bitmap = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &pixels, nullptr, 0);
  if (!bitmap) { DeleteDC(dc); return nullptr; }
  auto previous = SelectObject(dc, bitmap);
  HBRUSH background = CreateSolidBrush(learning ? RGB(30,145,85) : RGB(0,140,160));
  RECT rect = {0,0,32,32}; FillRect(dc, &rect, background); DeleteObject(background);
  SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255,255,255));
  HFONT font = CreateFontW(-24,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei");
  auto old_font = SelectObject(dc,font);
  DrawTextW(dc, learning ? L"学" : ascii ? L"A" : L"中", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  SelectObject(dc,old_font); DeleteObject(font);
  RECT badge = {20,20,32,32}; HBRUSH dark = CreateSolidBrush(RGB(30,35,40));
  FillRect(dc, &badge, dark); DeleteObject(dark);
  font = CreateFontW(-13,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  old_font = SelectObject(dc,font);
  DrawTextW(dc, ascii_punct ? L"E" : L"C", -1, &badge, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  SelectObject(dc,old_font); DeleteObject(font);
  auto colors = static_cast<DWORD*>(pixels);
  for (int i=0;i<32*32;i++) colors[i] |= 0xff000000;
  SelectObject(dc,previous); DeleteDC(dc);
  HBITMAP mask = CreateBitmap(32,32,1,1,nullptr);
  ICONINFO info = {}; info.fIcon = TRUE; info.hbmColor = bitmap; info.hbmMask = mask;
  HICON icon = CreateIconIndirect(&info); DeleteObject(bitmap); DeleteObject(mask);
  return icon;
}

void WeaselTrayIcon::Refresh(bool learning, bool ascii_punct) {
  const std::wstring diagnostic = L"learning=" + std::to_wstring(learning) +
      L" display=" + std::to_wstring(m_style.display_tray_icon) +
      L" disabled=" + std::to_wstring(m_status.disabled) +
      L" enabled=" + std::to_wstring(m_bEnabled) +
      L" hidden=" + std::to_wstring(m_bHidden) +
      L" state=" + std::to_wstring(m_feature_state);
  static std::wstring last_diagnostic;
  if (diagnostic != last_diagnostic) {
    HKEY key=nullptr;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\ZhongriInputMethod",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS) {
      RegSetValueExW(key,L"TrayFeatureDiagnostic",0,REG_SZ,reinterpret_cast<const BYTE*>(diagnostic.c_str()),static_cast<DWORD>((diagnostic.size()+1)*sizeof(wchar_t)));
      RegCloseKey(key);
    }
    last_diagnostic=diagnostic;
  }
  if (!m_style.display_tray_icon &&
      !m_status.disabled)  // display notification when deploying
  {
    if (m_mode != INITIAL) {
      RemoveIcon();
      m_mode = INITIAL;
    }
    m_disabled = false;
    return;
  }
  WeaselTrayMode mode = m_status.disabled     ? DISABLED
                        : m_status.ascii_mode ? ASCII
                                              : ZHUNG;
  /* change icon, when
          1,mode changed
          2,icon changed
          3,both m_schema_zhung_icon and m_style.current_zhung_icon empty(for
     initialize) 4,both m_schema_ascii_icon and m_style.current_ascii_icon
     empty(for initialize)
  */
  const bool base_changed = mode != m_mode || m_schema_zhung_icon != m_style.current_zhung_icon ||
      m_schema_ascii_icon != m_style.current_ascii_icon || m_feature_state < 0;
  if (base_changed) {
    ShowIcon();
    m_mode = mode;
    m_schema_zhung_icon = m_style.current_zhung_icon;
    m_schema_ascii_icon = m_style.current_ascii_icon;
    if (mode == ASCII) {
      if (m_schema_ascii_icon.empty())
        SetIcon(mode_icon[mode]);
      else
        SetIcon(m_schema_ascii_icon.c_str());
    } else if (mode == ZHUNG) {
      if (m_schema_zhung_icon.empty())
        SetIcon(mode_icon[mode]);
      else
        SetIcon(m_schema_zhung_icon.c_str());
    } else
      SetIcon(mode_icon[mode]);

    if (mode_label[mode] && m_disabled == false) {
      CString info;
      info.LoadStringW(IDS_STR_UNDER_MAINTENANCE);
      ShowBalloon(info, get_weasel_ime_name().c_str());
      m_disabled = true;
    }
    if (m_mode != DISABLED)
      m_disabled = false;
  } else if (!Visible()) {
    ShowIcon();
  }
  if (mode != DISABLED) {
    const int state = (learning ? 4 : 0) | (mode == ASCII ? 2 : 0) | (ascii_punct ? 1 : 0);
    if (state != m_feature_state) {
      const auto icon = MakeInputFeatureIcon(state);
      if (icon) {
        if (SetIcon(icon)) {
          if (m_feature_icon) DestroyIcon(m_feature_icon);
          m_feature_icon = icon; m_feature_state = state;
        } else DestroyIcon(icon);
      }
    } else if (m_feature_icon) SetIcon(m_feature_icon);
    const std::wstring tip = std::wstring(L"中日输入法\n学习模式：") + (learning ? L"开启" : L"关闭") +
        L"\n符号：" + (ascii_punct ? L"英文（E）" : L"中文（C）") +
        L"\n输入：" + (mode == ASCII ? L"英文直输" : L"中日转换");
    if (tip != m_feature_tooltip) { SetTooltipText(tip.c_str()); m_feature_tooltip = tip; }
    NOTIFYICONIDENTIFIER identifier={}; identifier.cbSize=sizeof(identifier);
    identifier.hWnd=m_tnd.hWnd; identifier.uID=m_tnd.uID;
    RECT actualRect={}; const HRESULT trayResult=Shell_NotifyIconGetRect(&identifier,&actualRect);
    const std::wstring receipt= L"state="+std::to_wstring(state)+L" rect_hr="+std::to_wstring(trayResult)+
        L" x="+std::to_wstring(actualRect.left)+L" y="+std::to_wstring(actualRect.top);
    static std::wstring last_receipt;
    if(receipt!=last_receipt) {
      HKEY key=nullptr;
      if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\ZhongriInputMethod",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS) {
        RegSetValueExW(key,L"TrayFeatureReceipt",0,REG_SZ,reinterpret_cast<const BYTE*>(receipt.c_str()),static_cast<DWORD>((receipt.size()+1)*sizeof(wchar_t))); RegCloseKey(key);
      }
      last_receipt=receipt;
    }
  } else { m_feature_state = -1; }
}
