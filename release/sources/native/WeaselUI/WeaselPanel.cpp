#include "stdafx.h"
#include "CloudBadge.h"
#include "WeaselPanel.h"
#include "FuzzyCorrectionMask.h"
#include "ReservedPhraseMask.h"
#include <CandidateLanguageSplit.h>
#include <SplitTextLayout.h>

static void OwnedPaintTrace(const std::wstring& preedit,int height,const std::wstring& line,bool reset=false) {
  if(preedit!=L"owned-split-fixture")return;
  wchar_t owned[MAX_PATH]={},report[MAX_PATH]={},capture[MAX_PATH]={},phase[16]={};
  if(!GetEnvironmentVariableW(L"ZHONGRI_TEST_RUN_ROOT",owned,MAX_PATH)||!GetEnvironmentVariableW(L"ZHONGRI_TEST_REPORT_ROOT",report,MAX_PATH)||!GetEnvironmentVariableW(L"ZHONGRI_TEST_CAPTURE_DIR",capture,MAX_PATH)||_wcsicmp(report,capture)!=0)return;
  GetEnvironmentVariableW(L"ZHONGRI_TEST_CAPTURE_PHASE",phase,16);
  const auto path=std::wstring(report)+(height<0?L"\\paint-expanded":L"\\paint-compact")+(wcscmp(phase,L"off")==0?L"-off.txt":L".txt");
  HANDLE file=CreateFileW(path.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,reset?CREATE_ALWAYS:OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file==INVALID_HANDLE_VALUE)return;DWORD bytes=0;const wchar_t bom=0xfeff;
  if(reset)WriteFile(file,&bom,2,&bytes,nullptr);
  const auto row=line+L"\r\n";WriteFile(file,row.data(),DWORD(row.size()*2),&bytes,nullptr);CloseHandle(file);
}

static void PanelFrameTrace(const std::string& message) {
  // Normal painting is latency-sensitive.  Keep only the targeted right-click
  // diagnostic instead of writing every layout frame to disk.
  if (message.rfind("RIGHT_CLICK", 0) != 0 &&
      message.rfind("PREVIEW", 0) != 0 &&
      message.rfind("WHEEL", 0) != 0 &&
      message.rfind("SELECTED_PALETTE", 0) != 0)
    return;
  wchar_t temp_path[MAX_PATH] = {};
  if (!GetTempPathW(MAX_PATH, temp_path)) return;
  std::wstring path(temp_path);
  path += L"weasel_candidate_expand.log";
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  const std::string line = std::to_string(GetTickCount64()) +
                           " [DRAW pid=" +
                           std::to_string(GetCurrentProcessId()) + "] " +
                           message + "\r\n";
  DWORD written = 0;
  WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written,
            nullptr);
  CloseHandle(file);
}

static bool IsAltReadingPreviewPollingEnabled() {
  DWORD value = 0;
  DWORD size = sizeof(value);
  return RegGetValueW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod",
                      L"AltReadingPreview", RRF_RT_REG_DWORD, nullptr,
                      &value, &size) == ERROR_SUCCESS && value != 0;
}

static unsigned long long g_panel_frame = 0;
static unsigned long long g_active_panel_frame = 0;
WeaselPanel* WeaselPanel::s_altPreviewHookPanel = nullptr;

void WeaselPanel::_EnsureAltPreviewKeyboardHook() {
  if (m_altPreviewKeyboardHook) return;
  s_altPreviewHookPanel = this;
  m_altPreviewKeyboardHook =
      ::SetWindowsHookExW(WH_KEYBOARD, AltPreviewKeyboardProc, nullptr,
                          ::GetCurrentThreadId());
}

void WeaselPanel::_RemoveAltPreviewKeyboardHook() {
  if (m_altPreviewKeyboardHook) {
    ::UnhookWindowsHookEx(m_altPreviewKeyboardHook);
    m_altPreviewKeyboardHook = nullptr;
  }
  if (s_altPreviewHookPanel == this) s_altPreviewHookPanel = nullptr;
  m_altPreviewReleasePending = false;
  m_altPreviewChordObserved = false;
}

LRESULT CALLBACK WeaselPanel::AltPreviewKeyboardProc(int code,
                                                      WPARAM wParam,
                                                      LPARAM lParam) {
  WeaselPanel* panel = s_altPreviewHookPanel;
  if (code == HC_ACTION && panel) {
    const bool key_up = (lParam & 0x80000000) != 0;
    const bool is_alt = wParam == VK_MENU || wParam == VK_LMENU ||
                        wParam == VK_RMENU;
    if (!is_alt && !key_up && panel->m_altPreviewReleasePending)
      panel->m_altPreviewChordObserved = true;

    if (is_alt && key_up && panel->m_altPreviewReleasePending) {
      const bool swallow_bare_alt = !panel->m_altPreviewChordObserved;
      panel->m_altPreviewReleasePending = false;
      panel->m_altPreviewChordObserved = false;
      if (panel->m_shiftReadingPreview)
        panel->SetJapaneseReadingPreview(false);
      if (swallow_bare_alt) {
        PanelFrameTrace("PREVIEW swallowed bare Alt release");
        return 1;
      }
    }
  }
  return ::CallNextHookEx(panel ? panel->m_altPreviewKeyboardHook : nullptr,
                          code, wParam, lParam);
}

LRESULT WeaselPanel::OnShowWindow(UINT,
                                  WPARAM wParam,
                                  LPARAM lParam,
                                  BOOL& bHandled) {
  if (wParam && !m_in_server)
    ::SetTimer(m_hWnd, HOT_ANNOTATION_TIMER, 150, nullptr);
  else
    ::KillTimer(m_hWnd, HOT_ANNOTATION_TIMER);
  // A host may hide the candidate window before forwarding the preview key's
  // release. The timer then stops with the window and the next composition
  // inherits the old reading projection. On a new show, keep the preview
  // only while a preview key is physically held.
  if (wParam && m_shiftReadingPreview) {
    const bool held = (::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 ||
                      (::GetAsyncKeyState(VK_SPACE) & 0x8000) != 0 ||
                      (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    if (!held) {
      m_shiftReadingPreview = false;
      _ApplyJapaneseReadingDisplay();
      PanelFrameTrace("PREVIEW cleared stale state on show");
    }
  }
  PanelFrameTrace("WM_SHOWWINDOW show=" + std::to_string(wParam != 0) +
                  " reason=" + std::to_string(lParam) +
                  " visible=" + std::to_string(IsWindowVisible()));
  bHandled = FALSE;
  return 0;
}

LRESULT WeaselPanel::OnWindowPosChanged(UINT,
                                        WPARAM,
                                        LPARAM lParam,
                                        BOOL& bHandled) {
  const auto* pos = reinterpret_cast<const WINDOWPOS*>(lParam);
  if (pos) {
    PanelFrameTrace("WM_WINDOWPOSCHANGED flags=" +
                    std::to_string(pos->flags) + " x=" +
                    std::to_string(pos->x) + " y=" +
                    std::to_string(pos->y) + " cx=" +
                    std::to_string(pos->cx) + " cy=" +
                    std::to_string(pos->cy) + " after=" +
                    std::to_string(reinterpret_cast<uintptr_t>(
                        pos->hwndInsertAfter)) +
                    " visible=" + std::to_string(IsWindowVisible()));
  }
  bHandled = FALSE;
  return 0;
}

static std::wstring StripDisplayedTranslationPrefixes(std::wstring text) {
  // Strip every internal metadata token, including duplicated/nested tokens
  // introduced when a candidate passes through multiple filters.
  const std::wstring hidden_marker = L"[[RIME_";
  size_t hidden_pos = 0;
  while ((hidden_pos = text.find(hidden_marker)) != std::wstring::npos) {
    const size_t hidden_end = text.find(L"]]", hidden_pos);
    text.erase(hidden_pos,
               hidden_end == std::wstring::npos
                   ? std::wstring::npos
                   : hidden_end + 2 - hidden_pos);
  }
  const std::wstring marker = L"[[RIME_JR:";
  const size_t reading_pos = text.find(marker);
  if (reading_pos != std::wstring::npos) text.erase(reading_pos);
  // Clean candidates produced by the short-lived control-character protocol.
  // This also repairs an already-open session without leaking `JR:reading`.
  const size_t legacy_pos = text.find(L"JR:");
  if (legacy_pos != std::wstring::npos) text.erase(legacy_pos);
  if (text.rfind(L"EN:", 0) == 0) text.erase(0, 3);
  if (text.rfind(L"JA:", 0) == 0) text.erase(0, 3);
  size_t pos = 0;
  while ((pos = text.find(L"\nEN:", pos)) != std::wstring::npos)
    text.erase(pos + 1, 3);
  pos = 0;
  while ((pos = text.find(L"\nJA:", pos)) != std::wstring::npos)
    text.erase(pos + 1, 3);
  return text;
}

static std::wstring ReadingPreviewMetadata(const std::wstring& comment) {
  std::wstring result;
  for(size_t pos=0;(pos=comment.find(L"[[RIME_",pos))!=std::wstring::npos;){
    const auto end=comment.find(L"]]",pos);
    if(end==std::wstring::npos)break;
    // JR is restored once from the separately cached pronunciation below.
    if(comment.compare(pos,10,L"[[RIME_JR:")!=0)
      result+=comment.substr(pos,end+2-pos);
    pos=end+2;
  }
  return result;
}

enum class CandidateLanguage { Other, Chinese, Japanese, CommonPhrase };

static CandidateLanguage HiddenCandidateLanguage(const std::wstring& comment) {
  if (comment.find(L"[[RIME_LANG:CP]]") != std::wstring::npos)
    return CandidateLanguage::CommonPhrase;
  if (comment.find(L"[[RIME_LANG:JA]]") != std::wstring::npos)
    return CandidateLanguage::Japanese;
  if (comment.find(L"[[RIME_LANG:ZH]]") != std::wstring::npos)
    return CandidateLanguage::Chinese;
  return CandidateLanguage::Other;
}

static CandidateLanguage CandidateLanguageFromTextAndComment(
    const std::wstring& text,
    const std::wstring& comment) {
  const CandidateLanguage marked = HiddenCandidateLanguage(comment);
  if (marked == CandidateLanguage::CommonPhrase)
    return marked;
  // Kana in the visible candidate is definitive Japanese evidence and must
  // outrank stale metadata left by another translator/filter.
  for (const wchar_t ch : text) {
    if ((ch >= 0x3040 && ch <= 0x30ff) || ch == 0x30fc)
      return CandidateLanguage::Japanese;
  }
  return marked;
}

static std::wstring HiddenJapaneseReading(const std::wstring& text) {
  const std::wstring marker = L"[[RIME_JR:";
  const size_t pos = text.find(marker);
  if (pos != std::wstring::npos) {
    const size_t end = text.find(L"]]", pos + marker.length());
    if (end != std::wstring::npos)
      return text.substr(pos + marker.length(),
                         end - (pos + marker.length()));
  }
  const size_t legacy_pos = text.find(L"JR:");
  return legacy_pos == std::wstring::npos ? L""
                                          : text.substr(legacy_pos + 3);
}

static std::wstring CommentWithJapaneseReading(
    const std::wstring& original,
    const std::wstring& reading) {
  const std::wstring reading_line = L"[" + reading + L"]";
  if (original.empty()) return reading_line;
  const size_t newline = original.find(L'\n');
  // English is the first annotation line.  Keep it visible and replace only
  // the Japanese translation line with the pronunciation.
  const std::wstring english =
      newline == std::wstring::npos ? original : original.substr(0, newline);
  return english.empty() ? reading_line : english + L"\n" + reading_line;
}

static float MeasureSingleLineWidth(const std::wstring& text,
                                    IDWriteTextFormat1* format,
                                    const weasel::PDWR& resources) {
  if (text.empty() || format == nullptr || resources == nullptr)
    return 0.0f;
  if (FAILED(resources->CreateTextLayout(text, static_cast<int>(text.size()),
                                         format, 100000.0f, 1000.0f)))
    return 0.0f;
  DWRITE_TEXT_METRICS metrics = {};
  if (FAILED(resources->GetLayoutMetrics(&metrics)))
    return 0.0f;
  return metrics.widthIncludingTrailingWhitespace;
}

// DirectWrite's layout clipping is unreliable on the transparent candidate
// window.  Shorten display text ourselves; the original Rime candidate is
// untouched and is still committed in full when selected.
static std::wstring EllipsizeSingleLine(const std::wstring& text,
                                        float max_width,
                                        IDWriteTextFormat1* format,
                                        const weasel::PDWR& resources) {
  if (text.empty() || max_width <= 0.0f ||
      MeasureSingleLineWidth(text, format, resources) <= max_width)
    return text;

  const std::wstring ellipsis = L"\u2026";
  if (MeasureSingleLineWidth(ellipsis, format, resources) > max_width)
    return L"";

  size_t low = 0;
  size_t high = text.size();
  while (low < high) {
    const size_t middle = low + (high - low + 1) / 2;
    const std::wstring trial = text.substr(0, middle) + ellipsis;
    if (MeasureSingleLineWidth(trial, format, resources) <= max_width)
      low = middle;
    else
      high = middle - 1;
  }
  return text.substr(0, low) + ellipsis;
}

static std::wstring EllipsizeEachLine(const std::wstring& text,
                                      float max_width,
                                      IDWriteTextFormat1* format,
                                      const weasel::PDWR& resources) {
  std::wstring result;
  size_t start = 0;
  while (start <= text.size()) {
    const size_t end = text.find(L'\n', start);
    const size_t length =
        end == std::wstring::npos ? text.size() - start : end - start;
    if (!result.empty()) result.push_back(L'\n');
    result += EllipsizeSingleLine(text.substr(start, length), max_width,
                                  format, resources);
    if (end == std::wstring::npos) break;
    start = end + 1;
  }
  return result;
}

static float WidestCommentLine(const std::wstring& text,
                               IDWriteTextFormat1* format,
                               const weasel::PDWR& resources) {
  size_t line_start = 0;
  float widest_line = 0.0f;
  while (line_start <= text.size()) {
    const size_t line_end = text.find(L'\n', line_start);
    const std::wstring line = text.substr(
        line_start, line_end == std::wstring::npos
                        ? std::wstring::npos
                        : line_end - line_start);
    widest_line = max(widest_line,
                      MeasureSingleLineWidth(line, format, resources));
    if (line_end == std::wstring::npos) break;
    line_start = line_end + 1;
  }
  return widest_line;
}

static void ExpandedEllipsisTrace(size_t index, size_t before, size_t after,
                                  int rect_width) {
  wchar_t temp_path[MAX_PATH] = {};
  if (!GetTempPathW(MAX_PATH, temp_path)) return;
  std::wstring path(temp_path);
  path += L"weasel_candidate_expand.log";
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  const std::string line = std::to_string(GetTickCount64()) +
      " [DRAW pid=" + std::to_string(GetCurrentProcessId()) +
      "] ELLIPSIS index=" + std::to_string(index) +
      " before=" + std::to_string(before) +
      " after=" + std::to_string(after) +
      " width=" + std::to_string(rect_width) + "\r\n";
  DWORD written = 0;
  WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written,
            nullptr);
  CloseHandle(file);
}

#include <utility>
#include <ShellScalingApi.h>
#include <VersionHelpers.hpp>
#include <WeaselIPCData.h>
#include <algorithm>

#include "VerticalLayout.h"
#include "HorizontalLayout.h"
#include "FullScreenLayout.h"
#include "VHorizontalLayout.h"

// for IDI_ZH, IDI_EN
#include <resource.h>
#define COLORTRANSPARENT(color) ((color & 0xff000000) == 0)
#define COLORNOTTRANSPARENT(color) ((color & 0xff000000) != 0)
#define TRANS_COLOR 0x00000000
#define GDPCOLOR_FROM_COLORREF(color)                                \
  Gdiplus::Color::MakeARGB(((color >> 24) & 0xff), GetRValue(color), \
                           GetGValue(color), GetBValue(color))
#define HALF_ALPHA_COLOR(color) \
  ((((color & 0xff000000) >> 25) & 0xff) << 24) | (color & 0x00ffffff)

#pragma comment(lib, "Shcore.lib")

template <class t0, class t1, class t2>
inline void LoadIconNecessary(t0& a, t1& b, t2& c, int d) {
  if (a == b)
    return;
  a = b;
  if (b.empty())
    c.LoadIconW(d, STATUS_ICON_SIZE, STATUS_ICON_SIZE, LR_DEFAULTCOLOR);
  else
    c = (HICON)LoadImage(NULL, b.c_str(), IMAGE_ICON, STATUS_ICON_SIZE,
                         STATUS_ICON_SIZE, LR_LOADFROMFILE);
}

static inline void ReconfigRoundInfo(IsToRoundStruct& rd,
                                     const int& i,
                                     const int& m_candidateCount) {
  if (i == 0 && m_candidateCount > 1) {
    std::swap(rd.IsTopLeftNeedToRound, rd.IsBottomLeftNeedToRound);
    std::swap(rd.IsTopRightNeedToRound, rd.IsBottomRightNeedToRound);
  }
  if (i == m_candidateCount - 1) {
    std::swap(rd.IsTopLeftNeedToRound, rd.IsBottomLeftNeedToRound);
    std::swap(rd.IsTopRightNeedToRound, rd.IsBottomRightNeedToRound);
  }
}

WeaselPanel::WeaselPanel(weasel::UI& ui)
    : m_layout(NULL),
      m_ctx(ui.ctx()),
      m_octx(ui.octx()),
      m_status(ui.status()),
      m_in_server(ui.InServer()),
      m_style(ui.style()),
      m_ostyle(ui.ostyle()),
      m_candidateCount(0),
      m_lastCandidateCount(0),
      m_current_zhung_icon(),
      m_inputPos(0, 0, 0, 0),
      m_sticky(false),
      dpi(96),
      hide_candidates(false),
      pDWR(ui.pdwr()),
      _UICallback(ui.uiCallback()),
      _hotAnnotationCallback(ui.hotAnnotationCallback()),
      _m_gdiplusToken(0) {
  m_iconDisabled.LoadIconW(IDI_RELOAD, STATUS_ICON_SIZE, STATUS_ICON_SIZE,
                           LR_DEFAULTCOLOR);
  m_iconEnabled.LoadIconW(IDI_ZH, STATUS_ICON_SIZE, STATUS_ICON_SIZE,
                          LR_DEFAULTCOLOR);
  m_iconAlpha.LoadIconW(IDI_EN, STATUS_ICON_SIZE, STATUS_ICON_SIZE,
                        LR_DEFAULTCOLOR);
  m_iconFull.LoadIconW(IDI_FULL_SHAPE, STATUS_ICON_SIZE, STATUS_ICON_SIZE,
                       LR_DEFAULTCOLOR);
  m_iconHalf.LoadIconW(IDI_HALF_SHAPE, STATUS_ICON_SIZE, STATUS_ICON_SIZE,
                       LR_DEFAULTCOLOR);
  // for gdi+ drawings, initialization
  GdiplusStartup(&_m_gdiplusToken, &_m_gdiplusStartupInput, NULL);

  HMONITOR hMonitor = MonitorFromRect(m_inputPos, MONITOR_DEFAULTTONEAREST);
  UINT dpiX = 96, dpiY = 96;
  if (hMonitor) {
    GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    m_hMonitor = hMonitor;
  }
  dpi = dpiX;
  _InitFontRes();
  m_ostyle = m_style;
}

WeaselPanel::~WeaselPanel() {
  Gdiplus::GdiplusShutdown(_m_gdiplusToken);
  delete m_layout;
  m_layout = NULL;
  // pDWR.reset();
}

void WeaselPanel::_ResizeWindow() {
  CDCHandle dc = GetDC();
  CSize m_size = m_layout->GetContentSize();
  SetWindowPos(NULL, 0, 0, m_size.cx, m_size.cy,
               SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOREDRAW);
  ReleaseDC(dc);
}

void WeaselPanel::_CreateLayout() {
  if (m_layout != NULL)
    delete m_layout;

  Layout* layout = NULL;
  if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT) {
    layout = new VHorizontalLayout(m_style, m_ctx, m_status, pDWR);
  } else {
    if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL ||
        m_style.layout_type == UIStyle::LAYOUT_VERTICAL_FULLSCREEN) {
      layout = new VerticalLayout(m_style, m_ctx, m_status, pDWR);
    } else if (m_style.layout_type == UIStyle::LAYOUT_HORIZONTAL ||
               m_style.layout_type == UIStyle::LAYOUT_HORIZONTAL_FULLSCREEN) {
      layout = new HorizontalLayout(m_style, m_ctx, m_status, pDWR);
    }

    if (IS_FULLSCREENLAYOUT(m_style)) {
      layout = new FullScreenLayout(m_style, m_ctx, m_status, m_inputPos,
                                    layout, pDWR);
    }
  }
  m_layout = layout;
}

