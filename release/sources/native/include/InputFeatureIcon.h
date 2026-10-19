#pragma once
#include <windows.h>
#include <string>
inline DWORD InputFeatureFlags(bool ascii) {
  DWORD learning=0, punct=ascii ? 1 : 0, bytes=sizeof(DWORD);
  RegGetValueW(HKEY_CURRENT_USER,L"Software\\ZhongriInputMethod",L"Option_japanese_learning_mode",RRF_RT_REG_DWORD,nullptr,&learning,&bytes);
  bytes=sizeof(DWORD);
  RegGetValueW(HKEY_CURRENT_USER,L"Software\\ZhongriInputMethod",L"TrayAsciiPunct",RRF_RT_REG_DWORD,nullptr,&punct,&bytes);
  return (learning ? 4 : 0) | (ascii ? 2 : 0) | ((punct || ascii) ? 1 : 0);
}
inline std::wstring InputFeatureTooltip(DWORD flags) {
  return std::wstring(L"中日输入法\n学习模式：") + ((flags&4)?L"开启":L"关闭") +
    L"\n符号：" + ((flags&1)?L"英文（E）":L"中文（C）") +
    L"\n输入：" + ((flags&2)?L"英文直输":L"中日转换");
}
inline HICON MakeInputFeatureIcon(DWORD flags) {
  HDC dc=CreateCompatibleDC(nullptr); if(!dc) return nullptr;
  BITMAPINFO info={}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth=32; info.bmiHeader.biHeight=-32;
  info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
  void* pixels=nullptr;
  HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
  if(!bitmap) { DeleteDC(dc); return nullptr; }
  auto old=SelectObject(dc,bitmap); RECT rect={0,0,32,32};
  HBRUSH brush=CreateSolidBrush((flags&4)?RGB(30,145,85):RGB(0,140,160));
  FillRect(dc,&rect,brush); DeleteObject(brush);
  SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(255,255,255));
  HFONT font=CreateFontW(-24,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei");
  auto oldfont=SelectObject(dc,font);
  DrawTextW(dc,(flags&4)?L"学":(flags&2)?L"A":L"中",-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
  SelectObject(dc,oldfont); DeleteObject(font);
  RECT badge={20,20,32,32}; brush=CreateSolidBrush(RGB(30,35,40));
  FillRect(dc,&badge,brush); DeleteObject(brush);
  font=CreateFontW(-13,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  oldfont=SelectObject(dc,font); DrawTextW(dc,(flags&1)?L"E":L"C",-1,&badge,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
  SelectObject(dc,oldfont); DeleteObject(font);
  for(int i=0;i<1024;i++) static_cast<DWORD*>(pixels)[i]|=0xff000000;
  SelectObject(dc,old); DeleteDC(dc);
  BYTE bits[128]={}; HBITMAP mask=CreateBitmap(32,32,1,1,bits);
  ICONINFO icon={}; icon.fIcon=TRUE; icon.hbmColor=bitmap; icon.hbmMask=mask;
  HICON result=CreateIconIndirect(&icon); DeleteObject(bitmap); DeleteObject(mask); return result;
}
