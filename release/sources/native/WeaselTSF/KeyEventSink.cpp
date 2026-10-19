#include "stdafx.h"
#include <CandidateLanguageSplit.h>
#include "WeaselIPC.h"
#include "WeaselTSF.h"
#include <KeyEvent.h>
#include "CandidateList.h"
#include <VisibleCandidateActions.h>
#include <ResponseParser.h>

static weasel::KeyEvent prevKeyEvent;
static BOOL prevfEaten = FALSE;
static int keyCountToSimulate = 0;

static bool IsShiftKey(WPARAM key) {
  return key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT;
}

static bool IsSpaceReadingPreviewEnabled() {
  DWORD value = 0;
  DWORD size = sizeof(value);
  return RegGetValueW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod",
                      L"SpaceReadingPreview", RRF_RT_REG_DWORD, nullptr,
                      &value, &size) == ERROR_SUCCESS && value != 0;
}

static bool IsLongPressReadingPreviewEnabled() {
  DWORD value = 0;
  DWORD size = sizeof(value);
  return RegGetValueW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod",
                      L"LongPressReadingPreview", RRF_RT_REG_DWORD, nullptr,
                      &value, &size) == ERROR_SUCCESS && value != 0;
}

static bool IsAltReadingPreviewEnabled() {
  DWORD value = 0;
  DWORD size = sizeof(value);
  return RegGetValueW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod",
                      L"AltReadingPreview", RRF_RT_REG_DWORD, nullptr,
                      &value, &size) == ERROR_SUCCESS && value != 0;
}

static bool IsEnterSubmitsToAppEnabled() {
  DWORD value = 0;
  DWORD size = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER,L"Software\\ZhongriInputMethod",L"EnterMode",
                  RRF_RT_REG_DWORD,nullptr,&value,&size)==ERROR_SUCCESS && ValidEnterMode(value)) return false;
  size=sizeof(value);
  return RegGetValueW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod",
                      L"EnterSubmitsToApp", RRF_RT_REG_DWORD, nullptr,
                      &value, &size) == ERROR_SUCCESS && value != 0;
}

static constexpr ULONG_PTR kReadingPreviewReplayTag = 0x52494D4552504C59ULL;
static constexpr ULONG_PTR kReadingPreviewHoldProbeTag = 0x52494D45484F4C44ULL;

static DWORD WINAPI ReadingPreviewHoldProbeWorker(LPVOID parameter) {
  const WORD key = static_cast<WORD>(reinterpret_cast<ULONG_PTR>(parameter));
  Sleep(370);
  if ((GetAsyncKeyState(key) & 0x8000) == 0)
    return 0;
  INPUT input = {};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = key;
  input.ki.dwExtraInfo = kReadingPreviewHoldProbeTag;
  SendInput(1, &input, sizeof(input));
  return 0;
}

static void ScheduleReadingPreviewHoldProbe(WPARAM key) {
  HANDLE worker = CreateThread(
      nullptr, 0, ReadingPreviewHoldProbeWorker,
      reinterpret_cast<LPVOID>(static_cast<ULONG_PTR>(key)), 0, nullptr);
  if (worker)
    CloseHandle(worker);
}

static DWORD WINAPI ReplayEnterWorker(LPVOID target) {
  // Do not call SendInput while still inside TSF's OnKeyDown callback.  The
  // injected key can synchronously re-enter TSF and leave the IPC session in
  // a half-updated state.  A tiny deferred worker lets the original callback
  // and its edit session finish first.
  Sleep(15);
  if (!target || GetForegroundWindow()!=static_cast<HWND>(target)) return 0;
  INPUT input[2] = {};
  input[0].type = input[1].type = INPUT_KEYBOARD;
  input[0].ki.wVk = input[1].ki.wVk = VK_RETURN;
  input[0].ki.dwExtraInfo = input[1].ki.dwExtraInfo =
      kReadingPreviewReplayTag;
  input[1].ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(2, input, sizeof(INPUT));
  return 0;
}

static void ReplayEnterToHost(HWND target = GetForegroundWindow()) {
  HANDLE worker = CreateThread(nullptr, 0, ReplayEnterWorker, target, 0,
                               nullptr);
  if (worker)
    CloseHandle(worker);
}

void WeaselTSF::_FinishConfiguredEnterReplay() {
  if (_configuredEnterReplay.Take() && GetForegroundWindow()==_configuredEnterTarget)
    ReplayEnterToHost(_configuredEnterTarget);
}