// 更新界面
void WeaselPanel::Refresh() {
  // Candidate text or geometry may be replaced while the pointer is still.
  // Never leave a tooltip belonging to the previous candidate page onscreen;
  // the next WM_MOUSEMOVE re-evaluates actual truncation.
  _HideCommentTooltip();
  ::KillTimer(m_hWnd, COMMENT_MARQUEE_TIMER);
  m_marqueeIndex = -1;
  m_marqueeOffset = 0.0f;
  m_marqueeMaxOffset = 0.0f;
  m_lastMousePos = {-1, -1};
  // A delayed server/context update can replace the temporarily transformed
  // comments while Shift is still physically held.  Reapply the display
  // projection on every refresh; key-up remains the only operation that
  // clears m_shiftReadingPreview.  This also preserves right-click toggles
  // across an otherwise identical candidate refresh.
  _SyncJapaneseReadingCache();
  // A new composition may reuse the same panel without a WM_SHOWWINDOW.
  // Recover a missed key-up before painting its first candidate frame.
  if (m_shiftReadingPreview &&
      ::GetTickCount64() - m_readingPreviewStarted >= 120 &&
      !(::GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
      !(::GetAsyncKeyState(VK_SPACE) & 0x8000) &&
      !(::GetAsyncKeyState(VK_MENU) & 0x8000)) {
    m_shiftReadingPreview = false;
    _ApplyJapaneseReadingDisplay();
    PanelFrameTrace("PREVIEW cleared stale state on refresh");
  }
  // Some packaged Windows apps (notably the current Windows 11 Notepad)
  // consume a bare Alt as a menu key before TSF receives OnKeyDown.  Poll Alt
  // while candidates are visible so the dedicated preview works in those
  // hosts as well as in ordinary TSF edit controls.
  if (IsAltReadingPreviewPollingEnabled() &&
      !m_ctx.cinfo.candies.empty()) {
    _EnsureAltPreviewKeyboardHook();
    ::SetTimer(m_hWnd, READING_PREVIEW_TIMER, 35, nullptr);
  } else if (m_shiftReadingPreview) {
    // Re-arm after a host temporarily hid the panel; otherwise a missed
    // key-up leaves the reading visible on every later composition.
    ::SetTimer(m_hWnd, READING_PREVIEW_TIMER, 35, nullptr);
  } else {
    ::KillTimer(m_hWnd, READING_PREVIEW_TIMER);
    _RemoveAltPreviewKeyboardHook();
  }
  // Shift preview is controlled only by the paired TSF key-down/key-up calls.
  // GetAsyncKeyState still reports "down" briefly while handling key-up; using
  // it here re-latched the preview during Refresh(), so one tap left readings
  // visible forever.  While Shift is genuinely held, m_shiftReadingPreview
  // remains true across all delayed context refreshes by itself.
  bool has_persistent_reading = m_shiftReadingPreview;
  if (!has_persistent_reading) {
    for (bool shown : m_readingShown) {
      if (shown) {
        has_persistent_reading = true;
        break;
      }
    }
  }
  if (has_persistent_reading)
    _ApplyJapaneseReadingDisplay();

  g_active_panel_frame = ++g_panel_frame;
  {
    CRect before;
    GetWindowRect(&before);
    PanelFrameTrace(
        "BEGIN frame=" + std::to_string(g_active_panel_frame) +
        " highlighted=" + std::to_string(m_ctx.cinfo.highlighted) +
        " candidates=" + std::to_string(m_ctx.cinfo.candies.size()) +
        " maxh=" + std::to_string(m_style.max_height) +
        " visible=" + std::to_string(IsWindowVisible()) + " win=" +
        std::to_string(before.left) + ":" + std::to_string(before.top) +
        ":" + std::to_string(before.right) + ":" +
        std::to_string(before.bottom));
  }
  bool should_show_icon =
      (m_status.ascii_mode || !m_status.composing || !m_ctx.aux.empty());
  m_candidateCount = min(m_ctx.cinfo.candies.size(), MAX_CANDIDATES_COUNT);
  // When the candidate window changes from having content to having no content,
  // reset the sticky state
  if (m_lastCandidateCount > 0 && m_candidateCount == 0) {
    m_sticky = false;
  }
  m_lastCandidateCount = m_candidateCount;
  // check if to hide candidates window
  // show tips status, two kind of situation: 1) only aux strings, don't care
  // icon status; 2)only icon(ascii mode switching)
  bool show_tips =
      m_in_server &&
      ((!m_ctx.aux.empty() && m_ctx.cinfo.empty() && m_ctx.preedit.empty()) ||
       (m_ctx.empty() && should_show_icon));
  // show schema menu status: schema_id == L".default"
  bool show_schema_menu = m_status.schema_id == L".default";
  bool margin_negative =
      (DPI_SCALE(m_style.margin_x) < 0 || DPI_SCALE(m_style.margin_y) < 0);
  // when to hide_cadidates?
  // 1. margin_negative, and not in show tips mode( ascii switching / half-full
  // switching / simp-trad switching / error tips), and not in schema menu
  // 2. inline preedit without candidates
  bool inline_no_candidates =
      (m_style.inline_preedit && m_candidateCount == 0) && !show_tips;
  hide_candidates = inline_no_candidates ||
                    (margin_negative && !show_tips && !show_schema_menu);

  // only RedrawWindow if no need to hide candidates window, or
  // inline_no_candidates
  if (!hide_candidates || inline_no_candidates) {
    // A navigation key can change only the presentation style (for example,
    // normal row -> persistent expanded grid) while the Rime context remains
    // byte-for-byte identical.  _InitFontRes() copies m_style to m_ostyle, so
    // remember this before initializing resources.  Previously the window was
    // resized for the grid but RedrawWindow() was skipped because m_ctx had not
    // changed; the old one-row pixels then remained until another key was
    // pressed or the navigation key was held down.
    const auto splitOptions=weasel::EffectiveCandidateSplitOptions(m_style);
    const int splitSignature=splitOptions.enabled?256+splitOptions.left*16+splitOptions.right:0;
    const bool presentation_changed = (m_style != m_ostyle)||m_seenSplitLayoutSignature!=splitSignature;
    m_seenSplitLayoutSignature=splitSignature;
    _InitFontRes();
    _CreateLayout();

    CDCHandle dc = GetDC();
    m_layout->DoLayout(dc, pDWR);
    ReleaseDC(dc);
    _ResizeWindow();
    _RepositionWindow();
    if (m_ctx != m_octx || presentation_changed) {
      m_octx = m_ctx;
      RedrawWindow();
    }
  }
  {
    CRect after;
    GetWindowRect(&after);
    PanelFrameTrace(
        "END frame=" + std::to_string(g_active_panel_frame) +
        " hidden=" + std::to_string(hide_candidates) + " visible=" +
        std::to_string(IsWindowVisible()) + " win=" +
        std::to_string(after.left) + ":" + std::to_string(after.top) + ":" +
        std::to_string(after.right) + ":" + std::to_string(after.bottom));
  }
}

std::vector<std::vector<int>> WeaselPanel::GetExpandedCandidateRows() const {
  if (!m_layout)
    return {};
  return m_layout->expanded_candidate_rows;
}

int WeaselPanel::GetVisibleCandidateCount() const {
  if (!m_layout)
    return -1;
  // Match DoPaint: layouts other than horizontal do not set this override.
  return m_layout->visible_candidates > 0
             ? m_layout->visible_candidates : m_candidateCount;
}

void WeaselPanel::_InitFontRes(bool forced) {
  HMONITOR hMonitor = MonitorFromRect(m_inputPos, MONITOR_DEFAULTTONEAREST);
  UINT dpiX = 96, dpiY = 96;
  if (hMonitor)
    GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
  // prepare d2d1 resources
  // if style changed, or dpi changed, or pDWR NULL, re-initialize directwrite
  // resources
  if (forced || (pDWR == NULL) || (m_ostyle != m_style) || (dpiX != dpi)) {
    pDWR.reset();
    pDWR = std::make_shared<DirectWriteResources>(m_style, dpiX);
    pDWR->pRenderTarget->SetTextAntialiasMode(
        (D2D1_TEXT_ANTIALIAS_MODE)m_style.antialias_mode);
  }
  m_ostyle = m_style;
  dpi = dpiX;
  dpiScaleLayout = (float)dpi / 96.0f;
}

static HBITMAP CopyDCToBitmap(HDC hDC, LPRECT lpRect) {
  if (!hDC || !lpRect || IsRectEmpty(lpRect))
    return NULL;
  HDC hMemDC = NULL;
  HBITMAP hBitmap = NULL, hOldBitmap = NULL;
  int nX, nY, nX2, nY2;
  int nWidth, nHeight;

  nX = lpRect->left;
  nY = lpRect->top;
  nX2 = lpRect->right;
  nY2 = lpRect->bottom;
  nWidth = nX2 - nX;
  nHeight = nY2 - nY;

  hMemDC = CreateCompatibleDC(hDC);
  if (!hMemDC)
    return NULL;

  hBitmap = CreateCompatibleBitmap(hDC, nWidth, nHeight);
  if (!hBitmap) {
    DeleteDC(hMemDC);
    return NULL;
  }

  hOldBitmap = (HBITMAP)SelectObject(hMemDC, hBitmap);
  if (!hOldBitmap) {
    DeleteObject(hBitmap);
    DeleteDC(hMemDC);
    return NULL;
  }

  if (!BitBlt(hMemDC, 0, 0, nWidth, nHeight, hDC, nX, nY, SRCCOPY)) {
    // restore and cleanup
    SelectObject(hMemDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemDC);
    return NULL;
  }

  SelectObject(hMemDC, hOldBitmap);
  DeleteDC(hMemDC);
  return hBitmap;
}

void WeaselPanel::_CaptureRect(CRect& rect) {
  HDC ScreenDC = ::GetDC(NULL);
  CRect rc;
  GetWindowRect(&rc);
  POINT WindowPosAtScreen = {rc.left, rc.top};
  CRect captureRect = rect;
  captureRect.OffsetRect(WindowPosAtScreen);
  // create bitmap first (avoid holding clipboard while capturing)
  HBITMAP bmp = CopyDCToBitmap(ScreenDC, LPRECT(captureRect));
  if (!bmp) {
    ::ReleaseDC(NULL, ScreenDC);
    return;
  }

  // capture input window to clipboard
  if (!OpenClipboard()) {
    DEBUG << "_CaptureRect: OpenClipord ailed";
    DeleteObject(bmp);
    ::ReleaseDC(NULL, ScreenDC);
    return;
  }
  EmptyClipboard();
  if (!SetClipboardData(CF_BITMAP, bmp)) {
    DEBUG << "_CaptureRect: SetClipboardData failed";
    DeleteObject(bmp);
  }
  CloseClipboard();
  ::ReleaseDC(NULL, ScreenDC);
}

LRESULT WeaselPanel::OnMouseActivate(UINT uMsg,
                                     WPARAM wParam,
                                     LPARAM lParam,
                                     BOOL& bHandled) {
  bHandled = true;
  return MA_NOACTIVATE;
}

LRESULT WeaselPanel::OnMouseWheel(UINT uMsg,
                                  WPARAM wParam,
                                  LPARAM lParam,
                                  BOOL& bHandled) {
  int delta = GET_WHEEL_DELTA_WPARAM(wParam);
  PanelFrameTrace("WHEEL delta=" + std::to_string(delta) +
                  " expanded=" + std::to_string(m_style.max_height < 0));
  if (_UICallback && delta != 0) {
    bool nextpage = delta < 0;
    _UICallback(NULL, NULL, NULL, &nextpage);
  }
  bHandled = true;
  return 0;
}

LRESULT WeaselPanel::OnLeftClickedUp(UINT uMsg,
                                     WPARAM wParam,
                                     LPARAM lParam,
                                     BOOL& bHandled) {
  if (hide_candidates) {
    bHandled = true;
    return 0;
  }
  CPoint point;
  point.x = GET_X_LPARAM(lParam);
  point.y = GET_Y_LPARAM(lParam);

  ::KillTimer(m_hWnd, AUTOREV_TIMER);
  bar_scale_ = 1.0;
  ptimer = 0;
  {
    // select by click
    CRect rect = m_layout->GetCandidateRect((int)m_ctx.cinfo.highlighted);
    if (m_istorepos)
      rect.OffsetRect(0, m_offsetys[m_ctx.cinfo.highlighted]);
    rect.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                     DPI_SCALE(m_style.hilite_padding_y));
    if (rect.PtInRect(point)) {
      size_t i = m_ctx.cinfo.highlighted;
      if (_UICallback) {
        m_mouse_entry = false;
        _UICallback(&i, NULL, NULL, NULL);
        if (!m_status.composing)
          DestroyWindow();
      }
    } else {
      RedrawWindow();
    }
  }
  bHandled = true;
  return 0;
}

