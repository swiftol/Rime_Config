#include "stdafx.h"

#include "WeaselTSF.h"
#include "CandidateList.h"
#include <KeyEvent.h>
#include <math.h>

using namespace std;
using namespace weasel;

static UINT IntegratedCandidateCount(const Context& ctx) {
  return static_cast<UINT>(ctx.cinfo.candies.size());
}

static void CandidateClientTrace(const std::string& message) {
  return;  // Disabled outside targeted diagnostics.
  const std::string debug_line =
      "WEASEL_TSF pid=" + std::to_string(GetCurrentProcessId()) + " " +
      message;
  OutputDebugStringA(debug_line.c_str());

  wchar_t temp_path[MAX_PATH] = {};
  HANDLE file = INVALID_HANDLE_VALUE;
  if (GetTempPathW(MAX_PATH, temp_path)) {
    std::wstring path(temp_path);
    path += L"weasel_candidate_expand.log";
    file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  }
  const std::string line = std::to_string(GetTickCount64()) +
                           " [TSF pid=" +
                           std::to_string(GetCurrentProcessId()) + "] " +
                           message + "\r\n";
  if (file != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written,
              nullptr);
    CloseHandle(file);
  }

  // Packaged hosts such as Windows Search run in an AppContainer whose TEMP
  // is not visible from the desktop session.  Mirror TSF diagnostics to a
  // public file so integrated-candidate-UI calls can be inspected reliably.
  wchar_t module_path[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, module_path, MAX_PATH);
  std::wstring public_path =
      L"C:\\Users\\Public\\Documents\\weasel_search_tsf.log";
  HANDLE public_file = CreateFileW(
      public_path.c_str(), FILE_APPEND_DATA,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (public_file != INVALID_HANDLE_VALUE) {
    std::string module;
    for (const wchar_t* p = module_path; *p; ++p)
      module.push_back(*p < 128 ? static_cast<char>(*p) : '?');
    const std::string public_line =
        std::to_string(GetTickCount64()) + " [TSF pid=" +
        std::to_string(GetCurrentProcessId()) + " exe=" + module + "] " +
        message + "\r\n";
    DWORD public_written = 0;
    WriteFile(public_file, public_line.data(),
              static_cast<DWORD>(public_line.size()), &public_written, nullptr);
    CloseHandle(public_file);
  }
}

CCandidateList::CCandidateList(com_ptr<WeaselTSF> pTextService)
    : _ui(make_unique<UI>()), _tsf(pTextService), _pbShow(TRUE) {
  _cRef = 1;
  _ui->SetHotAnnotationCallback([this]() {
    if (_tsf) _tsf->RefreshHotAnnotations();
  });
}

CCandidateList::~CCandidateList() {}