bool WeaselTSF::_HandleConfiguredEnter(ITfContext* context, WPARAM key, LPARAM param, BOOL* eaten) {
  if (key!=VK_RETURN || GetMessageExtraInfo()==static_cast<LPARAM>(kReadingPreviewReplayTag)) return false;
  if (_configuredEnterHeld) { *eaten=TRUE;return true; }
  DWORD mode=0,bytes=sizeof(mode);
  if (RegGetValueW(HKEY_CURRENT_USER,L"Software\\ZhongriInputMethod",L"EnterMode",
                  RRF_RT_REG_DWORD,nullptr,&mode,&bytes)!=ERROR_SUCCESS || !ValidEnterMode(mode) ||
      !_status.composing || !_IsComposing() ||
      (GetKeyState(VK_SHIFT)&0x8000) || (GetKeyState(VK_CONTROL)&0x8000) ||
      (GetKeyState(VK_MENU)&0x8000) || (_cand && _cand->HasNumericComposition())) return false;
  _configuredEnterHeld=true;
  _configuredEnterTarget=GetForegroundWindow();
  if (EnterForwardsToHost(mode)) _configuredEnterReplay.Arm();
  _ProcessKeyEvent(key,param,eaten);
  // Even forward modes consume the physical key in a new client. The edit
  // callback, not a timeout or preedit string, proves commit completion.
  *eaten=TRUE;
  _UpdateComposition(context);
  return true;
}

static bool IsReadingPreviewKey(WPARAM key) {
  return IsShiftKey(key) ||
         (key == VK_SPACE && IsSpaceReadingPreviewEnabled()) ||
         ((key == VK_MENU || key == VK_LMENU || key == VK_RMENU) &&
          IsAltReadingPreviewEnabled());
}


static bool IsAltReadingPreviewKey(WPARAM key) {
  return key == VK_MENU || key == VK_LMENU || key == VK_RMENU;
}

bool WeaselTSF::_HasReadingPreviewCandidates() {
  if (!_cand || !_status.composing || !_IsComposing())
    return false;
  UINT count = 0;
  return SUCCEEDED(_cand->GetCount(&count)) && count > 0;
}

bool WeaselTSF::_HandleReadingPreviewKeyDown(WPARAM key, BOOL* eaten) {
  // Alt used in Ctrl+Alt feature shortcuts is not the reading-preview key.
  if (IsAltReadingPreviewKey(key) && (GetKeyState(VK_CONTROL) & 0x8000)) return false;
  const LPARAM extra = GetMessageExtraInfo();
  if (!IsReadingPreviewKey(key) ||
      extra == static_cast<LPARAM>(kReadingPreviewReplayTag))
    return false;
  const bool hold_probe =
      extra == static_cast<LPARAM>(kReadingPreviewHoldProbeTag);
  if (hold_probe &&
      (!_readingPreviewKeyHeld || _readingPreviewHeldKey != key)) {
    *eaten = TRUE;
    return true;
  }
  if (!_readingPreviewKeyHeld) {
    if (!_HasReadingPreviewCandidates())
      return false;
    _readingPreviewKeyHeld = true;
    _readingPreviewShown = false;
    _readingPreviewHeldKey = key;
    _readingPreviewPressedAt = GetTickCount64();
    // Alt is a dedicated preview key: unlike Shift/Space, it has no short-tap
    // action to preserve, so its reading projection is always immediate.
    if (IsAltReadingPreviewKey(key) ||
        !IsLongPressReadingPreviewEnabled()) {
      _readingPreviewShown = true;
      _cand->SetJapaneseReadingPreview(true);
    } else {
      ScheduleReadingPreviewHoldProbe(key);
    }
  } else if (!_readingPreviewShown && _readingPreviewHeldKey == key &&
             GetTickCount64() - _readingPreviewPressedAt >= 350) {
    _readingPreviewShown = true;
    _cand->SetJapaneseReadingPreview(true);
  }
  *eaten = TRUE;
  return true;
}