LRESULT WeaselPanel::OnLeftClickedDown(UINT uMsg,
                                       WPARAM wParam,
                                       LPARAM lParam,
                                       BOOL& bHandled) {
  if (hide_candidates) {
    bHandled = true;
    return 0;
  }
  CPoint point;
  point.x = GET_X_LPARAM(lParam);
  point.y = GET_Y_LPARAM(lParam);

  // capture
  if (m_style.click_to_capture) {
    CRect recth = m_layout->GetCandidateRect((int)m_ctx.cinfo.highlighted);
    if (m_istorepos)
      recth.OffsetRect(0, m_offsetys[m_ctx.cinfo.highlighted]);
    recth.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                      DPI_SCALE(m_style.hilite_padding_y));
    // capture widow
    if (recth.PtInRect(point))
      _CaptureRect(recth);
    else {
      // if shadow_color transparent, decrease the capture rectangle size
      if (COLORTRANSPARENT(m_style.shadow_color) &&
          DPI_SCALE(m_style.shadow_radius) != 0) {
        CRect crc(rcw);
        int shadow_gap = (DPI_SCALE(m_style.shadow_offset_x) == 0 &&
                          DPI_SCALE(m_style.shadow_offset_y) == 0)
                             ? 2 * DPI_SCALE(m_style.shadow_radius)
                             : DPI_SCALE(m_style.shadow_radius) +
                                   DPI_SCALE(m_style.shadow_radius) / 2;
        int ofx = DPI_SCALE(m_style.hilite_padding_x) +
                              abs(DPI_SCALE(m_style.shadow_offset_x)) +
                              shadow_gap >
                          abs(DPI_SCALE(m_style.margin_x))
                      ? DPI_SCALE(m_style.hilite_padding_x) +
                            abs(DPI_SCALE(m_style.shadow_offset_x)) +
                            shadow_gap - abs(DPI_SCALE(m_style.margin_x))
                      : 0;
        int ofy = DPI_SCALE(m_style.hilite_padding_y) +
                              abs(DPI_SCALE(m_style.shadow_offset_y)) +
                              shadow_gap >
                          abs(DPI_SCALE(m_style.margin_y))
                      ? DPI_SCALE(m_style.hilite_padding_y) +
                            abs(DPI_SCALE(m_style.shadow_offset_y)) +
                            shadow_gap - abs(DPI_SCALE(m_style.margin_y))
                      : 0;
        crc.DeflateRect(m_layout->offsetX - ofx, m_layout->offsetY - ofy);
        _CaptureRect(crc);
      } else {
        _CaptureRect(rcw);
      }
    }
  }
  {
    if (!m_style.inline_preedit && m_candidateCount != 0 &&
        COLORNOTTRANSPARENT(m_style.prevpage_color) &&
        COLORNOTTRANSPARENT(m_style.nextpage_color)) {
      // click prepage
      if (m_ctx.cinfo.currentPage != 0) {
        CRect prc = m_layout->GetPrepageRect();
        if (m_istorepos)
          prc.OffsetRect(0, m_offsety_preedit);
        if (prc.PtInRect(point)) {
          bool nextPage = false;
          if (_UICallback)
            _UICallback(NULL, NULL, &nextPage, NULL);
          bHandled = true;
          return 0;
        }
      }
      // click nextpage
      if (!m_ctx.cinfo.is_last_page) {
        CRect prc = m_layout->GetNextpageRect();
        if (m_istorepos)
          prc.OffsetRect(0, m_offsety_preedit);
        if (prc.PtInRect(point)) {
          bool nextPage = true;
          if (_UICallback)
            _UICallback(NULL, NULL, &nextPage, NULL);
          bHandled = true;
          return 0;
        }
      }
    }
    // select by click relative actions
    for (size_t i = 0; i < m_candidateCount && i < MAX_CANDIDATES_COUNT; ++i) {
      CRect rect = m_layout->GetCandidateRect((int)i);
      if (m_istorepos)
        rect.OffsetRect(0, m_offsetys[i]);
      rect.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                       DPI_SCALE(m_style.hilite_padding_y));
      if (rect.PtInRect(point)) {
        bar_scale_ = 0.8f;
        // modify highlighted
        if (i != m_ctx.cinfo.highlighted) {
          if (_UICallback)
            _UICallback(NULL, &i, NULL, NULL);
        } else {
          RedrawWindow();
        }
        ptimer = UINT_PTR(this);
        ::SetTimer(m_hWnd, AUTOREV_TIMER, 1000, &WeaselPanel::OnTimer);
        bHandled = true;
        return 0;
      }
    }
  }
  bHandled = true;
  return 0;
}

LRESULT WeaselPanel::OnRightClickedUp(UINT uMsg,
                                      WPARAM wParam,
                                      LPARAM lParam,
                                      BOOL& bHandled) {
  if (hide_candidates) {
    bHandled = true;
    return 0;
  }
  CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
  PanelFrameTrace("RIGHT_CLICK msg=" + std::to_string(uMsg) +
                  " x=" + std::to_string(point.x) +
                  " y=" + std::to_string(point.y) +
                  " candidates=" + std::to_string(m_ctx.cinfo.candies.size()));

  _SyncJapaneseReadingCache();
  bool reset = m_readingToggleCandidates.size() != m_ctx.cinfo.candies.size();
  if (!reset) {
    for (size_t i = 0; i < m_ctx.cinfo.candies.size(); ++i) {
      if (m_readingToggleCandidates[i] != m_ctx.cinfo.candies[i].str) {
        reset = true;
        break;
      }
    }
  }
  if (reset) {
    m_readingToggleCandidates.clear();
    m_originalComments.clear();
    m_originalCommentMetadata.clear();
    m_japaneseReadings.clear();
    m_lastAppliedReadingComments.assign(m_ctx.cinfo.candies.size(), L"");
    m_candidateLanguages.clear();
    m_readingShown.assign(m_ctx.cinfo.candies.size(), false);
    for (size_t i = 0; i < m_ctx.cinfo.candies.size(); ++i) {
      m_readingToggleCandidates.push_back(m_ctx.cinfo.candies[i].str);
      m_originalComments.push_back(
          i < m_ctx.cinfo.comments.size()
              ? StripDisplayedTranslationPrefixes(m_ctx.cinfo.comments[i].str)
              : L"");
      m_originalCommentMetadata.push_back(i<m_ctx.cinfo.comments.size()?ReadingPreviewMetadata(m_ctx.cinfo.comments[i].str):L"");
      m_japaneseReadings.push_back(
          i < m_ctx.cinfo.comments.size()
              ? HiddenJapaneseReading(m_ctx.cinfo.comments[i].str)
              : L"");
      const CandidateLanguage language =
          i < m_ctx.cinfo.comments.size()
              ? CandidateLanguageFromTextAndComment(
                    m_ctx.cinfo.candies[i].str, m_ctx.cinfo.comments[i].str)
              : CandidateLanguage::Other;
      m_candidateLanguages.push_back(static_cast<int>(language));
    }
  }

  for (size_t i = 0; i < m_candidateCount && i < MAX_CANDIDATES_COUNT; ++i) {
    CRect rect = m_layout->GetCandidateRect((int)i);
    if (m_istorepos) rect.OffsetRect(0, m_offsetys[i]);
    rect.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                     DPI_SCALE(m_style.hilite_padding_y));
    if (!rect.PtInRect(point)) continue;
    const std::wstring reading =
        i < m_japaneseReadings.size() ? m_japaneseReadings[i] : L"";
    if (reading.empty()) {
      PanelFrameTrace("RIGHT_CLICK hit=" + std::to_string(i) +
                      " reading=empty");
      break;
    }
    PanelFrameTrace("RIGHT_CLICK hit=" + std::to_string(i) +
                    " reading=present");
    m_readingShown[i] = !m_readingShown[i];
    PanelFrameTrace("RIGHT_CLICK toggled=" +
                    std::to_string(m_readingShown[i] ? 1 : 0));
    _ApplyJapaneseReadingDisplay();
    Refresh();
    break;
  }
  bHandled = true;
  return 0;
}

void WeaselPanel::_SyncJapaneseReadingCache() {
  bool reset = m_readingToggleCandidates.size() != m_ctx.cinfo.candies.size();
  if (!reset) {
    for (size_t i = 0; i < m_ctx.cinfo.candies.size(); ++i) {
      if (m_readingToggleCandidates[i] != m_ctx.cinfo.candies[i].str) {
        reset = true;
        break;
      }
    }
  }
  if (reset) {
    m_readingToggleCandidates.clear();
    m_originalComments.clear();
    m_originalCommentMetadata.clear();
    m_japaneseReadings.clear();
    m_lastAppliedReadingComments.assign(m_ctx.cinfo.candies.size(), L"");
    m_candidateLanguages.clear();
    m_readingShown.assign(m_ctx.cinfo.candies.size(), false);
    for (size_t i = 0; i < m_ctx.cinfo.candies.size(); ++i) {
      m_readingToggleCandidates.push_back(m_ctx.cinfo.candies[i].str);
      const std::wstring comment =
          i < m_ctx.cinfo.comments.size() ? m_ctx.cinfo.comments[i].str : L"";
      m_originalComments.push_back(StripDisplayedTranslationPrefixes(comment));
      m_originalCommentMetadata.push_back(ReadingPreviewMetadata(comment));
      m_japaneseReadings.push_back(HiddenJapaneseReading(comment));
      m_candidateLanguages.push_back(
          static_cast<int>(CandidateLanguageFromTextAndComment(
              m_ctx.cinfo.candies[i].str, comment)));
    }
    return;
  }

  // A server refresh can replace the source comment while the candidate list
  // stays unchanged.  Never feed our own temporary reading projection back
  // into m_originalComments: doing so made key release restore the reading
  // text instead of the original Japanese translation.
  for (size_t i = 0; i < m_ctx.cinfo.comments.size() &&
                     i < m_japaneseReadings.size(); ++i) {
    const std::wstring& current = m_ctx.cinfo.comments[i].str;
    if (i < m_lastAppliedReadingComments.size() &&
        !m_lastAppliedReadingComments[i].empty() &&
        current == m_lastAppliedReadingComments[i])
      continue;
    m_japaneseReadings[i] = HiddenJapaneseReading(current);
    m_originalComments[i] = StripDisplayedTranslationPrefixes(current);
    m_originalCommentMetadata[i] = ReadingPreviewMetadata(current);
    const CandidateLanguage language = CandidateLanguageFromTextAndComment(
        m_ctx.cinfo.candies[i].str, current);
    if (language != CandidateLanguage::Other && i < m_candidateLanguages.size())
      m_candidateLanguages[i] = static_cast<int>(language);
  }
}

