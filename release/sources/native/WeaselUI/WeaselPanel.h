#pragma once
#include <WeaselIPCData.h>
#include <WeaselUI.h>
#include "StandardLayout.h"
#include "Layout.h"
#include "GdiplusBlur.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using namespace weasel;

typedef CWinTraits<WS_POPUP | WS_CLIPSIBLINGS | WS_DISABLED,
                   WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE |
                       WS_EX_LAYERED>
    CWeaselPanelTraits;

enum class BackType {
  TEXT = 0,
  CAND = 1,
  BACKGROUND = 2  // background
};

class WeaselPanel
    : public CWindowImpl<WeaselPanel, CWindow, CWeaselPanelTraits>,
      CDoubleBufferImpl<WeaselPanel> {
 public:
  BEGIN_MSG_MAP(WeaselPanel)
  MESSAGE_HANDLER(WM_CREATE, OnCreate)
  MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
  MESSAGE_HANDLER(WM_SHOWWINDOW, OnShowWindow)
  MESSAGE_HANDLER(WM_WINDOWPOSCHANGED, OnWindowPosChanged)
  MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
  MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
  MESSAGE_HANDLER(WM_LBUTTONUP, OnLeftClickedUp)
  MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLeftClickedDown)
  MESSAGE_HANDLER(WM_RBUTTONDOWN, OnRightClickedUp)
  MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
  MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
  MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
  MESSAGE_HANDLER(WM_TIMER, OnPreviewTimer)
  CHAIN_MSG_MAP(CDoubleBufferImpl<WeaselPanel>)
  END_MSG_MAP()

  LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnShowWindow(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnWindowPosChanged(UINT uMsg,
                             WPARAM wParam,
                             LPARAM lParam,
                             BOOL& bHandled);
  LRESULT OnDpiChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnMouseActivate(UINT uMsg,
                          WPARAM wParam,
                          LPARAM lParam,
                          BOOL& bHandled);
  LRESULT OnLeftClickedUp(UINT uMsg,
                          WPARAM wParam,
                          LPARAM lParam,
                          BOOL& bHandled);
  LRESULT OnLeftClickedDown(UINT uMsg,
                            WPARAM wParam,
                            LPARAM lParam,
                            BOOL& bHandled);
  LRESULT OnRightClickedUp(UINT uMsg,
                           WPARAM wParam,
                           LPARAM lParam,
                           BOOL& bHandled);
  LRESULT OnMouseWheel(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnPreviewTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  static LRESULT CALLBACK AltPreviewKeyboardProc(int code,
                                                  WPARAM wParam,
                                                  LPARAM lParam);

  WeaselPanel(weasel::UI& ui);
  ~WeaselPanel();

  void MoveTo(RECT const& rc);
  void Refresh();
  void SetJapaneseReadingPreview(bool active);
  std::vector<std::vector<int>> GetExpandedCandidateRows() const;
  int GetVisibleCandidateCount() const;
  RECT GetLanguageSplitDivider() const {return m_layout?m_layout->language_split_divider:RECT{0,0,0,0};}
  bool HasSuccessfulPaint() const {return m_lastPaintSucceeded;}
  PDWR GetDrawingResources() const {return pDWR;}
  void DoPaint(CDCHandle dc);
  bool GetIsReposition() { return m_istorepos; }
  void RedrawWindow();

  static VOID CALLBACK OnTimer(_In_ HWND hwnd,
                               _In_ UINT uMsg,
                               _In_ UINT_PTR idEvent,
                               _In_ DWORD dwTime);
  static const int AUTOREV_TIMER = 20240315;
  static const int READING_PREVIEW_TIMER = 20260822;
  static const int COMMENT_MARQUEE_TIMER = 20260922;
  static const int HOT_ANNOTATION_TIMER = 20260927;
  static UINT_PTR ptimer;

 private:
  template <typename T>
  int DPI_SCALE(T t) {
    return (int)(t * dpiScaleLayout);
  }
  void _InitFontRes(bool forced = false);
  void _CaptureRect(CRect& rect);
  bool _IsCommentTruncated(size_t index);
  void _CreateCommentTooltip();
  void _ShowCommentTooltip(size_t index, const CRect& candidate_rect);
  void _HideCommentTooltip();
  bool m_mouse_entry = false;
  CPoint m_lastMousePos = {-1, -1};
  std::vector<std::wstring> m_readingToggleCandidates;
  std::vector<std::wstring> m_originalComments;
  std::vector<std::wstring> m_originalCommentMetadata;
  std::vector<std::wstring> m_japaneseReadings;
  // Exact strings last projected into m_ctx for temporary reading display.
  // They must never be treated as fresh server comments, otherwise releasing
  // Shift/Space overwrites m_originalComments with the reading projection.
  std::vector<std::wstring> m_lastAppliedReadingComments;
  std::vector<int> m_candidateLanguages;
  std::vector<bool> m_readingShown;
  bool m_shiftReadingPreview = false;
  ULONGLONG m_readingPreviewStarted = 0;
  HHOOK m_altPreviewKeyboardHook = nullptr;
  bool m_altPreviewReleasePending = false;
  bool m_altPreviewChordObserved = false;
  static WeaselPanel* s_altPreviewHookPanel;
  void _EnsureAltPreviewKeyboardHook();
  void _RemoveAltPreviewKeyboardHook();
  void _SyncJapaneseReadingCache();
  void _ApplyJapaneseReadingDisplay();
  void _CreateLayout();
  void _ResizeWindow();
  void _RepositionWindow(const bool& adj = false);
  bool _DrawPreedit(const Text& text, CDCHandle dc, const CRect& rc);
  bool _DrawPreeditBack(const Text& text, CDCHandle dc, const CRect& rc);
  bool _DrawCandidates(CDCHandle& dc, bool back = false);
  void _HighlightText(CDCHandle& dc,
                      const CRect& rc,
                      const COLORREF& color,
                      const COLORREF& shadowColor,
                      const int& radius,
                      const BackType& type,
                      const IsToRoundStruct& rd,
                      const COLORREF& bordercolor);
  void _TextOut(const CRect& rc,
                const std::wstring& psz,
                const size_t& cch,
                const int& inColor,
                IDWriteTextFormat1* const pTextFormat = NULL,
                float horizontal_offset = 0.0f,
                float layout_width = 0.0f,
                const std::vector<bool>* correction_mask = nullptr,
                int correction_color = 0);

  bool _LayerUpdate(const CRect& rc, CDCHandle dc);

  weasel::Layout* m_layout;
  int m_seenSplitLayoutSignature=-1;
  bool m_lastPaintSucceeded=false;
  bool m_paintRetryActive=false;
  weasel::Context& m_ctx;
  weasel::Context& m_octx;
  weasel::Status& m_status;
  weasel::UIStyle& m_style;
  weasel::UIStyle& m_ostyle;
  const bool& m_in_server;

  CRect m_inputPos;
  int m_offsetys[MAX_CANDIDATES_COUNT]{};  // offset y for candidates when
                                         // vertical layout over bottom
  int m_offsety_preedit = 0;
  int m_offsety_aux = 0;
  bool m_istorepos = false;

  CIcon m_iconDisabled;
  CIcon m_iconEnabled;
  CIcon m_iconAlpha;
  CIcon m_iconFull;
  CIcon m_iconHalf;
  std::wstring m_current_zhung_icon;
  std::wstring m_current_ascii_icon;
  std::wstring m_current_half_icon;
  std::wstring m_current_full_icon;
  // for gdiplus drawings
  Gdiplus::GdiplusStartupInput _m_gdiplusStartupInput;
  ULONG_PTR _m_gdiplusToken;

  UINT dpi;

  CRect rcw;
  BYTE m_candidateCount;
  BYTE m_lastCandidateCount;

  bool hide_candidates;
  bool m_sticky;
  // for multi font_face & font_point
  PDWR pDWR;
  std::function<void(size_t* const, size_t* const, bool* const, bool* const)>&
      _UICallback;
  std::function<void()>& _hotAnnotationCallback;
  DWORD m_seenAnnotationGeneration = 0;
  DWORD m_seenFuzzyColorGeneration = 0xffffffff;
  int m_fuzzyCorrectionTextColor = static_cast<int>(0xff00ffff);
  int m_reservedPhraseTextColor = static_cast<int>(0xff80de4a);
  float bar_scale_ = 1.0;
  float dpiScaleLayout = 1.0f;
  int m_hoverIndex = -1;
  int m_marqueeIndex = -1;
  float m_marqueeOffset = 0.0f;
  float m_marqueeMaxOffset = 0.0f;
  ULONGLONG m_marqueeHoverStarted = 0;
  HWND m_commentTooltip = nullptr;
  HFONT m_commentTooltipFont = nullptr;
  std::wstring m_commentTooltipText;
  HMONITOR m_hMonitor = NULL;
  bool m_redraw_by_monitor_change = false;
};