bool WeaselTSF::_HandleReadingPreviewKeyUp(WPARAM key, BOOL* eaten) {
  if (!IsReadingPreviewKey(key) || !_readingPreviewKeyHeld ||
      key != _readingPreviewHeldKey)
    return false;
  const bool replay_short_press =
      !IsAltReadingPreviewKey(key) && IsLongPressReadingPreviewEnabled() &&
      !_readingPreviewShown;
  if (_readingPreviewShown)
    _cand->SetJapaneseReadingPreview(false);
  _readingPreviewKeyHeld = false;
  _readingPreviewShown = false;
  _readingPreviewHeldKey = 0;
  _readingPreviewPressedAt = 0;
  *eaten = TRUE;
  if (replay_short_press) {
    if (key == VK_SPACE) {
      // In reading-preview mode a short Space keeps the familiar IME action:
      // choose the first candidate.  Sending Space back through Rime would be
      // swallowed by the preview-mode Lua processor.
      _SelectCandidateOnCurrentPage(0);
      return true;
    }
    INPUT input[2] = {};
    input[0].type = input[1].type = INPUT_KEYBOARD;
    input[0].ki.wVk = input[1].ki.wVk = static_cast<WORD>(key);
    input[0].ki.dwExtraInfo = input[1].ki.dwExtraInfo =
        kReadingPreviewReplayTag;
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, input, sizeof(INPUT));
  }
  return true;
}

void WeaselTSF::_ProcessKeyEvent(WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
  // when _IsKeyboardDisabled don't eat the key,
  // when keyboard closable and keyboard closed, don't eat the key
  if ((_isToOpenClose && !_IsKeyboardOpen()) || _IsKeyboardDisabled()) {
    *pfEaten = FALSE;
    return;
  }

  // if server connection is Not OK, don't eat it.
  if (!_EnsureServerConnected()) {
    *pfEaten = FALSE;
    return;
  }
  weasel::KeyEvent ke;
  GetKeyboardState(_lpbKeyState);
  if (!ConvertKeyEvent(static_cast<UINT>(wParam), lParam, _lpbKeyState, ke)) {
    /* Unknown key event */
    *pfEaten = FALSE;
  } else {
    const bool row_down = ke.keycode == ibus::Page_Down || ke.keycode == ']';
    const bool row_up = ke.keycode == ibus::Page_Up || ke.keycode == '[';
    if (_cand && !(ke.mask & ibus::RELEASE_MASK) &&
        (row_down || row_up || ke.keycode == ibus::Up || ke.keycode == ibus::Down ||
         ke.keycode == ibus::Left || ke.keycode == ibus::Right ||
         ke.keycode == ibus::Home || ke.keycode == ibus::End))
      _cand->ResetSelectionGuard();
    if (_cand && _status.composing &&
        (row_down || row_up) && !weasel::EffectiveCandidateSplitOptions(_cand->style()).enabled &&
        !(ke.mask & (ibus::SHIFT_MASK | ibus::CONTROL_MASK | ibus::MOD1_MASK))) {
      const int target = _cand->GetAdjacentCandidateRowIndex(row_up);
      // If a hot layout is not ready yet, keep the current selection rather
      // than falling back to the server's different six-slot estimate.
      if (target >= -2 && (_cand->style().max_height < 0 || row_down)) {
        if (!(ke.mask & ibus::RELEASE_MASK) && target >= 0)
          weasel::NavigatePaintedCandidateRow(
              m_client, target, _cand->style().max_height < 0, [&] {
                weasel::ResponseParser parser(nullptr,nullptr,&_status,nullptr,
                                               &_cand->style());
                m_client.GetResponseData(std::ref(parser));
              });
        else
          m_client.ProcessKeyEvent(weasel::KeyEvent{ibus::Select, 0});
        *pfEaten = TRUE; prevfEaten = TRUE; prevKeyEvent = ke;
        return;
      }
    }
    // TSF owns the painted candidate surface; the server cannot infer its
    // width/DPI or its actual row boundaries. Never pass invisible ordinals
    // through to Rime's full logical page (which may contain 100 entries).
    if (_cand && _status.composing && ke.keycode >= '1' &&
        ke.keycode <= '9' &&
        !(ke.mask & (ibus::SHIFT_MASK | ibus::CONTROL_MASK | ibus::MOD1_MASK))) {
      const int target = _cand->GetNumericCandidateIndex(ke.keycode - '1');
      if (target == -1 || (target >= 0 &&
          (_cand->style().max_height < 0 || weasel::EffectiveCandidateSplitOptions(_cand->style()).enabled || _cand->IsSelectionGuardActive()))) {
        if (target >= 0 && !(ke.mask & ibus::RELEASE_MASK))
          m_client.SelectCandidateOnCurrentPage(static_cast<size_t>(target));
        // Selection IPC only stages the commit. Fetch a fresh full response
        // using the same inert Select event as mouse selection, without
        // injecting any system keyboard event. Invalid ordinals also need a
        // fresh unchanged response so TSF never replays stale context/commit.
        m_client.ProcessKeyEvent(weasel::KeyEvent{ibus::Select, 0});
        *pfEaten = TRUE;
        prevfEaten = TRUE;
        prevKeyEvent = ke;
        return;
      }
    }
    // cheet key code when vertical auto reverse happened, swap up and down
    if (_cand->GetIsReposition()) {
      if (ke.keycode == ibus::Up)
        ke.keycode = ibus::Down;
      else if (ke.keycode == ibus::Down)
        ke.keycode = ibus::Up;
    }
    if (!keyCountToSimulate)
      *pfEaten = (BOOL)m_client.ProcessKeyEvent(ke);

    if (ke.keycode == ibus::Caps_Lock) {
      if (prevKeyEvent.keycode == ibus::Caps_Lock && prevfEaten == TRUE &&
          (ke.mask & ibus::RELEASE_MASK) && (!keyCountToSimulate)) {
        if ((GetKeyState(VK_CAPITAL) & 0x01)) {
          if (_committed || (!*pfEaten && _status.composing)) {
            keyCountToSimulate = 2;
            INPUT inputs[2];
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki = {VK_CAPITAL, 0, 0, 0, 0};
            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki = {VK_CAPITAL, 0, KEYEVENTF_KEYUP, 0, 0};
            ::SendInput(sizeof(inputs) / sizeof(INPUT), inputs, sizeof(INPUT));
          }
        }
        *pfEaten = TRUE;
      }
      if (keyCountToSimulate)
        keyCountToSimulate--;
    }

    prevfEaten = *pfEaten;
    prevKeyEvent = ke;
  }
}