void WeaselPanel::_ApplyJapaneseReadingDisplay() {
  _SyncJapaneseReadingCache();
  if (m_ctx.cinfo.comments.size() < m_ctx.cinfo.candies.size())
    m_ctx.cinfo.comments.resize(m_ctx.cinfo.candies.size());
  if (m_lastAppliedReadingComments.size() < m_ctx.cinfo.candies.size())
    m_lastAppliedReadingComments.resize(m_ctx.cinfo.candies.size());
  size_t original_count = 0;
  size_t reading_count = 0;
  for (size_t i = 0; i < m_ctx.cinfo.candies.size() &&
                     i < m_originalComments.size(); ++i) {
    const bool show = m_shiftReadingPreview ||
                      (i < m_readingShown.size() && m_readingShown[i]);
    std::wstring displayed =
        show && !m_japaneseReadings[i].empty()
            ? CommentWithJapaneseReading(m_originalComments[i],
                                         m_japaneseReadings[i])
            : m_originalComments[i];
    // Keep the reading as hidden transport metadata in both display modes.
    // HorizontalLayout measures it together with the ordinary annotation, so
    // holding Shift changes only the painted text and never candidate widths.
    if (!m_japaneseReadings[i].empty())
      displayed += L"[[RIME_JR:" + m_japaneseReadings[i] + L"]]";
    if(i<m_originalCommentMetadata.size())
      displayed+=m_originalCommentMetadata[i];
    m_ctx.cinfo.comments[i].str = displayed;
    m_lastAppliedReadingComments[i] = displayed;
    const std::wstring visible = StripDisplayedTranslationPrefixes(displayed);
    if (visible == m_originalComments[i]) ++original_count;
    if (!m_japaneseReadings[i].empty() &&
        visible == CommentWithJapaneseReading(m_originalComments[i],
                                              m_japaneseReadings[i]))
      ++reading_count;
  }
  PanelFrameTrace("PREVIEW applied active=" +
                  std::to_string(m_shiftReadingPreview ? 1 : 0) +
                  " originals=" + std::to_string(original_count) +
                  " readings=" + std::to_string(reading_count));
}

void WeaselPanel::SetJapaneseReadingPreview(bool active) {
  // Keyboard auto-repeat can call this dozens of times while Space is held.
  // Re-arming the timer on every repeat starves the physical key-release poll
  // and, in an expanded grid, makes the display alternate with context frames.
  // Only a real state transition may touch the timer or redraw the panel.
  if (m_shiftReadingPreview == active) return;
  // Several TSF hosts deliver modifier/Space key-down but omit the matching
  // key-up callback.  Poll the physical keys only after a preview has begun;
  // this makes the state truly momentary without touching normal rendering.
  if (active) {
    if (!m_shiftReadingPreview) m_readingPreviewStarted = ::GetTickCount64();
    // A normal WM_TIMER message is more reliable here than a TIMERPROC
    // callback in TSF hosts which filter callback dispatch.
    ::SetTimer(m_hWnd, READING_PREVIEW_TIMER, 35, nullptr);
  } else {
    ::KillTimer(m_hWnd, READING_PREVIEW_TIMER);
  }
  PanelFrameTrace("PREVIEW set active=" + std::to_string(active ? 1 : 0) +
                  " hwnd=" +
                  std::to_string(reinterpret_cast<uintptr_t>(m_hWnd)));
  m_shiftReadingPreview = active;
  _ApplyJapaneseReadingDisplay();
  Refresh();
}

UINT_PTR WeaselPanel::ptimer = 0;
VOID CALLBACK WeaselPanel::OnTimer(_In_ HWND hwnd,
                                   _In_ UINT uMsg,
                                   _In_ UINT_PTR idEvent,
                                   _In_ DWORD dwTime) {
  ::KillTimer(hwnd, idEvent);
  WeaselPanel* self = (WeaselPanel*)ptimer;
  ptimer = 0;
  if (self) {
    self->bar_scale_ = 1.0;
    self->RedrawWindow();
  }
}