STDMETHODIMP CCandidateList::QueryInterface(REFIID riid, void** ppvObj) {
  if (ppvObj == nullptr) {
    return E_INVALIDARG;
  }

  *ppvObj = nullptr;

  if (IsEqualIID(riid, IID_ITfUIElement) ||
      IsEqualIID(riid, IID_ITfCandidateListUIElement) ||
      IsEqualIID(riid, IID_ITfCandidateListUIElementBehavior)) {
    *ppvObj = (ITfCandidateListUIElementBehavior*)this;
  } else if (IsEqualIID(riid, IID_IUnknown) ||
             IsEqualIID(riid,
                        __uuidof(ITfIntegratableCandidateListUIElement))) {
    *ppvObj = (ITfIntegratableCandidateListUIElement*)this;
  }

  if (*ppvObj) {
    AddRef();
    return S_OK;
  }

  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CCandidateList::AddRef(void) {
  return ++_cRef;
}

STDMETHODIMP_(ULONG) CCandidateList::Release(void) {
  LONG cr = --_cRef;

  assert(_cRef >= 0);

  if (_cRef == 0) {
    delete this;
  }

  return cr;
}

STDMETHODIMP CCandidateList::GetDescription(BSTR* pbstr) {
  static auto str = SysAllocString(L"Candidate List");
  if (pbstr) {
    *pbstr = str;
  }
  return S_OK;
}

STDMETHODIMP CCandidateList::GetGUID(GUID* pguid) {
  /// 36c3c795-7159-45aa-ab12-30229a51dbd3
  *pguid = {0x36c3c795,
            0x7159,
            0x45aa,
            {0xab, 0x12, 0x30, 0x22, 0x9a, 0x51, 0xdb, 0xd3}};
  return S_OK;
}

STDMETHODIMP CCandidateList::Show(BOOL showCandidateWindow) {
  CandidateClientTrace(std::string("SHOW requested=") +
                       (showCandidateWindow ? "1" : "0") +
                       " pbShow=" + std::to_string(_pbShow) +
                       " composing=" +
                       std::to_string(_ui->status().composing));
  if (showCandidateWindow)
    _ui->Show();
  else
    _ui->Hide();
  return S_OK;
}

STDMETHODIMP CCandidateList::IsShown(BOOL* pIsShow) {
  *pIsShow = _ui->IsShown();
  CandidateClientTrace("IS_SHOWN result=" + std::to_string(*pIsShow));
  return S_OK;
}

STDMETHODIMP CCandidateList::GetUpdatedFlags(DWORD* pdwFlags) {
  if (!pdwFlags)
    return E_INVALIDARG;

  *pdwFlags = TF_CLUIE_DOCUMENTMGR | TF_CLUIE_COUNT | TF_CLUIE_SELECTION |
              TF_CLUIE_STRING | TF_CLUIE_CURRENTPAGE;
  return S_OK;
}

STDMETHODIMP CCandidateList::GetDocumentMgr(ITfDocumentMgr** ppdim) {
  *ppdim = nullptr;
  auto pThreadMgr = _tsf->_GetThreadMgr();
  if (pThreadMgr == nullptr) {
    return E_FAIL;
  }
  if (FAILED(pThreadMgr->GetFocus(ppdim)) || (*ppdim == nullptr)) {
    return E_FAIL;
  }
  return S_OK;
}

STDMETHODIMP CCandidateList::GetCount(UINT* pCandidateCount) {
  *pCandidateCount = IntegratedCandidateCount(_ui->ctx());
  CandidateClientTrace("GET_COUNT count=" +
                       std::to_string(*pCandidateCount) +
                       " highlighted=" +
                       std::to_string(_ui->ctx().cinfo.highlighted));
  return S_OK;
}

STDMETHODIMP CCandidateList::GetSelection(UINT* pSelectedCandidateIndex) {
  const UINT count = IntegratedCandidateCount(_ui->ctx());
  *pSelectedCandidateIndex =
      count ? (std::min)(static_cast<UINT>(_ui->ctx().cinfo.highlighted),
                         count - 1)
            : 0;
  return S_OK;
}

STDMETHODIMP CCandidateList::GetString(UINT uIndex, BSTR* pbstr) {
  *pbstr = nullptr;
  auto& cinfo = _ui->ctx().cinfo;
  if (uIndex >= IntegratedCandidateCount(_ui->ctx()))
    return E_INVALIDARG;

  auto& str = cinfo.candies[uIndex].str;
  CandidateClientTrace("GET_STRING index=" + std::to_string(uIndex) +
                       " total=" + std::to_string(cinfo.candies.size()));
  *pbstr = SysAllocStringLen(str.c_str(), static_cast<UINT>(str.size()) + 1);

  return S_OK;
}

STDMETHODIMP CCandidateList::GetPageIndex(UINT* pIndex,
                                          UINT uSize,
                                          UINT* puPageCnt) {
  if (!puPageCnt)
    return E_INVALIDARG;
  *puPageCnt = 1;
  if (pIndex) {
    if (uSize < *puPageCnt) {
      return E_INVALIDARG;
    }
    *pIndex = 0;
  }
  return S_OK;
}

STDMETHODIMP CCandidateList::SetPageIndex(UINT* pIndex, UINT uPageCnt) {
  if (!pIndex)
    return E_INVALIDARG;
  return S_OK;
}

STDMETHODIMP CCandidateList::GetCurrentPage(UINT* puPage) {
  *puPage = 0;
  return S_OK;
}

STDMETHODIMP CCandidateList::SetSelection(UINT nIndex) {
  _ui->ctx().cinfo.highlighted = nIndex;
  return S_OK;
}

STDMETHODIMP CCandidateList::Finalize(void) {
  Destroy();
  return S_OK;
}

STDMETHODIMP CCandidateList::Abort(void) {
  _tsf->_AbortComposition(true);
  Destroy();
  return S_OK;
}

STDMETHODIMP CCandidateList::SetIntegrationStyle(GUID guidIntegrationStyle) {
  return S_OK;
}

STDMETHODIMP CCandidateList::GetSelectionStyle(
    TfIntegratableCandidateListSelectionStyle* ptfSelectionStyle) {
  *ptfSelectionStyle = _selectionStyle;
  return S_OK;
}

STDMETHODIMP CCandidateList::OnKeyDown(WPARAM wParam,
                                       LPARAM lParam,
                                       BOOL* pIsEaten) {
  CandidateClientTrace("ON_KEY_DOWN key=" + std::to_string(wParam) +
                       " lparam=" + std::to_string(lParam));
  *pIsEaten = TRUE;
  return S_OK;
}

STDMETHODIMP CCandidateList::ShowCandidateNumbers(BOOL* pIsShow) {
  *pIsShow = TRUE;
  return S_OK;
}

STDMETHODIMP CCandidateList::FinalizeExactCompositionString() {
  _tsf->_AbortComposition(false);
  return E_NOTIMPL;
}

void CCandidateList::UpdateUI(const Context& ctx, const Status& status) {
  _currentPreedit = ctx.preedit.str;
  _currentCandidateCount = ctx.cinfo.candies.size();
  // ResponseParser writes the newest server style into _style.  The visible
  // candidate window, however, renders from _ui->style().  Keeping the two
  // copies out of sync made every key response (notably paging and Shift)
  // briefly repaint the TSF window with the old one-line 1..9 layout over the
  // expanded grid.  Apply the complete style before changing context so one
  // frame can never mix the new candidates with the old geometry/fonts.
  CandidateClientTrace(
      "UPDATE staged_maxh=" + std::to_string(_style.max_height) +
      " live_maxh=" + std::to_string(_ui->style().max_height) +
      " layout=" + std::to_string(_style.layout_type) +
      " highlighted=" + std::to_string(ctx.cinfo.highlighted) +
      " count=" + std::to_string(ctx.cinfo.candies.size()));
  _ui->style() = _style;

  const bool expanded_window = _style.max_height < 0;
  // Expanded mode always uses the TSF-owned surface.  It is the only surface
  // with a direct callback to the active composition, so mouse selection can
  // commit the chosen candidate.  The server copy is suppressed separately.
  if (expanded_window && !_ui->HasWindow()) {
    CandidateClientTrace("EXPANDED_CREATE_LOCAL_WINDOW pbShow=" +
                         std::to_string(_pbShow));
    _MakeUIWindow();
  }

  if (_ui->style().inline_preedit) {
    _ui->style().client_caps |= weasel::INLINE_PREEDIT_CAPABLE;
  } else {
    _ui->style().client_caps &= ~weasel::INLINE_PREEDIT_CAPABLE;
  }

  /// In UWP, candidate window will only be shown
  /// if it is owned by active view window
  //_UpdateOwner();
  _ui->Update(ctx, status);
  _UpdateUIElement();

  if (status.composing) {
    Show(expanded_window ? TRUE : _pbShow);
    if (!expanded_window && !_pbShow && _ui->HasWindow()) {
      CandidateClientTrace("NORMAL_DISPOSE_LOCAL_WINDOW");
      _DisposeUIWindow();
    }
  } else
    Show(FALSE);
}

int CCandidateList::GetNumericCandidateIndex(int ordinal) const {
  if (!_ui || !_currentCandidateCount)
    return -2;
  return _ui->GetProtectedNumericCandidateIndex(ordinal);
}

int CCandidateList::GetAdjacentCandidateRowIndex(bool backward) const {
  return _ui ? _ui->GetAdjacentCandidateRowIndex(backward) : -2;
}

void CCandidateList::ResetSelectionGuard() {
  if (_ui) _ui->ResetSelectionGuard();
}

bool CCandidateList::IsSelectionGuardActive() const {
  return _ui && _ui->IsSelectionGuardActive();
}

bool CCandidateList::HasNumericComposition() const {
  if (_currentCandidateCount == 0 || _currentPreedit.empty())
    return false;
  bool has_digit = false;
  for (const wchar_t ch : _currentPreedit) {
    if (ch >= L'0' && ch <= L'9') {
      has_digit = true;
      continue;
    }
    // Rime may insert visual syllable separators into the preedit string.
    if (ch != L' ' && ch != L'\'' && ch != L'\t')
      return false;
  }
  return has_digit;
}

void CCandidateList::UpdateStyle(const UIStyle& sty) {
  _style = sty;
  _ui->style() = sty;
}

void CCandidateList::UpdateInputPosition(RECT const& rc) {
  _ui->UpdateInputPosition(rc);
}

void CCandidateList::SetJapaneseReadingPreview(bool active) {
  if (_ui)
    _ui->SetJapaneseReadingPreview(active);
  if (_tsf)
    _tsf->SetJapaneseReadingPreview(active);
}

void CCandidateList::Destroy() {
  // EndUI();
  Show(FALSE);
  _DisposeUIWindow();
}

void CCandidateList::DestroyAll() {
  // EndUI();
  Show(FALSE);
  _DisposeUIWindowAll();
}
UIStyle& CCandidateList::style() {
  // return _ui->style();
  return _style;
}

HWND CCandidateList::_GetActiveWnd() {
  com_ptr<ITfDocumentMgr> pDocumentMgr;
  com_ptr<ITfContext> pContext;
  com_ptr<ITfContextView> pContextView;
  com_ptr<ITfThreadMgr> pThreadMgr = _tsf->_GetThreadMgr();

  HWND w = NULL;

  // Reset current context
  _pContextDocument = nullptr;

  if (pThreadMgr != nullptr && SUCCEEDED(pThreadMgr->GetFocus(&pDocumentMgr)) &&
      SUCCEEDED(pDocumentMgr->GetTop(&pContext)) &&
      SUCCEEDED(pContext->GetActiveView(&pContextView))) {
    // Set current context
    _pContextDocument = pContext;
    pContextView->GetWnd(&w);
  }

  if (w == NULL)
    w = ::GetFocus();
  return w;
}

HRESULT CCandidateList::_UpdateUIElement() {
  HRESULT hr = S_OK;

  com_ptr<ITfUIElementMgr> pUIElementMgr;
  com_ptr<ITfThreadMgr> pThreadMgr = _tsf->_GetThreadMgr();
  if (nullptr == pThreadMgr) {
    return S_OK;
  }
  hr = pThreadMgr->QueryInterface(IID_ITfUIElementMgr, (void**)&pUIElementMgr);

  if (hr == S_OK) {
    pUIElementMgr->UpdateUIElement(uiid);
  }

  return S_OK;
}

void CCandidateList::StartUI() {
  com_ptr<ITfThreadMgr> pThreadMgr = _tsf->_GetThreadMgr();
  if (!pThreadMgr) {
    return;
  }

  com_ptr<ITfUIElementMgr> pUIElementMgr;
  auto hr = pThreadMgr->QueryInterface(&pUIElementMgr);
  if (FAILED(hr))
    return;

  if (pUIElementMgr == NULL) {
    return;
  }

  if (!_ui->uiCallback())
    _ui->SetUICallBack([this](size_t* const sel, size_t* const hov,
                              bool* const next, bool* const scroll_next) {
      _tsf->HandleUICallback(sel, hov, next, scroll_next);
    });
  pUIElementMgr->BeginUIElement(this, &_pbShow, &uiid);
  CandidateClientTrace("BEGIN_UI_ELEMENT pbShow=" +
                       std::to_string(_pbShow) + " uiid=" +
                       std::to_string(uiid));
  // pUIElementMgr->UpdateUIElement(uiid);
  if (_pbShow) {
    _ui->style() = _style;
    _MakeUIWindow();
  }
}

void CCandidateList::EndUI() {
  com_ptr<ITfThreadMgr> pThreadMgr = _tsf->_GetThreadMgr();
  if (pThreadMgr) {
    com_ptr<ITfUIElementMgr> emgr;
    auto hr = pThreadMgr->QueryInterface(&emgr);
    if (FAILED(hr))
      return;
    if (emgr != NULL)
      emgr->EndUIElement(uiid);
  }
  _DisposeUIWindow();
}

com_ptr<ITfContext> CCandidateList::GetContextDocument() {
  return _pContextDocument;
}

void CCandidateList::_DisposeUIWindow() {
  if (_ui == nullptr) {
    return;
  }

  _ui->Destroy();
}

void CCandidateList::_DisposeUIWindowAll() {
  if (_ui == nullptr) {
    return;
  }

  // call _ui->Destroy(true) to clean resources
  _ui->Destroy(true);
}

void CCandidateList::_MakeUIWindow() {
  HWND p = _GetActiveWnd();
  _ui->Create(p);
}

void WeaselTSF::_UpdateUI(const Context& ctx, const Status& status) {
  _cand->UpdateUI(ctx, status);
}

void WeaselTSF::_StartUI() {
  _cand->StartUI();
}

void WeaselTSF::_EndUI() {
  _cand->EndUI();
}

void WeaselTSF::_ShowUI() {
  _cand->Show(TRUE);
}

void WeaselTSF::_HideUI() {
  _cand->Show(FALSE);
}

com_ptr<ITfContext> WeaselTSF::_GetUIContextDocument() {
  return _cand->GetContextDocument();
}

void WeaselTSF::_DeleteCandidateList() {
  _cand->Destroy();
}

void WeaselTSF::_SelectCandidateOnCurrentPage(size_t index) {
  m_client.SelectCandidateOnCurrentPage(index);
  // simulate a VK_SELECT presskey to get data back and DoEditSession
  // the simulated keycode must be the one make TranslateKeycode Non-Zero return
  // fix me: are there any better ways?
  INPUT inputs[2];
  inputs[0].type = INPUT_KEYBOARD;
  inputs[0].ki = {VK_SELECT, 0, 0, 0, 0};
  inputs[1].type = INPUT_KEYBOARD;
  inputs[1].ki = {VK_SELECT, 0, KEYEVENTF_KEYUP, 0, 0};
  ::SendInput(sizeof(inputs) / sizeof(INPUT), inputs, sizeof(INPUT));
}

void WeaselTSF::_HandleMousePageEvent(bool* const nextPage,
                                      bool* const scrollNextPage) {
  _cand->ResetSelectionGuard();
  // from scrolling event
  if (scrollNextPage) {
    // The expanded grid is a continuous row viewport, not a stack of Rime
    // pages.  Route one wheel notch through the same visual-row navigation as
    // Page Up/Down, so it advances exactly one row (whose item count may vary
    // when long candidates span multiple cells).
    if (_cand->style().max_height < 0) {
      const int target = _cand->GetAdjacentCandidateRowIndex(!*scrollNextPage);
      if (target >= 0) {
        m_client.HighlightCandidateOnCurrentPage(static_cast<size_t>(target));
        _UpdateComposition(_pEditSessionContext);
        return;
      }
      if (target == -1 || target == -2) return;
      KeyEvent ke{0, 0};
      ke.keycode = *scrollNextPage ? ibus::Page_Down : ibus::Page_Up;
      m_client.ProcessKeyEvent(ke);
    } else if (_cand->style().paging_on_scroll)
      m_client.ChangePage(!(*scrollNextPage));
    else {
      UINT current_select = 0, cand_count = 0;
      _cand->GetSelection(&current_select);
      _cand->GetCount(&cand_count);
      bool is_reposition = _cand->GetIsReposition();
      int offset = *scrollNextPage ? 1 : -1;
      offset = offset * (is_reposition ? -1 : 1);
      int index = (int)current_select + offset;
      if (index >= 0 && index < (int)cand_count)
        m_client.HighlightCandidateOnCurrentPage((size_t)index);
      else {
        KeyEvent ke{0, 0};
        ke.keycode = (index < 0) ? ibus::Up : ibus::Down;
        m_client.ProcessKeyEvent(ke);
      }
    }
  } else {  // from click event
    m_client.ChangePage(!(*nextPage));
  }
  _UpdateComposition(_pEditSessionContext);
}

void WeaselTSF::_HandleMouseHoverEvent(const size_t index) {
  UINT current_select = 0;
  _cand->GetSelection(&current_select);

  if (index != current_select) {
    m_client.HighlightCandidateOnCurrentPage(index);
    _UpdateComposition(_pEditSessionContext);
  }
}

void WeaselTSF::HandleUICallback(size_t* const sel,
                                 size_t* const hov,
                                 bool* const next,
                                 bool* const scroll_next) {
  if (sel)
    _SelectCandidateOnCurrentPage(*sel);
  else if (hov)
    _HandleMouseHoverEvent(*hov);
  else if (next || scroll_next)
    _HandleMousePageEvent(next, scroll_next);
}