STDAPI WeaselTSF::OnSetFocus(BOOL fForeground) {
  if (fForeground) {
    _InitPreservedKey();
    m_client.FocusIn();
  }
  else {
    m_client.FocusOut();
    _configuredEnterReplay.Cancel();_configuredEnterHeld=false;
    _AbortComposition();
  }

  return S_OK;
}

/* Some apps sends strange OnTestKeyDown/OnKeyDown combinations:
 *  Some sends OnKeyDown() only. (QQ2012)
 *  Some sends multiple OnTestKeyDown() for a single key event. (MS WORD 2010
 * x64)
 *
 * We assume every key event will eventually cause a OnKeyDown() call.
 * We use _fTestKeyDownPending to omit multiple OnTestKeyDown() calls,
 *  and for OnKeyDown() to check if the key has already been sent to the server.
 */

STDAPI WeaselTSF::OnTestKeyDown(ITfContext* pContext,
                                WPARAM wParam,
                                LPARAM lParam,
                                BOOL* pfEaten) {
  if(wParam==VK_RETURN && GetMessageExtraInfo()==static_cast<LPARAM>(kReadingPreviewReplayTag)) {
    *pfEaten=FALSE;return S_OK;
  }
  _fTestKeyUpPending = FALSE;
  // Reading-preview keys are momentary UI controls, not Rime input.  Letting
  // Space auto-repeat reach _ProcessKeyEvent made the expanded candidate
  // context refresh repeatedly, alternating raw Japanese text and its reading.
  if (_HandleReadingPreviewKeyDown(wParam, pfEaten)) {
    _fTestKeyDownPending = TRUE;
    return S_OK;
  }
  if (_fTestKeyDownPending) {
    *pfEaten = TRUE;
    return S_OK;
  }
  if (_HandleConfiguredEnter(pContext,wParam,lParam,pfEaten)) {
    _fTestKeyDownPending=TRUE;
    return S_OK;
  }
  const bool submit_enter =
      wParam == VK_RETURN && IsEnterSubmitsToAppEnabled() &&
      GetMessageExtraInfo() != static_cast<LPARAM>(kReadingPreviewReplayTag) &&
      _status.composing && _IsComposing() &&
      (!_cand || !_cand->HasNumericComposition());
  if (submit_enter) {
    // Inline preedit already contains the exact raw letters in the host edit
    // range.  Finalize that range without asking Rime to process Return, then
    // replay Return to the host from OnKeyDown.  This deliberately bypasses
    // Lua and leaves the candidate-generation pipeline untouched.
    _AbortComposition(false);
    *pfEaten = TRUE;
    _fTestKeyDownPending = TRUE;
    _replayEnterAfterKeyDown = true;
    return S_OK;
  }
  _ProcessKeyEvent(wParam, lParam, pfEaten);
  _UpdateComposition(pContext);
  if (*pfEaten) {
    _fTestKeyDownPending = TRUE;
  }
  return S_OK;
}