LRESULT WeaselPanel::OnPreviewTimer(UINT,
                                    WPARAM wParam,
                                    LPARAM,
                                    BOOL& bHandled) {
  if (wParam == HOT_ANNOTATION_TIMER) {
    DWORD generation = 0;
    DWORD size = sizeof(generation);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod",
                     L"AnnotationSettingsGeneration", RRF_RT_REG_DWORD,
                     nullptr, &generation, &size) == ERROR_SUCCESS &&
        generation != m_seenAnnotationGeneration) {
      m_seenAnnotationGeneration = generation;
      if (_hotAnnotationCallback) _hotAnnotationCallback();
      Refresh(); // Registry-only layout options also require a new row map.
    }
    bHandled = TRUE;
    return 0;
  }
  if (wParam == COMMENT_MARQUEE_TIMER) {
    if (!m_style.hover_comment_scroll || m_marqueeIndex < 0 ||
        m_marqueeIndex >= static_cast<int>(m_candidateCount)) {
      ::KillTimer(m_hWnd, COMMENT_MARQUEE_TIMER);
      m_marqueeOffset = 0.0f;
      m_marqueeMaxOffset = 0.0f;
    } else if (::GetTickCount64() - m_marqueeHoverStarted >= 650) {
      // The first repaint after the delay measures the full, unellipsized
      // annotation and fills m_marqueeMaxOffset.  Later ticks advance it.
      if (m_marqueeMaxOffset > 0.0f) {
        m_marqueeOffset += max(1.0f, static_cast<float>(DPI_SCALE(1)));
        if (m_marqueeOffset > m_marqueeMaxOffset + DPI_SCALE(28)) {
          m_marqueeOffset = 0.0f;
          m_marqueeHoverStarted = ::GetTickCount64();
        }
      }
      InvalidateRect(&rcw, FALSE);
    }
    bHandled = TRUE;
    return 0;
  }
  if (wParam != READING_PREVIEW_TIMER) {
    bHandled = FALSE;
    return 0;
  }
  const bool shift_held =
      (::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 ||
      (::GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0 ||
      (::GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
  const bool space_held = (::GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
  const bool alt_held =
      (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0 ||
      (::GetAsyncKeyState(VK_LMENU) & 0x8000) != 0 ||
      (::GetAsyncKeyState(VK_RMENU) & 0x8000) != 0;
  const bool alt_polling = IsAltReadingPreviewPollingEnabled() &&
                           !m_ctx.cinfo.candies.empty();

  // Do not depend on the host forwarding Alt through ITfKeyEventSink.  The
  // panel is already alive when candidates exist, so the physical state is
  // the reliable cross-application signal.
  if (alt_polling && alt_held && !m_shiftReadingPreview) {
    m_altPreviewReleasePending = true;
    m_altPreviewChordObserved = false;
    SetJapaneseReadingPreview(true);
    bHandled = TRUE;
    return 0;
  }

  const ULONGLONG elapsed = ::GetTickCount64() - m_readingPreviewStarted;
  if (m_shiftReadingPreview || alt_held) {
    PanelFrameTrace("PREVIEW timer elapsed=" + std::to_string(elapsed) +
                    " shift=" + std::to_string(shift_held ? 1 : 0) +
                    " space=" + std::to_string(space_held ? 1 : 0) +
                    " alt=" + std::to_string(alt_held ? 1 : 0));
  }
  if (elapsed >= 120 && !shift_held && !space_held && !alt_held)
    SetJapaneseReadingPreview(false);
  else if (!alt_polling && !m_shiftReadingPreview)
    ::KillTimer(m_hWnd, READING_PREVIEW_TIMER);
  bHandled = TRUE;
  return 0;
}

LRESULT WeaselPanel::OnMouseMove(UINT uMsg,
                                 WPARAM wParam,
                                 LPARAM lParam,
                                 BOOL& bHandled) {
  if (m_mouse_entry == false) {
    TRACKMOUSEEVENT tme;
    tme.cbSize = sizeof(TRACKMOUSEEVENT);
    tme.dwFlags = TME_LEAVE;
    tme.dwHoverTime = 20;  // unit: ms
    tme.hwndTrack = m_hWnd;
    TrackMouseEvent(&tme);
  }
  bHandled = true;
  m_mouse_entry = true;
  CPoint point;
  point.x = GET_X_LPARAM(lParam);
  point.y = GET_Y_LPARAM(lParam);

  // Ignore if mouse screen position not changed
  CPoint ptScreen = point;
  ClientToScreen(&ptScreen);
  if (ptScreen == m_lastMousePos) {
    return 0;
  }
  m_lastMousePos = ptScreen;

  int marquee_index = -1;
  CRect hovered_candidate_rect;
  for (size_t i = 0; i < m_candidateCount && i < MAX_CANDIDATES_COUNT; ++i) {
    CRect rect = m_layout->GetCandidateRect((int)i);
    if (m_istorepos)
      rect.OffsetRect(0, m_offsetys[i]);
    const CRect candidate_rect = rect;
    rect.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                     DPI_SCALE(m_style.hilite_padding_y));
    if (rect.PtInRect(point)) {
      // Ordinary short annotations already fit in the candidate and must not
      // open a redundant hover card.  Both hover modes are reserved for a
      // line that is genuinely ellipsized by the compact layout.
      const bool comment_truncated = _IsCommentTruncated(i);
      PanelFrameTrace("HOVER hit=" + std::to_string(i) +
                      " truncated=" +
                      std::to_string(comment_truncated ? 1 : 0) +
                      " x=" + std::to_string(point.x) +
                      " y=" + std::to_string(point.y));
      if (comment_truncated) {
        marquee_index = static_cast<int>(i);
        hovered_candidate_rect = candidate_rect;
      }
      if (i != m_ctx.cinfo.highlighted) {
        if (m_style.hover_type == UIStyle::HoverType::HILITE) {
          if (_UICallback)
            _UICallback(NULL, &i, NULL, NULL);
        } else if (m_hoverIndex != i) {
          m_hoverIndex = static_cast<int>(i);
          InvalidateRect(&rcw, true);
        }
      } else if (m_style.hover_type == UIStyle::HoverType::SEMI_HILITE &&
                 m_hoverIndex != -1) {
        m_hoverIndex = -1;
        InvalidateRect(&rcw, true);
      }
      break;
    }
  }
  if (marquee_index != m_marqueeIndex) {
    _HideCommentTooltip();
    m_marqueeIndex = marquee_index;
    m_marqueeOffset = 0.0f;
    m_marqueeMaxOffset = 0.0f;
    m_marqueeHoverStarted = ::GetTickCount64();
    if (m_marqueeIndex >= 0 && m_style.hover_comment_scroll) {
      ::SetTimer(m_hWnd, COMMENT_MARQUEE_TIMER, 40, nullptr);
    } else {
      ::KillTimer(m_hWnd, COMMENT_MARQUEE_TIMER);
      if (m_marqueeIndex >= 0)
        _ShowCommentTooltip(static_cast<size_t>(m_marqueeIndex),
                            hovered_candidate_rect);
    }
    InvalidateRect(&rcw, FALSE);
  }
  return 0;
}

bool WeaselPanel::_IsCommentTruncated(size_t index) {
  if (m_layout == nullptr || index >= m_ctx.cinfo.comments.size() ||
      pDWR == nullptr || pDWR->pCommentTextFormat.Get() == nullptr)
    return false;
  // Expanded comments that participate in cell width are already fully
  // visible.  Every other layout follows the compact ellipsis path.
  if (m_style.max_height < 0 && m_style.expanded_comment_width)
    return false;
  const std::wstring comment =
      StripDisplayedTranslationPrefixes(m_ctx.cinfo.comments[index].str);
  if (comment.empty()) return false;
  CRect rect = m_layout->GetCandidateCommentRect(static_cast<int>(index));
  if (m_istorepos) rect.OffsetRect(0, m_offsetys[index]);
  const float widest =
      WidestCommentLine(comment, pDWR->pCommentTextFormat.Get(), pDWR);
  PanelFrameTrace("HOVER_MEASURE index=" + std::to_string(index) +
                  " width=" + std::to_string(rect.Width()) +
                  " text=" + std::to_string(widest));
  return rect.Width() > 0 && widest > static_cast<float>(rect.Width());
}

void WeaselPanel::_CreateCommentTooltip() {
  if (m_commentTooltip != nullptr) return;
  m_commentTooltip = ::CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
      L"STATIC", nullptr,
      WS_POPUP | WS_BORDER | SS_LEFT | SS_NOPREFIX,
      CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
      m_hWnd, nullptr, ::GetModuleHandleW(nullptr), nullptr);
  if (m_commentTooltip == nullptr) return;
  NONCLIENTMETRICSW metrics = {sizeof(metrics)};
  if (::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
                              &metrics, 0)) {
    metrics.lfStatusFont.lfHeight = -DPI_SCALE(max(12, m_style.comment_font_point));
    m_commentTooltipFont = ::CreateFontIndirectW(&metrics.lfStatusFont);
    if (m_commentTooltipFont != nullptr)
      ::SendMessageW(m_commentTooltip, WM_SETFONT,
                     reinterpret_cast<WPARAM>(m_commentTooltipFont), FALSE);
  }
}

void WeaselPanel::_ShowCommentTooltip(size_t index,
                                      const CRect& candidate_rect) {
  if (m_style.hover_comment_scroll || index >= m_ctx.cinfo.candies.size() ||
      index >= m_ctx.cinfo.comments.size() || !_IsCommentTruncated(index)) {
    _HideCommentTooltip();
    return;
  }
  _CreateCommentTooltip();
  if (m_commentTooltip == nullptr) return;

  const std::wstring comment =
      StripDisplayedTranslationPrefixes(m_ctx.cinfo.comments[index].str);
  // The annotation filter stores English first and Japanese second.  Keeping
  // its original line breaks produces the requested Chinese / English /
  // Japanese hover card without shortening either translation.
  m_commentTooltipText = m_ctx.cinfo.candies[index].str + L"\n" + comment;
  ::SetWindowTextW(m_commentTooltip, m_commentTooltipText.c_str());

  const int padding_x = DPI_SCALE(12);
  const int padding_y = DPI_SCALE(10);
  RECT text_rect = {0, 0, DPI_SCALE(640), 0};
  HDC dc = ::GetDC(m_commentTooltip);
  HGDIOBJ previous_font = nullptr;
  if (dc != nullptr) {
    if (m_commentTooltipFont != nullptr)
      previous_font = ::SelectObject(dc, m_commentTooltipFont);
    ::DrawTextW(dc, m_commentTooltipText.c_str(),
                static_cast<int>(m_commentTooltipText.size()), &text_rect,
                DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    if (previous_font != nullptr) ::SelectObject(dc, previous_font);
    ::ReleaseDC(m_commentTooltip, dc);
  }
  const int popup_width = min(DPI_SCALE(664),
                              max(DPI_SCALE(220), text_rect.right + padding_x * 2));
  const int popup_height = max(DPI_SCALE(72), text_rect.bottom + padding_y * 2);

  CPoint anchor(candidate_rect.left, candidate_rect.bottom + DPI_SCALE(8));
  ClientToScreen(&anchor);
  MONITORINFO monitor_info = {sizeof(monitor_info)};
  HMONITOR monitor = ::MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
  if (::GetMonitorInfoW(monitor, &monitor_info)) {
    anchor.x = max(monitor_info.rcWork.left + DPI_SCALE(8),
                   min(anchor.x, monitor_info.rcWork.right - popup_width -
                                     DPI_SCALE(8)));
    // If there is little room below the candidate, place the card above it.
    if (anchor.y + popup_height > monitor_info.rcWork.bottom) {
      CPoint above(candidate_rect.left,
                   candidate_rect.top - popup_height - DPI_SCALE(8));
      ClientToScreen(&above);
      anchor.y = max(monitor_info.rcWork.top + DPI_SCALE(8), above.y);
    }
  }
  ::SetWindowPos(m_commentTooltip, HWND_TOPMOST, anchor.x, anchor.y,
                 popup_width, popup_height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
  ::RedrawWindow(m_commentTooltip, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
}

void WeaselPanel::_HideCommentTooltip() {
  if (m_commentTooltip == nullptr) return;
  ::ShowWindow(m_commentTooltip, SW_HIDE);
}

LRESULT WeaselPanel::OnMouseLeave(UINT uMsg,
                                  WPARAM wParam,
                                  LPARAM lParam,
                                  BOOL& bHandled) {
  m_hoverIndex = -1;
  m_marqueeIndex = -1;
  m_marqueeOffset = 0.0f;
  m_marqueeMaxOffset = 0.0f;
  _HideCommentTooltip();
  ::KillTimer(m_hWnd, COMMENT_MARQUEE_TIMER);
  InvalidateRect(&rcw, true);
  m_mouse_entry = false;
  return 0;
}

void WeaselPanel::_HighlightText(CDCHandle& dc,
                                 const CRect& rc,
                                 const COLORREF& color,
                                 const COLORREF& shadowColor,
                                 const int& radius,
                                 const BackType& type = BackType::TEXT,
                                 const IsToRoundStruct& rd = IsToRoundStruct(),
                                 const COLORREF& bordercolor = TRANS_COLOR) {
  // Graphics obj with SmoothingMode
  Gdiplus::Graphics g_back(dc);
  g_back.SetSmoothingMode(Gdiplus::SmoothingMode::SmoothingModeHighQuality);

  // blur buffer
  int blurMarginX = m_layout->offsetX;
  int blurMarginY = m_layout->offsetY;

  GraphicsRoundRectPath* hiliteBackPath;
  if (rd.Hemispherical && type != BackType::BACKGROUND &&
      NOT_FULLSCREENLAYOUT(m_style))
    hiliteBackPath = new GraphicsRoundRectPath(
        rc,
        DPI_SCALE(m_style.round_corner_ex) -
            (DPI_SCALE(m_style.border) % 2 ? DPI_SCALE(m_style.border) / 2 : 0),
        rd.IsTopLeftNeedToRound, rd.IsTopRightNeedToRound,
        rd.IsBottomRightNeedToRound, rd.IsBottomLeftNeedToRound);
  else  // background or current candidate background not out of window
        // background
    hiliteBackPath = new GraphicsRoundRectPath(rc, radius);

  // 必须shadow_color都是非完全透明色才做绘制, 全屏状态不绘制阴影保证响应速度
  if (DPI_SCALE(m_style.shadow_radius) && COLORNOTTRANSPARENT(shadowColor) &&
      NOT_FULLSCREENLAYOUT(m_style)) {
    CRect rect(blurMarginX + DPI_SCALE(m_style.shadow_offset_x),
               blurMarginY + DPI_SCALE(m_style.shadow_offset_y),
               rc.Width() + blurMarginX + DPI_SCALE(m_style.shadow_offset_x),
               rc.Height() + blurMarginY + DPI_SCALE(m_style.shadow_offset_y));
    BYTE r = GetRValue(shadowColor);
    BYTE g = GetGValue(shadowColor);
    BYTE b = GetBValue(shadowColor);
    BYTE alpha = (BYTE)((shadowColor >> 24) & 255);
    Gdiplus::Color shadow_color = Gdiplus::Color::MakeARGB(alpha, r, g, b);
    static Gdiplus::Bitmap* pBitmapDropShadow;
    pBitmapDropShadow = new Gdiplus::Bitmap((INT)rc.Width() + blurMarginX * 2,
                                            (INT)rc.Height() + blurMarginY * 2,
                                            PixelFormat32bppPARGB);

    Gdiplus::Graphics g_shadow(pBitmapDropShadow);
    g_shadow.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    // dropshadow, draw a roundrectangle to blur
    if (DPI_SCALE(m_style.shadow_offset_x) != 0 ||
        DPI_SCALE(m_style.shadow_offset_y) != 0) {
      GraphicsRoundRectPath shadow_path(rect, radius);
      Gdiplus::SolidBrush shadow_brush(shadow_color);
      g_shadow.FillPath(&shadow_brush, &shadow_path);
    }
    // round shadow, draw multilines as base round line
    else {
      int step = alpha / DPI_SCALE(m_style.shadow_radius) / 2;
      Gdiplus::Pen pen_shadow(shadow_color, (Gdiplus::REAL)1);
      for (int i = 0; i < DPI_SCALE(m_style.shadow_radius); i++) {
        GraphicsRoundRectPath round_path(rect, radius + 1 + i);
        g_shadow.DrawPath(&pen_shadow, &round_path);
        shadow_color = Gdiplus::Color::MakeARGB(alpha - i * step, r, g, b);
        pen_shadow.SetColor(shadow_color);
        rect.InflateRect(1, 1);
      }
    }
    DoGaussianBlur(pBitmapDropShadow, (float)DPI_SCALE(m_style.shadow_radius),
                   (float)DPI_SCALE(m_style.shadow_radius));

    g_back.DrawImage(pBitmapDropShadow, rc.left - blurMarginX,
                     rc.top - blurMarginY);

    // free memory
    delete pBitmapDropShadow;
    pBitmapDropShadow = NULL;
  }

  // 必须back_color非完全透明才绘制
  if (COLORNOTTRANSPARENT(color)) {
    Gdiplus::Color back_color = GDPCOLOR_FROM_COLORREF(color);
    Gdiplus::SolidBrush back_brush(back_color);
    g_back.FillPath(&back_brush, hiliteBackPath);
  }
  // draw border, for bordercolor not transparent and border valid
  if (COLORNOTTRANSPARENT(bordercolor) && DPI_SCALE(m_style.border) > 0) {
    Gdiplus::Color border_color = GDPCOLOR_FROM_COLORREF(bordercolor);
    Gdiplus::Pen gPenBorder(border_color,
                            (Gdiplus::REAL)DPI_SCALE(m_style.border));
    // candidate window border
    if (type == BackType::BACKGROUND) {
      GraphicsRoundRectPath bgPath(rc, DPI_SCALE(m_style.round_corner_ex));
      g_back.DrawPath(&gPenBorder, &bgPath);
    } else if (type !=
               BackType::TEXT)  // hilited_candidate_border / candidate_border
      g_back.DrawPath(&gPenBorder, hiliteBackPath);
  }
  // free memory
  delete hiliteBackPath;
  hiliteBackPath = NULL;
}

// draw preedit text, text only
bool WeaselPanel::_DrawPreedit(const Text& text,
                               CDCHandle dc,
                               const CRect& rc) {
  bool drawn = false;
  OwnedPaintTrace(m_ctx.preedit.str,m_style.max_height,L"BEGIN transport="+std::to_wstring(m_style.candidate_language_split),true);
  std::wstring const& t = text.str;
  IDWriteTextFormat1* txtFormat = pDWR->pPreeditTextFormat.Get();

  if (!t.empty()) {
    weasel::TextRange range = m_layout->GetPreeditRange();

    if (range.start < range.end) {
      std::wstring before_str = t.substr(0, range.start);
      std::wstring hilited_str = t.substr(range.start, range.end);
      std::wstring after_str = t.substr(range.end);
      CSize beforeSz = m_layout->GetBeforeSize();
      CSize hilitedSz = m_layout->GetHilitedSize();
      CSize afterSz = m_layout->GetAfterSize();

      int x = rc.left;
      int y = rc.top;

      if (range.start > 0) {
        // zzz
        std::wstring str_before(t.substr(0, range.start));
        CRect rc_before;
        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
          rc_before = CRect(rc.left, y, rc.right, y + beforeSz.cy);
        else
          rc_before = CRect(x, rc.top, rc.left + beforeSz.cx, rc.bottom);
        _TextOut(rc_before, str_before.c_str(), str_before.length(),
                 m_style.text_color, txtFormat);
        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
          y += beforeSz.cy + DPI_SCALE(m_style.hilite_spacing);
        else
          x += beforeSz.cx + DPI_SCALE(m_style.hilite_spacing);
      }
      {
        // zzz[yyy]
        std::wstring str_highlight(
            t.substr(range.start, (size_t)range.end - range.start));
        CRect rc_hi;

        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
          rc_hi = CRect(rc.left, y, rc.right, y + hilitedSz.cy);
        else
          rc_hi = CRect(x, rc.top, x + hilitedSz.cx, rc.bottom);
        _TextOut(rc_hi, str_highlight.c_str(), str_highlight.length(),
                 m_style.hilited_text_color, txtFormat);
        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
          y += rc_hi.Height() + DPI_SCALE(m_style.hilite_spacing);
        else
          x += rc_hi.Width() + DPI_SCALE(m_style.hilite_spacing);
      }
      if (range.end < static_cast<int>(t.length())) {
        // zzz[yyy]xxx
        std::wstring str_after(t.substr(range.end));
        CRect rc_after;
        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
          rc_after = CRect(rc.left, y, rc.right, y + afterSz.cy);
        else
          rc_after = CRect(x, rc.top, x + afterSz.cx, rc.bottom);
        _TextOut(rc_after, str_after.c_str(), str_after.length(),
                 m_style.text_color, txtFormat);
      }
    } else {
      CRect rcText(rc.left, rc.top, rc.right, rc.bottom);
      _TextOut(rcText, t.c_str(), t.length(), m_style.text_color, txtFormat);
    }
    // draw pager mark if not inline_preedit if necessary
    if (m_candidateCount && !m_style.inline_preedit &&
        COLORNOTTRANSPARENT(m_style.prevpage_color) &&
        COLORNOTTRANSPARENT(m_style.nextpage_color)) {
      const std::wstring pre = L"<";
      const std::wstring next = L">";
      CRect prc = m_layout->GetPrepageRect();
      // clickable color / disabled color
      int color =
          m_ctx.cinfo.currentPage ? m_style.prevpage_color : m_style.text_color;
      if (m_istorepos)
        prc.OffsetRect(0, m_offsety_preedit);
      _TextOut(prc, pre.c_str(), pre.length(), color, txtFormat);

      CRect nrc = m_layout->GetNextpageRect();
      // clickable color / disabled color
      color = m_ctx.cinfo.is_last_page ? m_style.text_color
                                       : m_style.nextpage_color;
      if (m_istorepos)
        nrc.OffsetRect(0, m_offsety_preedit);
      _TextOut(nrc, next.c_str(), next.length(), color, txtFormat);
    }
    drawn = true;
  }
  return drawn;
}

// draw hilited back color, back only
bool WeaselPanel::_DrawPreeditBack(const Text& text,
                                   CDCHandle dc,
                                   const CRect& rc) {
  bool drawn = false;
  std::wstring const& t = text.str;
  IDWriteTextFormat1* txtFormat = pDWR->pPreeditTextFormat.Get();

  if (!t.empty()) {
    weasel::TextRange range = m_layout->GetPreeditRange();

    if (range.start < range.end) {
      CSize beforeSz = m_layout->GetBeforeSize();
      CSize hilitedSz = m_layout->GetHilitedSize();

      int x = rc.left;
      int y = rc.top;

      if (range.start > 0) {
        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
          y += beforeSz.cy + DPI_SCALE(m_style.hilite_spacing);
        else
          x += beforeSz.cx + DPI_SCALE(m_style.hilite_spacing);
      }
      {
        CRect rc_hi;
        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
          rc_hi = CRect(rc.left, y, rc.right, y + hilitedSz.cy);
        else
          rc_hi = CRect(x, rc.top, x + hilitedSz.cx, rc.bottom);
        // if preedit rect size smaller than icon, fill the gap to
        // STATUS_ICON_SIZE
        if (m_layout->ShouldDisplayStatusIcon()) {
          if ((m_style.layout_type == UIStyle::LAYOUT_HORIZONTAL ||
               m_style.layout_type == UIStyle::LAYOUT_VERTICAL) &&
              hilitedSz.cy < STATUS_ICON_SIZE)
            rc_hi.InflateRect(0, (STATUS_ICON_SIZE - hilitedSz.cy) / 2);
          if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT &&
              hilitedSz.cx < STATUS_ICON_SIZE)
            rc_hi.InflateRect((STATUS_ICON_SIZE - hilitedSz.cx) / 2, 0);
        }

        rc_hi.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                          DPI_SCALE(m_style.hilite_padding_y));
        IsToRoundStruct rd = m_layout->GetTextRoundInfo();
        if (m_istorepos) {
          std::swap(rd.IsTopLeftNeedToRound, rd.IsBottomLeftNeedToRound);
          std::swap(rd.IsTopRightNeedToRound, rd.IsBottomRightNeedToRound);
        }
        _HighlightText(dc, rc_hi, m_style.hilited_back_color,
                       m_style.hilited_shadow_color,
                       DPI_SCALE(m_style.round_corner), BackType::TEXT, rd);
      }
    }
    drawn = true;
  }
  return drawn;
}

bool WeaselPanel::_DrawCandidates(CDCHandle& dc, bool back) {
  bool drawn = false;
  if (!back) {
    DWORD generation = 0;
    DWORD bytes = sizeof(generation);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod",
                 L"AppearanceSettingsGeneration", RRF_RT_REG_DWORD,
                 nullptr, &generation, &bytes);
    if (generation != m_seenFuzzyColorGeneration) {
      m_seenFuzzyColorGeneration = generation;
      DWORD color = 0;
      bytes = sizeof(color);
      m_fuzzyCorrectionTextColor = static_cast<int>(0xff00ffff);
      if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod",
                       L"FuzzyCorrectionTextColor", RRF_RT_REG_DWORD,
                       nullptr, &color, &bytes) == ERROR_SUCCESS)
        m_fuzzyCorrectionTextColor = static_cast<int>(color);
      bytes=sizeof(color);
      m_reservedPhraseTextColor=static_cast<int>(0xff80de4a);
      if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\ZhongriInputMethod",
                     L"ReservedPhraseTextColor",RRF_RT_REG_DWORD,nullptr,&color,&bytes)==ERROR_SUCCESS)
        m_reservedPhraseTextColor=static_cast<int>(color);
    }
  }
  const std::vector<Text>& candidates(m_ctx.cinfo.candies);
  const std::vector<Text>& comments(m_ctx.cinfo.comments);
  const std::vector<Text>& labels(m_ctx.cinfo.labels);
  // prevent all text format nullptr
  if (pDWR->pTextFormat.Get() == nullptr &&
      pDWR->pLabelTextFormat.Get() == nullptr &&
      pDWR->pCommentTextFormat.Get() == nullptr) {
    _InitFontRes(true);
  }
  ComPtr<IDWriteTextFormat1> txtFormat = pDWR->pTextFormat;
  ComPtr<IDWriteTextFormat1> labeltxtFormat = pDWR->pLabelTextFormat;
  ComPtr<IDWriteTextFormat1> commenttxtFormat = pDWR->pCommentTextFormat;
  BackType bkType = BackType::CAND;
  // Use visible_candidates if set (truncated by max_height), otherwise use full count
  int draw_count = (m_layout->visible_candidates > 0)
                       ? m_layout->visible_candidates
                       : m_candidateCount;

  CRect rect;
  // draw back color and shadow color, with gdi+
  if (back) {
    // if candidate_shadow_color not transparent, draw candidate shadow first
    if (COLORNOTTRANSPARENT(m_style.candidate_shadow_color)) {
      for (auto i = 0; i < draw_count && i < MAX_CANDIDATES_COUNT; ++i) {
        if(m_layout->GetCandidateRect(i).IsRectEmpty())continue;
        if (i == m_ctx.cinfo.highlighted || i == m_hoverIndex)
          continue;  // draw non hilited candidates only
        rect = m_layout->GetCandidateRect((int)i);
        IsToRoundStruct rd = m_layout->GetRoundInfo(i);
        if (m_istorepos) {
          rect.OffsetRect(0, m_offsetys[i]);
          ReconfigRoundInfo(rd, i, draw_count);
        }
        rect.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                         DPI_SCALE(m_style.hilite_padding_y));
        _HighlightText(dc, rect, 0x00000000, m_style.candidate_shadow_color,
                       DPI_SCALE(m_style.round_corner), bkType, rd);
        drawn = true;
      }
    }
    // draw non highlighted candidates, without shadow
    if ((COLORNOTTRANSPARENT(m_style.candidate_back_color) ||
         COLORNOTTRANSPARENT(m_style.chinese_candidate_back_color) ||
         COLORNOTTRANSPARENT(m_style.japanese_candidate_back_color) ||
         COLORNOTTRANSPARENT(m_style.common_phrase_candidate_back_color) ||
         COLORNOTTRANSPARENT(m_style.candidate_border_color))) {
      for (auto i = 0; i < draw_count && i < MAX_CANDIDATES_COUNT; ++i) {
        if(m_layout->GetCandidateRect(i).IsRectEmpty())continue;
        if (i == m_ctx.cinfo.highlighted || i == m_hoverIndex)
          continue;
        rect = m_layout->GetCandidateRect((int)i);
        IsToRoundStruct rd = m_layout->GetRoundInfo(i);
        if (m_istorepos) {
          rect.OffsetRect(0, m_offsetys[i]);
          ReconfigRoundInfo(rd, i, draw_count);
        }
        rect.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                         DPI_SCALE(m_style.hilite_padding_y));
        int language_back_color = m_style.candidate_back_color;
        const CandidateLanguage language =
            i < m_candidateLanguages.size()
                ? static_cast<CandidateLanguage>(m_candidateLanguages[i])
                : (i < comments.size()
                       ? HiddenCandidateLanguage(comments[i].str)
                       : CandidateLanguage::Other);
        if (language == CandidateLanguage::Chinese &&
            COLORNOTTRANSPARENT(m_style.chinese_candidate_back_color))
          language_back_color = m_style.chinese_candidate_back_color;
        else if (language == CandidateLanguage::Japanese &&
                 COLORNOTTRANSPARENT(m_style.japanese_candidate_back_color))
          language_back_color = m_style.japanese_candidate_back_color;
        else if (language == CandidateLanguage::CommonPhrase &&
                 COLORNOTTRANSPARENT(m_style.common_phrase_candidate_back_color))
          language_back_color = m_style.common_phrase_candidate_back_color;
        _HighlightText(dc, rect, language_back_color, 0x00000000,
                       DPI_SCALE(m_style.round_corner), bkType, rd,
                       m_style.candidate_border_color);
        drawn = true;
      }
    }
    // draw semi-hilite background and shadow
    if (m_hoverIndex >= 0) {
      rect = m_layout->GetCandidateRect(m_hoverIndex);
      IsToRoundStruct rd = m_layout->GetRoundInfo(m_hoverIndex);
      if (m_istorepos) {
        rect.OffsetRect(0, m_offsetys[m_hoverIndex]);
        ReconfigRoundInfo(rd, m_hoverIndex, m_candidateCount);
      }
      rect.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                       DPI_SCALE(m_style.hilite_padding_y));
      _HighlightText(dc, rect,
                     HALF_ALPHA_COLOR(m_style.hilited_candidate_back_color),
                     HALF_ALPHA_COLOR(m_style.hilited_candidate_shadow_color),
                     DPI_SCALE(m_style.round_corner), bkType, rd,
                     HALF_ALPHA_COLOR(m_style.hilited_candidate_border_color));
    }
    // draw highlighted background and shadow
    {
      rect = m_layout->GetHighlightRect();
      bool markSt = bar_scale_ == 1.0 || (!m_style.mark_text.empty());
      IsToRoundStruct rd = m_layout->GetRoundInfo(m_ctx.cinfo.highlighted);
      if (m_istorepos) {
        rect.OffsetRect(0, m_offsetys[m_ctx.cinfo.highlighted]);
        ReconfigRoundInfo(rd, m_ctx.cinfo.highlighted, m_candidateCount);
      }
      rect.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                       DPI_SCALE(m_style.hilite_padding_y));
      _HighlightText(dc, rect, m_style.hilited_candidate_back_color,
                     markSt ? m_style.hilited_candidate_shadow_color : 0,
                     DPI_SCALE(m_style.round_corner), bkType, rd,
                     m_style.hilited_candidate_border_color);
      if (m_style.mark_text.empty() &&
          COLORNOTTRANSPARENT(m_style.hilited_mark_color)) {
        int height =
            min(rect.Height() - DPI_SCALE(m_style.hilite_padding_y) * 2,
                rect.Height() - DPI_SCALE(m_style.round_corner) * 2);
        int width = min(rect.Width() - DPI_SCALE(m_style.hilite_padding_x) * 2,
                        rect.Width() - DPI_SCALE(m_style.round_corner) * 2);
        width = min(width, static_cast<int>(rect.Width() * 0.618));
        height = min(height, static_cast<int>(rect.Height() * 0.618));
        if (bar_scale_ != 1.0f) {
          width = static_cast<int>(width * bar_scale_);
          height = static_cast<int>(height * bar_scale_);
        }
        Gdiplus::Graphics g_back(dc);
        g_back.SetSmoothingMode(
            Gdiplus::SmoothingMode::SmoothingModeHighQuality);
        Gdiplus::Color mark_color =
            GDPCOLOR_FROM_COLORREF(m_style.hilited_mark_color);
        Gdiplus::SolidBrush mk_brush(mark_color);
        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT) {
          int x = rect.left + (rect.Width() - width) / 2;
          CRect mkrc{x, rect.top, x + width, rect.top + m_layout->mark_height};
          GraphicsRoundRectPath mk_path(mkrc, mkrc.Height() / 2);
          g_back.FillPath(&mk_brush, &mk_path);
        } else {
          int y = rect.top + (rect.Height() - height) / 2;
          CRect mkrc{rect.left, y, rect.left + m_layout->mark_width,
                     y + height};
          GraphicsRoundRectPath mk_path(mkrc, mkrc.Width() / 2);
          g_back.FillPath(&mk_brush, &mk_path);
        }
      }
      drawn = true;
    }
  }
  // draw text with direct write
  else {
    {
      std::string trace =
          "TEXT frame=" + std::to_string(g_active_panel_frame) +
          " highlighted=" + std::to_string(m_ctx.cinfo.highlighted) +
          " maxh=" + std::to_string(m_style.max_height) +
          " draw_count=" + std::to_string(draw_count) + " items=";
      const int trace_count = min(draw_count, 18);
      for (int i = 0; i < trace_count; ++i) {
        const CRect lr = m_layout->GetCandidateLabelRect(i);
        const CRect tr = m_layout->GetCandidateTextRect(i);
        const CRect cr = m_layout->GetCandidateCommentRect(i);
        const std::wstring shown_label =
            m_style.max_height < 0
                ? m_layout->GetDisplayLabel(i)
                : m_layout->GetLabelText(labels, i,
                                         m_style.label_text_format.c_str());
        if (i) trace += ",";
        trace += std::to_string(i) + "{lab=" +
                 std::to_string(!shown_label.empty()) + ",L=" +
                 std::to_string(lr.left) + ":" +
                 std::to_string(lr.right) + ",T=" +
                 std::to_string(tr.left) + ":" +
                 std::to_string(tr.right) + ",C=" +
                 std::to_string(cr.left) + ":" +
                 std::to_string(cr.right) + "}";
      }
      PanelFrameTrace(trace);
    }
    // begin draw candidate texts
    int label_text_color, candidate_text_color, comment_text_color;
    for (auto i = 0; i < draw_count && i < MAX_CANDIDATES_COUNT; ++i) {
    if(m_layout->GetCandidateRect(i).IsRectEmpty())continue;
      if (i == m_ctx.cinfo.highlighted || i == m_hoverIndex) {
        label_text_color = m_style.hilited_label_text_color;
        candidate_text_color = m_style.hilited_candidate_text_color;
        comment_text_color = m_style.hilited_comment_text_color;
      } else {
        label_text_color = m_style.label_text_color;
        candidate_text_color = m_style.candidate_text_color;
        comment_text_color = m_style.comment_text_color;
      }
      const CandidateLanguage language =
          i < m_candidateLanguages.size()
              ? static_cast<CandidateLanguage>(m_candidateLanguages[i])
              : (i < comments.size()
                     ? HiddenCandidateLanguage(comments[i].str)
                     : CandidateLanguage::Other);
      // Semantic colour stays active even on the highlighted first candidate.
      // The highlight background still communicates selection, while Chinese,
      // Japanese and common phrases retain their independently configured text.
      if (language == CandidateLanguage::Chinese &&
          COLORNOTTRANSPARENT(m_style.chinese_candidate_text_color))
        candidate_text_color = m_style.chinese_candidate_text_color;
      else if (language == CandidateLanguage::Japanese &&
               COLORNOTTRANSPARENT(m_style.japanese_candidate_text_color))
        candidate_text_color = m_style.japanese_candidate_text_color;
      else if (language == CandidateLanguage::CommonPhrase &&
               COLORNOTTRANSPARENT(m_style.common_phrase_candidate_text_color))
        candidate_text_color = m_style.common_phrase_candidate_text_color;
      if (i == m_ctx.cinfo.highlighted) {
        PanelFrameTrace(
            "SELECTED_PALETTE index=" + std::to_string(i) + " language=" +
            std::to_string(static_cast<int>(language)) + " text_color=" +
            std::to_string(candidate_text_color) + " theme_highlight_text=" +
            std::to_string(m_style.hilited_candidate_text_color));
      }
      // Draw label
      std::wstring label;
      if (m_style.max_height < 0 || m_layout->language_split_enabled) {
        label = m_layout->GetDisplayLabel((int)i);
      } else {
        label = m_layout->GetLabelText(
            labels, (int)i, m_style.label_text_format.c_str());
      }
      if (!label.empty()) {
        rect = m_layout->GetCandidateLabelRect((int)i);
        if (m_istorepos)
          rect.OffsetRect(0, m_offsetys[i]);
        _TextOut(rect, label.c_str(), label.length(), label_text_color,
                 labeltxtFormat.Get());
      }
      // Draw text
      std::wstring text = CandidateWithCloudBadge(candidates.at(i).str, comments.at(i).str);
      if (!text.empty()) {
        rect = m_layout->GetCandidateTextRect((int)i);
        if (m_istorepos)
          rect.OffsetRect(0, m_offsetys[i]);
        // Expanded grid allocates 1..6 cells from the widest candidate or
        // annotation. Never shorten the candidate there; an ellipsis hides
        // information and also defeats the variable-span layout contract.
        if (!m_layout->language_split_enabled &&
            (m_style.max_height >= 0 || !m_style.expanded_comment_width)) {
          text = EllipsizeSingleLine(text, static_cast<float>(rect.Width()),
                                     txtFormat.Get(), pDWR);
        }
        const auto reserved_mask=ReservedPhraseMask(text,comments.at(i).str);
        _TextOut(rect, text.c_str(), text.length(), candidate_text_color,
                 txtFormat.Get(),0.0f,0.0f,&reserved_mask,m_reservedPhraseTextColor);
      }
      // Draw comment
      std::wstring comment =
          WithoutCloudBadgeLine(StripDisplayedTranslationPrefixes(comments.at(i).str));
      if (!comment.empty() && COLORNOTTRANSPARENT(comment_text_color)) {
        rect = m_layout->GetCandidateCommentRect((int)i);
        if (m_istorepos)
          rect.OffsetRect(0, m_offsetys[i]);
        const bool compact_comment =
            !m_layout->language_split_enabled &&
            (m_style.max_height >= 0 || !m_style.expanded_comment_width);
        bool marquee_comment = false;
        float marquee_layout_width = 0.0f;
        if (m_style.hover_comment_scroll && compact_comment &&
            static_cast<int>(i) == m_marqueeIndex &&
            ::GetTickCount64() - m_marqueeHoverStarted >= 650) {
          const float widest_line = WidestCommentLine(
              comment, commenttxtFormat.Get(), pDWR);
          if (widest_line > static_cast<float>(rect.Width())) {
            marquee_comment = true;
            marquee_layout_width = widest_line + DPI_SCALE(4);
            m_marqueeMaxOffset =
                widest_line - static_cast<float>(rect.Width());
          } else {
            m_marqueeMaxOffset = 0.0f;
            m_marqueeOffset = 0.0f;
          }
        }
        if (compact_comment && !marquee_comment && !m_layout->language_split_enabled) {
          comment = EllipsizeEachLine(comment, static_cast<float>(rect.Width()),
                                      commenttxtFormat.Get(), pDWR);
        }
        const std::vector<bool> correction_mask = FuzzyCorrectionCommentMask(
            comment, comments.at(i).str);
        _TextOut(rect, comment.c_str(), comment.length(), comment_text_color,
                 commenttxtFormat.Get(),
                 marquee_comment ? m_marqueeOffset : 0.0f,
                 marquee_comment ? marquee_layout_width : 0.0f,
                 &correction_mask, m_fuzzyCorrectionTextColor);
      }
      drawn = true;
    }
    // draw highlight mark
    {
      if (!m_style.mark_text.empty() &&
          COLORNOTTRANSPARENT(m_style.hilited_mark_color)) {
        CRect rc = m_layout->GetHighlightRect();
        if (m_istorepos)
          rc.OffsetRect(0, m_offsetys[m_ctx.cinfo.highlighted]);
        rc.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                       DPI_SCALE(m_style.hilite_padding_y));
        int vgap = m_layout->mark_height
                       ? (rc.Height() - m_layout->mark_height) / 2
                       : 0;
        int hgap =
            m_layout->mark_width ? (rc.Width() - m_layout->mark_width) / 2 : 0;
        CRect hlRc;
        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
          hlRc = CRect(rc.left + hgap,
                       rc.top + DPI_SCALE(m_style.hilite_padding_y),
                       rc.left + hgap + m_layout->mark_width,
                       rc.top + DPI_SCALE(m_style.hilite_padding_y) +
                           m_layout->mark_height);
        else
          hlRc = CRect(rc.left + DPI_SCALE(m_style.hilite_padding_x),
                       rc.top + vgap,
                       rc.left + DPI_SCALE(m_style.hilite_padding_x) +
                           m_layout->mark_width,
                       rc.bottom - vgap);
        _TextOut(hlRc, m_style.mark_text.c_str(), m_style.mark_text.length(),
                 m_style.hilited_mark_color, pDWR->pTextFormat.Get());
      }
    }
  }
  return drawn;
}

