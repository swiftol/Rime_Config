#include "stdafx.h"
#include "WeaselTSF.h"
#include "CandidateList.h"
#include "ResponseParser.h"

STDAPI WeaselTSF::DoEditSession(TfEditCookie ec) {
  // get commit string from server
  std::wstring commit;
  weasel::Config config;
  auto context = std::make_shared<weasel::Context>();
  weasel::ResponseParser parser(&commit, context.get(), &_status, &config,
                                &_cand->style());

  bool ok = m_client.GetResponseData(std::ref(parser));
  _async_edit = false;
  if(ok)_SyncHoldTimer();else _StopHoldTimer();

  _UpdateLanguageBar(_status);

  if (ok) {
    if (!commit.empty()) {
      // For auto-selecting, commit and preedit can both exist.
      // Commit and close the original composition first.
      if (_configuredEnterReplay.armed || commit.find_first_of(L"\r\n")!=std::wstring::npos) {
        _committed=_CommitConfiguredEnter(ec,commit);
        if (!_committed) _configuredEnterReplay.Cancel();
      } else {
      if (!_IsComposing()) {
        _StartComposition(_pEditSessionContext,
                          _fCUASWorkaroundEnabled && !config.inline_preedit);
      }
      _InsertText(_pEditSessionContext, commit);
      _EndComposition(_pEditSessionContext, false);
      _committed = TRUE;
      }
    } else {
      _committed = FALSE;
    }
    if (_status.composing && !_IsComposing()) {
      _StartComposition(_pEditSessionContext,
                        _fCUASWorkaroundEnabled && !config.inline_preedit);
    } else if (!_status.composing && _IsComposing()) {
      _EndComposition(_pEditSessionContext, true);
    }
    if (_IsComposing() && config.inline_preedit) {
      _ShowInlinePreedit(_pEditSessionContext, context);
    }
    _UpdateCompositionWindow(_pEditSessionContext);
  }

  _UpdateUI(*context, _status);

  if (_configuredEnterReplay.armed) {
    _configuredEnterReplay.CommitApplied(ok && !commit.empty() && !_status.composing);
    _FinishConfiguredEnterReplay();
  }

  return TRUE;
}