STDAPI WeaselTSF::OnKeyDown(ITfContext* pContext,
                            WPARAM wParam,
                            LPARAM lParam,
                            BOOL* pfEaten) {
  if(wParam==VK_RETURN && GetMessageExtraInfo()==static_cast<LPARAM>(kReadingPreviewReplayTag)) {
    *pfEaten=FALSE;return S_OK;
  }
  _fTestKeyUpPending = FALSE;
  if (_HandleReadingPreviewKeyDown(wParam, pfEaten)) {
    _fTestKeyDownPending = FALSE;
    return S_OK;
  }
  const bool submit_enter_without_test =
      wParam == VK_RETURN && IsEnterSubmitsToAppEnabled() &&
      GetMessageExtraInfo() != static_cast<LPARAM>(kReadingPreviewReplayTag) &&
      _status.composing && _IsComposing() &&
      (!_cand || !_cand->HasNumericComposition());
  if (_fTestKeyDownPending) {
    _fTestKeyDownPending = FALSE;
    *pfEaten = TRUE;
  } else {
    if (_HandleConfiguredEnter(pContext,wParam,lParam,pfEaten)) {
      // The same path is used for hosts that omit OnTestKeyDown.
    } else if (submit_enter_without_test) {
      _AbortComposition(false);
      *pfEaten = TRUE;
      _replayEnterAfterKeyDown = true;
    } else {
      _ProcessKeyEvent(wParam, lParam, pfEaten);
      _UpdateComposition(pContext);
    }
  }
  if (wParam==VK_RETURN) {
    _configuredEnterReplay.KeyDown();
    _FinishConfiguredEnterReplay();
  }
  if (_replayEnterAfterKeyDown) {
    _replayEnterAfterKeyDown = false;
    ReplayEnterToHost();
  }
  return S_OK;
}

STDAPI WeaselTSF::OnTestKeyUp(ITfContext* pContext,
                              WPARAM wParam,
                              LPARAM lParam,
                              BOOL* pfEaten) {
  if(wParam==VK_RETURN && GetMessageExtraInfo()==static_cast<LPARAM>(kReadingPreviewReplayTag)) {
    *pfEaten=FALSE;return S_OK;
  }
  _fTestKeyDownPending = FALSE;
  if (wParam==VK_RETURN && _configuredEnterHeld) {
    _fTestKeyUpPending=TRUE;*pfEaten=TRUE;return S_OK;
  }
  if (_HandleReadingPreviewKeyUp(wParam, pfEaten)) {
    _fTestKeyUpPending = TRUE;
    return S_OK;
  }
  if (_fTestKeyUpPending) {
    *pfEaten = TRUE;
    return S_OK;
  }
  _ProcessKeyEvent(wParam, lParam, pfEaten);
  _UpdateComposition(pContext);
  if (*pfEaten)
    _fTestKeyUpPending = TRUE;
  return S_OK;
}

STDAPI WeaselTSF::OnKeyUp(ITfContext* pContext,
                          WPARAM wParam,
                          LPARAM lParam,
                          BOOL* pfEaten) {
  if(wParam==VK_RETURN && GetMessageExtraInfo()==static_cast<LPARAM>(kReadingPreviewReplayTag)) {
    *pfEaten=FALSE;return S_OK;
  }
  _fTestKeyDownPending = FALSE;
  if (wParam==VK_RETURN && _configuredEnterHeld) {
    _configuredEnterHeld=false;_fTestKeyUpPending=FALSE;*pfEaten=TRUE;return S_OK;
  }
  if (_HandleReadingPreviewKeyUp(wParam, pfEaten)) {
    _fTestKeyUpPending = FALSE;
    return S_OK;
  }
  if (_fTestKeyUpPending) {
    _fTestKeyUpPending = FALSE;
    *pfEaten = TRUE;
  } else {
    _ProcessKeyEvent(wParam, lParam, pfEaten);
    if (!_async_edit)
      _UpdateComposition(pContext);
  }
  return S_OK;
}