// draw client area
void WeaselPanel::DoPaint(CDCHandle dc) {
  m_lastPaintSucceeded=false;
  // turn off WS_EX_TRANSPARENT, for better resp performance
  ModifyStyleEx(WS_EX_TRANSPARENT, WS_EX_LAYERED);
  GetClientRect(&rcw);
  // prepare memDC
  CDCHandle hdc = ::GetDC(m_hWnd);
  CDCHandle memDC = ::CreateCompatibleDC(hdc);
  BITMAPINFO bitmapInfo={};bitmapInfo.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
  bitmapInfo.bmiHeader.biWidth=rcw.Width();bitmapInfo.bmiHeader.biHeight=-rcw.Height();
  bitmapInfo.bmiHeader.biPlanes=1;bitmapInfo.bmiHeader.biBitCount=32;bitmapInfo.bmiHeader.biCompression=BI_RGB;
  void* pixelBuffer=nullptr;
  HBITMAP memBitmap=::CreateDIBSection(hdc,&bitmapInfo,DIB_RGB_COLORS,&pixelBuffer,nullptr,0);
  if(!memBitmap){::DeleteDC(memDC);ReleaseDC(hdc);return;}
  ZeroMemory(pixelBuffer,size_t(rcw.Width())*size_t(rcw.Height())*4);
  HGDIOBJ previousBitmap=::SelectObject(memDC, memBitmap);
  ReleaseDC(hdc);
  bool drawn = false;
  if (!hide_candidates) {
    CRect auxrc = m_layout->GetAuxiliaryRect();
    CRect preeditrc = m_layout->GetPreeditRect();
    if (m_istorepos) {
      CRect* rects = new CRect[m_candidateCount];
      int* btmys = new int[m_candidateCount];
      for (auto i = 0; i < m_candidateCount && i < MAX_CANDIDATES_COUNT; ++i) {
        rects[i] = m_layout->GetCandidateRect(i);
        btmys[i] = rects[i].bottom;
      }
      if (m_candidateCount) {
        if (!m_layout->IsInlinePreedit() && !m_ctx.preedit.str.empty())
          m_offsety_preedit =
              rects[m_candidateCount - 1].bottom - preeditrc.bottom;
        if (!m_ctx.aux.str.empty())
          m_offsety_aux = rects[m_candidateCount - 1].bottom - auxrc.bottom;
      } else {
        m_offsety_preedit = 0;
        m_offsety_aux = 0;
      }
      int base_gap = 0;
      if (!m_ctx.aux.str.empty())
        base_gap = auxrc.Height() + m_style.spacing;
      else if (!m_layout->IsInlinePreedit() && !m_ctx.preedit.str.empty())
        base_gap = preeditrc.Height() + m_style.spacing;

      for (auto i = 0; i < m_candidateCount && i < MAX_CANDIDATES_COUNT; ++i) {
        if (i == 0)
          m_offsetys[i] =
              btmys[m_candidateCount - i - 1] - base_gap - rects[i].bottom;
        else
          m_offsetys[i] = (rects[i - 1].top + m_offsetys[i - 1] -
                           DPI_SCALE(m_style.candidate_spacing)) -
                          rects[i].bottom;
      }
      delete[] rects;
      delete[] btmys;
    }
    // background and candidates back, hilite back drawing start
    if ((!m_ctx.empty() && !m_style.inline_preedit) ||
        (m_style.inline_preedit && (m_candidateCount || !m_ctx.aux.empty()))) {
      CRect backrc = m_layout->GetContentRect();
      _HighlightText(memDC, backrc, m_style.back_color, m_style.shadow_color,
                     DPI_SCALE(m_style.round_corner_ex), BackType::BACKGROUND,
                     IsToRoundStruct(), m_style.border_color);
    }
    if (!m_ctx.aux.str.empty()) {
      if (m_istorepos)
        auxrc.OffsetRect(0, m_offsety_aux);
      drawn |= _DrawPreeditBack(m_ctx.aux, memDC, auxrc);
    }
    if (!m_layout->IsInlinePreedit() && !m_ctx.preedit.str.empty()) {
      if (m_istorepos)
        preeditrc.OffsetRect(0, m_offsety_preedit);
      drawn |= _DrawPreeditBack(m_ctx.preedit, memDC, preeditrc);
    }
    if (m_candidateCount)
      drawn |= _DrawCandidates(memDC, true);
    // The expanded candidate grid is a continuous row viewport.  Draw the
    // same kind of position indicator used by scroll-mode frontends, based on
    // actual packed rows (long candidates may span two or three cells).
    if (m_style.max_height < 0 && m_layout->grid_total_rows > 0 &&
        m_layout->grid_visible_rows > 0) {
      CRect content = m_layout->GetContentRect();
      const int track_width = max(3, DPI_SCALE(4));
      const int inset = max(3, DPI_SCALE(6));
      CRect track(content.right - inset - track_width,
                  content.top + inset,
                  content.right - inset,
                  content.bottom - inset);
      if (track.Height() > 0) {
        // The layout holds a rolling candidate window.  Include the rows
        // already scrolled past, so the thumb never jumps back to the top
        // when that window advances across a Rime page boundary.
        const int skipped_rows = max(0, m_ctx.cinfo.currentPage);
        const int local_rows = m_layout->grid_total_rows;
        const int visible_rows = min(m_layout->grid_visible_rows, local_rows);
        const int total_rows = skipped_rows + local_rows +
            (m_ctx.cinfo.is_last_page ? 0 : visible_rows * 2);
        const int thumb_height = max(
            DPI_SCALE(18), track.Height() * visible_rows / total_rows);
        const int travel = max(0, track.Height() - thumb_height);
        const int max_first = max(1, total_rows - visible_rows);
        const int thumb_top = track.top +
            travel * min(skipped_rows + m_layout->grid_first_visible_row,
                         max_first) /
                max_first;
        Gdiplus::Graphics graphics(memDC);
        graphics.SetSmoothingMode(
            Gdiplus::SmoothingMode::SmoothingModeHighQuality);
        Gdiplus::SolidBrush track_brush(Gdiplus::Color(45, 255, 255, 255));
        Gdiplus::SolidBrush thumb_brush(Gdiplus::Color(150, 220, 220, 220));
        Gdiplus::RectF track_rect(static_cast<Gdiplus::REAL>(track.left),
                                  static_cast<Gdiplus::REAL>(track.top),
                                  static_cast<Gdiplus::REAL>(track.Width()),
                                  static_cast<Gdiplus::REAL>(track.Height()));
        Gdiplus::RectF thumb_rect(static_cast<Gdiplus::REAL>(track.left),
                                  static_cast<Gdiplus::REAL>(thumb_top),
                                  static_cast<Gdiplus::REAL>(track.Width()),
                                  static_cast<Gdiplus::REAL>(thumb_height));
        graphics.FillRectangle(&track_brush, track_rect);
        graphics.FillRectangle(&thumb_brush, thumb_rect);
      }
    }
    // background and candidates back, hilite back drawing end

    // begin  texts drawing, if pRenderTarget failed, force to reinit
    // directwrite resources
    if (FAILED(pDWR->pRenderTarget->BindDC(memDC, &rcw))) {
      _InitFontRes(true);
      pDWR->pRenderTarget->BindDC(memDC, &rcw);
    }
    pDWR->pRenderTarget->BeginDraw();
    // draw auxiliary string
    if (!m_ctx.aux.str.empty())
      drawn |= _DrawPreedit(m_ctx.aux, memDC, auxrc);
    // draw preedit string
    if (!m_layout->IsInlinePreedit() && !m_ctx.preedit.str.empty())
      drawn |= _DrawPreedit(m_ctx.preedit, memDC, preeditrc);
    // draw candidates string
    if (m_candidateCount)
      drawn |= _DrawCandidates(memDC);
    if(m_ctx.preedit.str==L"owned-split-fixture") {
      pDWR->SetBrushColor(D2D1::ColorF(1.0f,1.0f,0.0f,1.0f));
      pDWR->pRenderTarget->FillRectangle(D2D1::RectF(575,5,590,20),pDWR->pBrush.Get());
    }
    // Keep the divider in the same Direct2D pass as the glyphs. Mixing a new
    // GDI+ write after EndDraw can replace the just-rendered DC surface.
    for(const auto& line:m_layout->language_split_dividers) {
      pDWR->SetBrushColor(D2D1::ColorF(0.63f,0.63f,0.63f,0.55f));
      pDWR->pRenderTarget->DrawLine(D2D1::Point2F(float(line.left),float(line.top)),
          D2D1::Point2F(float(line.left),float(line.bottom)),pDWR->pBrush.Get(),1.0f);
    }
    const HRESULT finished=pDWR->pRenderTarget->EndDraw();
    OwnedPaintTrace(m_ctx.preedit.str,m_style.max_height,L"END="+std::to_wstring(long(finished)));
    if (FAILED(finished)) {
      ::SelectObject(memDC,previousBitmap);::DeleteDC(memDC);::DeleteObject(memBitmap);
      // A failed outer frame must never overwrite a successful recovery frame.
      if(m_paintRetryActive)return;
      m_paintRetryActive=true;
      _InitFontRes(true);
      RedrawWindow();
      m_paintRetryActive=false;
      return;
    }
    // end texts drawing

    // status icon (I guess Metro IME stole my idea :)
    if (m_layout->ShouldDisplayStatusIcon()) {
      // decide if custom schema zhung icon to show
      LoadIconNecessary(m_current_zhung_icon, m_style.current_zhung_icon,
                        m_iconEnabled, IDI_ZH);
      LoadIconNecessary(m_current_ascii_icon, m_style.current_ascii_icon,
                        m_iconAlpha, IDI_EN);
      LoadIconNecessary(m_current_half_icon, m_style.current_half_icon,
                        m_iconHalf, IDI_HALF_SHAPE);
      LoadIconNecessary(m_current_full_icon, m_style.current_full_icon,
                        m_iconFull, IDI_FULL_SHAPE);
      CRect iconRect(m_layout->GetStatusIconRect());
      if (m_istorepos && !m_ctx.aux.str.empty())
        iconRect.OffsetRect(0, m_offsety_aux);
      else if (m_istorepos && !m_layout->IsInlinePreedit() &&
               !m_ctx.preedit.str.empty())
        iconRect.OffsetRect(0, m_offsety_preedit);

      CIcon& icon(
          m_status.disabled ? m_iconDisabled
          : m_status.ascii_mode
              ? m_iconAlpha
              : (m_status.type == SCHEMA
                     ? m_iconEnabled
                     : (m_status.full_shape ? m_iconFull : m_iconHalf)));
      memDC.DrawIconEx(iconRect.left, iconRect.top, icon, 0, 0);
      drawn = true;
    }
    /* Nothing drawn, hide candidate window */
    if (!drawn)
      ShowWindow(SW_HIDE);
  }
  if(m_ctx.preedit.str==L"owned-split-fixture") {
    const auto pixels=static_cast<const BYTE*>(pixelBuffer);int all=0,glyph=0;
    for(int y=0;y<rcw.Height();++y)for(int x=0;x<rcw.Width();++x){auto p=pixels+4*(size_t(y)*rcw.Width()+x);if(p[2]>200&&p[1]>200&&p[0]<120){++all;if(y>30)++glyph;}}
    OwnedPaintTrace(m_ctx.preedit.str,m_style.max_height,L"RAW yellow="+std::to_wstring(all)+L" glyph="+std::to_wstring(glyph));
  }
  m_lastPaintSucceeded=_LayerUpdate(rcw, memDC);
  // Readback must not use a bitmap still selected in a DC (GetDIBits contract).
  ::GdiFlush();
  ::SelectObject(memDC,previousBitmap);

  // Capture only this process's explicit managed fixture, never a user host.
  wchar_t capture[MAX_PATH]={},ownedRoot[MAX_PATH]={},reportRoot[MAX_PATH]={};
  if(m_ctx.preedit.str==L"owned-split-fixture"&&
      GetEnvironmentVariableW(L"ZHONGRI_TEST_RUN_ROOT",ownedRoot,MAX_PATH)&&
      GetEnvironmentVariableW(L"ZHONGRI_TEST_REPORT_ROOT",reportRoot,MAX_PATH)&&
      GetEnvironmentVariableW(L"ZHONGRI_TEST_CAPTURE_DIR",capture,MAX_PATH)&&
      _wcsicmp(capture,reportRoot)==0) {
    wchar_t phase[16]={};GetEnvironmentVariableW(L"ZHONGRI_TEST_CAPTURE_PHASE",phase,16);
    const std::wstring path=std::wstring(capture)+(m_style.max_height<0?L"\\factory-expanded":L"\\factory-compact")+(wcscmp(phase,L"off")==0?L"-off.png":L".png");
    Gdiplus::Bitmap frame(memBitmap,nullptr);
    const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
    frame.Save(path.c_str(),&png,nullptr);
  }

  // clean objs
  ::DeleteDC(memDC);
  ::DeleteObject(memBitmap);
}

// 由于某些软件并不依赖 WM_PAINT 消息来重绘，在消息循环中直接忽略掉了 WM_PAINT
// 消息， 导致 DoPaint() 永远不会被调用，这里手动调用 DoPaint() 强制重绘
void WeaselPanel::RedrawWindow() {
  HDC hdc = GetDC();
  DoPaint(hdc);
  ReleaseDC(hdc);
}

bool WeaselPanel::_LayerUpdate(const CRect& rc, CDCHandle dc) {
  HDC ScreenDC = ::GetDC(NULL);
  CRect rect;
  GetWindowRect(&rect);
  POINT WindowPosAtScreen = {rect.left, rect.top};
  POINT PointOriginal = {0, 0};
  SIZE sz = {rc.Width(), rc.Height()};

  BLENDFUNCTION bf = {AC_SRC_OVER, 0, 0XFF, AC_SRC_ALPHA};
  const BOOL updated=UpdateLayeredWindow(m_hWnd, ScreenDC, &WindowPosAtScreen, &sz, dc,
                      &PointOriginal, RGB(0, 0, 0), &bf, ULW_ALPHA);
  ReleaseDC(ScreenDC);
  return updated!=FALSE;
}

LRESULT WeaselPanel::OnCreate(UINT uMsg,
                              WPARAM wParam,
                              LPARAM lParam,
                              BOOL& bHandled) {
  m_mouse_entry = false;
  m_hoverIndex = -1;
  m_marqueeIndex = -1;
  m_marqueeOffset = 0.0f;
  m_marqueeMaxOffset = 0.0f;
  _CreateCommentTooltip();
  Refresh();
  return TRUE;
}