STDAPI WeaselTSF::OnPreservedKey(ITfContext* pContext,
                                 REFGUID rguid,
                                 BOOL* pfEaten) {
  *pfEaten = FALSE;
  // Registered feature keys must reach the backend even outside composition.
  static const GUID learningGuid = {0x2e450e2a, 0x868f, 0x448b, {0x9b, 0x53, 0x9e, 0x23, 0x63, 0x49, 0xd1, 0x10}};
  if (IsEqualGUID(rguid, learningGuid) && _learningPreservedBinding && _EnsureServerConnected()) {
    weasel::KeyEvent event(_learningPreservedBinding);
    *pfEaten = m_client.ProcessKeyEvent(event);
    event.mask |= ibus::RELEASE_MASK;
    m_client.ProcessKeyEvent(event);
    _UpdateComposition(pContext);
  }
  return S_OK;
}

BOOL WeaselTSF::_InitKeyEventSink() {
  com_ptr<ITfKeystrokeMgr> pKeystrokeMgr;
  HRESULT hr;

  if (_pThreadMgr->QueryInterface(&pKeystrokeMgr) != S_OK)
    return FALSE;

  hr = pKeystrokeMgr->AdviseKeyEventSink(_tfClientId, (ITfKeyEventSink*)this,
                                         TRUE);

  return (hr == S_OK);
}

void WeaselTSF::_UninitKeyEventSink() {
  com_ptr<ITfKeystrokeMgr> pKeystrokeMgr;

  if (_pThreadMgr->QueryInterface(&pKeystrokeMgr) != S_OK)
    return;

  pKeystrokeMgr->UnadviseKeyEventSink(_tfClientId);
}

BOOL WeaselTSF::_InitPreservedKey() {
  DWORD binding = (12u << 16) | 'L', bytes = sizeof(binding);
  RegGetValueW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod", L"Shortcut_Learning",
               RRF_RT_REG_DWORD, nullptr, &binding, &bytes);
  if (binding == _learningPreservedBinding) return TRUE;
  _UninitPreservedKey();
  if (!binding) return TRUE;
  com_ptr<ITfKeystrokeMgr> manager;
  if (_pThreadMgr->QueryInterface(&manager) != S_OK) return TRUE;
  const UINT code = binding & 0xffff, mask = binding >> 16;
  TF_PRESERVEDKEY key = {};
  key.uVKey = code >= 0xffbe && code <= 0xffc9 ? VK_F1 + code - 0xffbe :
      code == 0xff55 ? VK_PRIOR : code == 0xff56 ? VK_NEXT : code;
  key.uModifiers = ((mask & ibus::CONTROL_MASK) ? TF_MOD_CONTROL : 0) |
      ((mask & ibus::ALT_MASK) ? TF_MOD_ALT : 0) |
      ((mask & ibus::SHIFT_MASK) ? TF_MOD_SHIFT : 0);
  static const GUID guid = {0x2e450e2a, 0x868f, 0x448b, {0x9b, 0x53, 0x9e, 0x23, 0x63, 0x49, 0xd1, 0x10}};
  if (SUCCEEDED(manager->PreserveKey(_tfClientId, guid, &key, L"日语学习模式", 6))) {
    _learningPreservedBinding = binding;
    _learningPreservedKey = key;
  }
  return TRUE;
#if 0
	com_ptr<ITfKeystrokeMgr> pKeystrokeMgr;
	if (_pThreadMgr->QueryInterface(pKeystrokeMgr.GetAddressOf()) != S_OK)
	{
		return FALSE;
	}
	TF_PRESERVEDKEY preservedKeyImeMode;

	/* Define SHIFT ONLY for now */
	preservedKeyImeMode.uVKey = VK_SHIFT;
	preservedKeyImeMode.uModifiers = TF_MOD_ON_KEYUP;

	auto hr = pKeystrokeMgr->PreserveKey(
		_tfClientId,
		GUID_IME_MODE_PRESERVED_KEY,
		&preservedKeyImeMode, L"", 0);
	
	return SUCCEEDED(hr);
#endif
}

void WeaselTSF::_UninitPreservedKey() {
  if (!_learningPreservedBinding) return;
  com_ptr<ITfKeystrokeMgr> manager;
  static const GUID guid = {0x2e450e2a, 0x868f, 0x448b, {0x9b, 0x53, 0x9e, 0x23, 0x63, 0x49, 0xd1, 0x10}};
  if (_pThreadMgr->QueryInterface(&manager) == S_OK) manager->UnpreserveKey(guid, &_learningPreservedKey);
  _learningPreservedBinding = 0;
}