LRESULT WeaselPanel::OnDestroy(UINT uMsg,
                               WPARAM wParam,
                               LPARAM lParam,
                               BOOL& bHandled) {
  ::KillTimer(m_hWnd, READING_PREVIEW_TIMER);
  ::KillTimer(m_hWnd, COMMENT_MARQUEE_TIMER);
  _HideCommentTooltip();
  if (m_commentTooltip != nullptr) {
    ::DestroyWindow(m_commentTooltip);
    m_commentTooltip = nullptr;
  }
  if (m_commentTooltipFont != nullptr) {
    ::DeleteObject(m_commentTooltipFont);
    m_commentTooltipFont = nullptr;
  }
  _RemoveAltPreviewKeyboardHook();
  m_hoverIndex = -1;
  m_marqueeIndex = -1;
  m_marqueeOffset = 0.0f;
  m_marqueeMaxOffset = 0.0f;
  m_lastMousePos = {-1, -1};
  m_sticky = false;
  delete m_layout;
  m_layout = NULL;
  return 0;
}

LRESULT WeaselPanel::OnDpiChanged(UINT uMsg,
                                  WPARAM wParam,
                                  LPARAM lParam,
                                  BOOL& bHandled) {
  Refresh();
  return LRESULT();
}

void WeaselPanel::MoveTo(RECT const& rc) {
  if (!m_layout)
    return;  // avoid handling nullptr in _RepositionWindow
  m_redraw_by_monitor_change = false;
  // The conditions for resetting the sticky state:
  // 1. When the input session ends (ctx.empty() is true)
  // 2. When the input position changes significantly (the position change
  // exceeds the threshold)
  // 3. When the content of the candidate window is empty
  bool should_reset_sticky =
      (m_ctx.empty() || (abs(rc.left - m_inputPos.left) > 50) ||
       (abs(rc.bottom - m_inputPos.bottom) > 50));
  if (should_reset_sticky && m_sticky) {
    m_sticky = false;
    // Force reposition the window
    m_inputPos = rc;
    m_inputPos.OffsetRect(0, 6);
    _RepositionWindow(true);
    RedrawWindow();
    return;
  }
  // if ascii_tip_follow_cursor set, move tip icon to mouse cursor
  if (m_style.ascii_tip_follow_cursor && m_ctx.empty() &&
      (!m_status.composing) && m_layout->ShouldDisplayStatusIcon()) {
    // ascii icon follow cursor
    POINT p;
    ::GetCursorPos(&p);
    RECT irc{p.x - STATUS_ICON_SIZE, p.y - STATUS_ICON_SIZE, p.x, p.y};
    m_inputPos = irc;
    _RepositionWindow(true);
    RedrawWindow();
  } else if (!(rc.left == m_inputPos.left && rc.bottom != m_inputPos.bottom &&
               abs(rc.bottom - m_inputPos.bottom) < 6) ||
             m_layout->ShouldDisplayStatusIcon()) {
    // in some apps like word 2021, with inline_preedit set,
    // bottom of rc would flicker 1 px or 2, make the candidate flickering
    m_inputPos = rc;
    m_inputPos.OffsetRect(0, 6);
    // buffer current m_istorepos status
    bool m_istorepos_buf = m_istorepos;
    // with parameter to avoid vertical flicker
    _RepositionWindow(true);
    // m_istorepos status changed by _RepositionWindow, or tips to show,
    // redrawing is required
    if (m_istorepos != m_istorepos_buf || !m_ctx.aux.empty() ||
        m_layout->ShouldDisplayStatusIcon() || m_redraw_by_monitor_change)
      RedrawWindow();
  }
}

void WeaselPanel::_RepositionWindow(const bool& adj) {
  RECT rcWorkArea;
  memset(&rcWorkArea, 0, sizeof(rcWorkArea));
  HMONITOR hMonitor = MonitorFromRect(m_inputPos, MONITOR_DEFAULTTONEAREST);
  if (hMonitor) {
    MONITORINFO info;
    info.cbSize = sizeof(MONITORINFO);
    if (GetMonitorInfo(hMonitor, &info)) {
      rcWorkArea = info.rcWork;
    }
    if (hMonitor != m_hMonitor) {
      m_hMonitor = hMonitor;
      m_redraw_by_monitor_change = true;
    }
  }
  RECT rcWindow;
  GetWindowRect(&rcWindow);
  int width = (rcWindow.right - rcWindow.left);
  int height = (rcWindow.bottom - rcWindow.top);
  // keep panel visible
  rcWorkArea.right -= width;
  rcWorkArea.bottom -= height;
  int x = m_inputPos.left;
  int y = m_inputPos.bottom;
  if (DPI_SCALE(m_style.shadow_radius)) {
    x -= (DPI_SCALE(m_style.shadow_offset_x) >= 0 ||
          COLORTRANSPARENT(m_style.shadow_color))
             ? m_layout->offsetX
             : (m_layout->offsetX / 2);
    if (adj)
      y -= (DPI_SCALE(m_style.shadow_offset_y) > 0 ||
            COLORTRANSPARENT(m_style.shadow_color))
               ? m_layout->offsetY
               : (m_layout->offsetY / 2);
  }
  // for vertical text layout, flow right to left, make window left side
  if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT &&
      !m_style.vertical_text_left_to_right) {
    x += m_layout->offsetX - width;
    if (DPI_SCALE(m_style.shadow_offset_x) < 0)
      x += m_layout->offsetX;
  }
  if (adj || !m_style.vertical_auto_reverse ||
      m_style.layout_type != UIStyle::LAYOUT_VERTICAL)
    m_istorepos = false;
  if (x > rcWorkArea.right)
    x = rcWorkArea.right;  // over workarea right
  if (x < rcWorkArea.left)
    x = rcWorkArea.left;  // over workarea left
  // show panel above the input focus if we're around the bottom
  if (y > rcWorkArea.bottom || m_sticky) {
    if (!m_sticky)
      m_sticky = true;
    y = m_inputPos.top - height - 6;  // over workarea bottom
    if (DPI_SCALE(m_style.shadow_radius) &&
        DPI_SCALE(m_style.shadow_offset_y) > 0)
      y -= DPI_SCALE(m_style.shadow_offset_y);
    m_istorepos = (m_style.vertical_auto_reverse &&
                   m_style.layout_type == UIStyle::LAYOUT_VERTICAL);
    if (DPI_SCALE(m_style.shadow_radius) > 0)
      y += (DPI_SCALE(m_style.shadow_offset_y) < 0 ||
            COLORTRANSPARENT(m_style.shadow_color))
               ? m_layout->offsetY
               : (m_layout->offsetY / 2);
  }
  if (y < rcWorkArea.top)
    y = rcWorkArea.top;  // over workarea top
  // memorize adjusted position (to avoid window bouncing on height change)
  m_inputPos.bottom = y;
  SetWindowPos(HWND_TOPMOST, x, y, 0, 0,
               SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
}

void WeaselPanel::_TextOut(const CRect& rc,
                           const std::wstring& psz,
                           const size_t& cch,
                           const int& inColor,
                           IDWriteTextFormat1* const pTextFormat,
                           float horizontal_offset,
                           float layout_width,
                           const std::vector<bool>* correction_mask,
                           int correction_color) {
  if (pTextFormat == NULL)
    return;
  float r = (float)(GetRValue(inColor)) / 255.0f;
  float g = (float)(GetGValue(inColor)) / 255.0f;
  float b = (float)(GetBValue(inColor)) / 255.0f;
  float alpha = (float)((inColor >> 24) & 255) / 255.0f;
  HRESULT hr = S_OK;
  if (pDWR->pBrush == NULL) {
    HR(pDWR->CreateBrush(D2D1::ColorF(r, g, b, alpha)));
  } else
    pDWR->SetBrushColor(D2D1::ColorF(r, g, b, alpha));

  HR(pDWR->CreateTextLayout(psz.c_str(), (int)cch, pTextFormat,
                            layout_width > 0.0f ? layout_width
                                                : (float)rc.Width(),
                            (float)rc.Height()));
  if (m_layout->language_split_enabled)
    ConfigureSplitTextLayout(pDWR->pTextLayout.Get());
  if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT) {
    DWRITE_FLOW_DIRECTION flow = m_style.vertical_text_left_to_right
                                     ? DWRITE_FLOW_DIRECTION_LEFT_TO_RIGHT
                                     : DWRITE_FLOW_DIRECTION_RIGHT_TO_LEFT;
    HR(pDWR->SetLayoutReadingDirection(DWRITE_READING_DIRECTION_TOP_TO_BOTTOM));
    HR(pDWR->SetLayoutFlowDirection(flow));
  }

  // offsetx for font glyph over left
  float offsetx = (float)rc.left - max(0.0f, horizontal_offset);
  float offsety = (float)rc.top;
  // prepare for space when first character overhanged
  DWRITE_OVERHANG_METRICS omt;
  HR(pDWR->GetLayoutOverhangMetrics(&omt));
  if(m_ctx.preedit.str==L"owned-split-fixture") {
    DWRITE_TEXT_METRICS metrics={};pDWR->GetLayoutMetrics(&metrics);
    wchar_t trace[256]={};swprintf_s(trace,L"TEXT ch=%04x count=%u color=%08x rect=%d,%d,%d,%d font=%.1f metrics=%.1f,%.1f",psz.empty()?0:unsigned(psz[0]),unsigned(cch),unsigned(inColor),rc.left,rc.top,rc.right,rc.bottom,pTextFormat->GetFontSize(),metrics.widthIncludingTrailingWhitespace,metrics.height);
    OwnedPaintTrace(m_ctx.preedit.str,m_style.max_height,trace);
  }
  if (m_style.layout_type != UIStyle::LAYOUT_VERTICAL_TEXT && omt.left > 0)
    offsetx += omt.left;
  if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT && omt.top > 0)
    offsety += omt.top;

  if (pDWR->pTextLayout != NULL) {
    const D2D1_RECT_F clip = D2D1::RectF(
        static_cast<float>(rc.left), static_cast<float>(rc.top),
        static_cast<float>(rc.right), static_cast<float>(rc.bottom));
    pDWR->pRenderTarget->PushAxisAlignedClip(
        clip, D2D1_ANTIALIAS_MODE_ALIASED);
    pDWR->DrawTextLayoutAt({offsetx, offsety});
    pDWR->pRenderTarget->PopAxisAlignedClip();
    if (correction_mask && COLORNOTTRANSPARENT(correction_color)) {
      const size_t limit = (std::min)(cch, correction_mask->size());
      bool accent_brush_ready = false;
      for (size_t index = 0; index < limit; ++index) {
        if (!(*correction_mask)[index]) continue;
        DWRITE_HIT_TEST_METRICS metric = {};
        UINT32 actual = 0;
        if (FAILED(pDWR->pTextLayout->HitTestTextRange(
                static_cast<UINT32>(index), 1, offsetx, offsety,
                &metric, 1, &actual)) || actual == 0)
          continue;
        const D2D1_RECT_F glyph_clip = D2D1::RectF(
            (std::max)(static_cast<float>(rc.left), metric.left),
            (std::max)(static_cast<float>(rc.top), metric.top),
            (std::min)(static_cast<float>(rc.right), metric.left + metric.width),
            (std::min)(static_cast<float>(rc.bottom), metric.top + metric.height));
        if (glyph_clip.right <= glyph_clip.left ||
            glyph_clip.bottom <= glyph_clip.top)
          continue;
        if (!accent_brush_ready) {
          pDWR->SetBrushColor(D2D1::ColorF(
              GetRValue(correction_color) / 255.0f,
              GetGValue(correction_color) / 255.0f,
              GetBValue(correction_color) / 255.0f,
              ((correction_color >> 24) & 255) / 255.0f));
          accent_brush_ready = true;
        }
        pDWR->pRenderTarget->PushAxisAlignedClip(
            glyph_clip, D2D1_ANTIALIAS_MODE_ALIASED);
        pDWR->DrawTextLayoutAt({offsetx, offsety});
        pDWR->pRenderTarget->PopAxisAlignedClip();
      }
    }
#if 0
    D2D1_RECT_F rectf =  D2D1::RectF(offsetx, offsety, offsetx + rc.Width(), offsety + rc.Height());
    pDWR->DrawRect(&rectf);
#endif
  }
}
