#include "stdafx.h"
#include <WeaselUI.h>
#include <CandidateLanguageSplit.h>
#include "WeaselPanel.h"
#include "HorizontalLayout.h"

using namespace weasel;

static std::pair<bool, unsigned> SelectionGuardOptions() {
  HKEY key = nullptr;
  DWORD enabled = 1, duration = 250, bytes = sizeof(DWORD), type = 0;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod", 0,
                    KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
    RegQueryValueExW(key, L"CandidateSelectionGuardEnabled", nullptr, &type,
                     reinterpret_cast<BYTE*>(&enabled), &bytes);
    bytes = sizeof(DWORD);
    RegQueryValueExW(key, L"CandidateSelectionGuardMs", nullptr, &type,
                     reinterpret_cast<BYTE*>(&duration), &bytes);
    RegCloseKey(key);
  }
  return {enabled != 0, max(50u, min(1000u, duration))};
}

static void UIFlowTrace(const std::string& message) {
  return;  // Disabled outside targeted diagnostics.
  wchar_t temp_path[MAX_PATH] = {};
  if (!GetTempPathW(MAX_PATH, temp_path)) return;
  std::wstring path(temp_path);
  path += L"weasel_candidate_expand.log";
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  const std::string line = std::to_string(GetTickCount64()) +
                           " [UI pid=" +
                           std::to_string(GetCurrentProcessId()) + "] " +
                           message + "\r\n";
  DWORD written = 0;
  WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written,
            nullptr);
  CloseHandle(file);
}

static std::string WindowState(HWND hwnd) {
  RECT rc = {};
  GetWindowRect(hwnd, &rc);
  HWND foreground = GetForegroundWindow();
  HWND owner = GetWindow(hwnd, GW_OWNER);
  return " hwnd=" + std::to_string(reinterpret_cast<uintptr_t>(hwnd)) +
         " valid=" + std::to_string(IsWindow(hwnd)) +
         " visible=" + std::to_string(IsWindowVisible(hwnd)) +
         " iconic=" + std::to_string(IsIconic(hwnd)) +
         " rect=" + std::to_string(rc.left) + ":" +
         std::to_string(rc.top) + ":" + std::to_string(rc.right) + ":" +
         std::to_string(rc.bottom) +
         " owner=" + std::to_string(reinterpret_cast<uintptr_t>(owner)) +
         " foreground=" +
         std::to_string(reinterpret_cast<uintptr_t>(foreground));
}

class weasel::UIImpl {
 public:
  WeaselPanel panel;

  UIImpl(weasel::UI& ui) : panel(ui), shown(false) {}
  ~UIImpl() {}
  void Refresh() {
    if (!panel.IsWindow())
      return;
    if (timer) {
      Hide();
      KillTimer(panel.m_hWnd, AUTOHIDE_TIMER);
      timer = 0;
    }
    panel.Refresh();
  }
  void Show();
  void Hide();
  void ShowWithTimeout(size_t millisec);
  bool IsShown() const { return shown; }

  static VOID CALLBACK OnTimer(_In_ HWND hwnd,
                               _In_ UINT uMsg,
                               _In_ UINT_PTR idEvent,
                               _In_ DWORD dwTime);
  static const int AUTOHIDE_TIMER = 20121220;
  static UINT_PTR timer;
  bool shown;
};

UINT_PTR UIImpl::timer = 0;

void UIImpl::Show() {
  if (!panel.IsWindow())
    return;
  UIFlowTrace("SHOW_BEFORE" + WindowState(panel.m_hWnd));
  panel.ShowWindow(SW_SHOWNA);
  shown = true;
  UIFlowTrace("SHOW_AFTER" + WindowState(panel.m_hWnd));
  if (timer) {
    KillTimer(panel.m_hWnd, AUTOHIDE_TIMER);
    timer = 0;
  }
}

void UIImpl::Hide() {
  if (!panel.IsWindow())
    return;
  UIFlowTrace("HIDE_BEFORE" + WindowState(panel.m_hWnd));
  panel.ShowWindow(SW_HIDE);
  shown = false;
  UIFlowTrace("HIDE_AFTER" + WindowState(panel.m_hWnd));
  if (timer) {
    KillTimer(panel.m_hWnd, AUTOHIDE_TIMER);
    timer = 0;
  }
}

void UIImpl::ShowWithTimeout(size_t millisec) {
  if (!panel.IsWindow())
    return;
  DLOG(INFO) << "ShowWithTimeout: " << millisec;
  panel.ShowWindow(SW_SHOWNA);
  shown = true;
  SetTimer(panel.m_hWnd, AUTOHIDE_TIMER, static_cast<UINT>(millisec),
           &UIImpl::OnTimer);
  timer = UINT_PTR(this);
}
VOID CALLBACK UIImpl::OnTimer(_In_ HWND hwnd,
                              _In_ UINT uMsg,
                              _In_ UINT_PTR idEvent,
                              _In_ DWORD dwTime) {
  DLOG(INFO) << "OnTimer:";
  KillTimer(hwnd, idEvent);
  UIImpl* self = (UIImpl*)timer;
  timer = 0;
  if (self) {
    self->Hide();
    self->shown = false;
  }
}

bool UI::Create(HWND parent) {
  if (pimpl_) {
    pimpl_->panel.Create(
        parent, 0, 0, WS_POPUP,
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        0U, 0);
    return true;
  }

  pimpl_ = new UIImpl(*this);
  if (!pimpl_)
    return false;

  pimpl_->panel.Create(
      parent, 0, 0, WS_POPUP,
      WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
      0U, 0);
  return true;
}

void UI::Destroy(bool full) {
  if (pimpl_) {
    // destroy panel
    if (pimpl_->panel.IsWindow()) {
      pimpl_->panel.DestroyWindow();
    }
    if (full) {
      delete pimpl_;
      pimpl_ = 0;
      pDWR.reset();
    }
  }
}

bool UI::GetIsReposition() {
  if (pimpl_)
    return pimpl_->panel.GetIsReposition();
  else
    return false;
}

void UI::Show() {
  if (pimpl_) {
    pimpl_->Show();
  }
}

void UI::Hide() {
  if (pimpl_) {
    pimpl_->Hide();
  }
}

void UI::ShowWithTimeout(size_t millisec) {
  if (pimpl_) {
    pimpl_->ShowWithTimeout(millisec);
  }
}

bool UI::IsCountingDown() const {
  return pimpl_ && pimpl_->timer != 0;
}

bool UI::IsShown() const {
  return pimpl_ && pimpl_->IsShown();
}

bool UI::HasWindow() const {
  return pimpl_ && pimpl_->panel.IsWindow();
}

void UI::Refresh() {
  if (pimpl_) {
    UIFlowTrace("REFRESH_REQUEST");
    pimpl_->Refresh();
  }
}

void UI::SetJapaneseReadingPreview(bool active) {
  if (pimpl_ && pimpl_->panel.IsWindow())
    pimpl_->panel.SetJapaneseReadingPreview(active);
}

std::vector<std::vector<int>> UI::GetExpandedCandidateRows() const {
  if (!pimpl_ || !pimpl_->panel.IsWindow())
    return {};
  return pimpl_->panel.GetExpandedCandidateRows();
}

std::vector<std::vector<int>> UI::MeasureCandidateRows(const CandidateInfo& candidates,const UIStyle* renderingStyle) {
  UIStyle style=renderingStyle?*renderingStyle:style_;
  if(style.layout_type!=UIStyle::LAYOUT_HORIZONTAL)return {};
  // TSF clients suppress the server surface, so its cached font resources may
  // belong to an older style. Measure with the current session's actual fonts.
  const auto resources=std::make_shared<DirectWriteResources>(style,96);
  Context context;context.cinfo=candidates;Status status;status.composing=true;
  HorizontalLayout measured(style,context,status,resources);
  HDC dc=CreateCompatibleDC(nullptr);
  measured.DoLayout(dc,resources);
  DeleteDC(dc);
  return measured.expanded_candidate_rows;
}

RECT UI::GetLanguageSplitDivider() const {
  return pimpl_&&pimpl_->panel.IsWindow()?pimpl_->panel.GetLanguageSplitDivider():RECT{0,0,0,0};
}
bool UI::HasSuccessfulPaint() const {return pimpl_&&pimpl_->panel.IsWindow()&&pimpl_->panel.HasSuccessfulPaint();}

int UI::GetVisibleCandidateCount() const {
  return pimpl_ && pimpl_->panel.IsWindow()
             ? pimpl_->panel.GetVisibleCandidateCount() : -1;
}

int UI::GetNumericCandidateIndex(int ordinal) const {
  if (ordinal < 0 || ordinal >= 9)
    return -1;
  if (style_.max_height < 0 || EffectiveCandidateSplitOptions(style_).enabled) {
    const auto rows = GetExpandedCandidateRows();
    for (const auto& row : rows) {
      if (std::find(row.begin(), row.end(), ctx_.cinfo.highlighted) != row.end())
        return ordinal < static_cast<int>(row.size()) ? row[ordinal] : -1;
    }
    return -1;
  }
  const int count = GetVisibleCandidateCount();
  return count < 0 ? -2 : ordinal < count ? ordinal : -1;
}

int UI::GetProtectedNumericCandidateIndex(int ordinal) const {
  // Expanded labels describe the currently painted row, not the preceding
  // compact row or a previous asynchronous row layout.
  if (style_.max_height < 0 || EffectiveCandidateSplitOptions(style_).enabled) return GetNumericCandidateIndex(ordinal);
  const auto options = SelectionGuardOptions();
  return selection_guard_.Resolve(ordinal, GetNumericCandidateIndex(ordinal),
                                  GetTickCount64(), options.second, options.first);
}

bool UI::IsSelectionGuardActive() const {
  const auto options = SelectionGuardOptions();
  return selection_guard_.Active(GetTickCount64(), options.second, options.first);
}

int UI::GetAdjacentCandidateRowIndex(bool backward) const {
  if (style_.max_height >= 0)
    return backward ? -1 : GetVisibleCandidateCount();
  const auto rows = GetExpandedCandidateRows();
  for (size_t row = 0; row < rows.size(); ++row) {
    if (std::find(rows[row].begin(), rows[row].end(), ctx_.cinfo.highlighted) == rows[row].end()) continue;
    if (backward) {
      if (row) return rows[row - 1].front();
      return ctx_.cinfo.currentPage > 0 ? -3 : -1;
    }
    if (row + 1 < rows.size()) return rows[row + 1].front();
    return ctx_.cinfo.is_last_page ? -1 : -3;
  }
  return -2;
}

void UI::UpdateInputPosition(RECT const& rc) {
  if (pimpl_ && pimpl_->panel.IsWindow()) {
    pimpl_->panel.MoveTo(rc);
  }
}

void UI::Update(const Context& ctx, const Status& status) {
  const bool unchanged = ctx_ == ctx && status_ == status;
  UIFlowTrace("UPDATE same=" + std::to_string(unchanged) +
              " highlighted=" + std::to_string(ctx.cinfo.highlighted) +
              " candidates=" + std::to_string(ctx.cinfo.candies.size()) +
              " maxh=" + std::to_string(style_.max_height));
  if (unchanged) {
    // A hot appearance update can change only the style.  Candidate text and
    // composition stay identical, but the existing panel still needs to
    // rebuild its fonts, layout, size, and colors in this very window.
    if (style_ != ostyle_)
      Refresh();
    return;
  }
  ctx_ = ctx;
  status_ = status;
  if (style_.candidate_abbreviate_length > 0) {
    for (auto& c : ctx_.cinfo.candies) {
      if (c.str.length() > (size_t)style_.candidate_abbreviate_length) {
        c.str =
            c.str.substr(0, (size_t)style_.candidate_abbreviate_length - 1) +
            L"..." + c.str.substr(c.str.length() - 1);
      }
    }
  }
  Refresh();
  std::vector<std::wstring> words, ordinals(9);
  for (const auto& word : ctx.cinfo.candies) words.push_back(word.str);
  for (int ordinal = 0; ordinal < 9; ++ordinal) {
    const int index = GetNumericCandidateIndex(ordinal);
    if (index >= 0 && index < static_cast<int>(words.size())) ordinals[ordinal] = words[index];
  }
  const auto options = SelectionGuardOptions();
  selection_guard_.Observe(status.composing ? ctx.preedit.str : L"", words, ordinals,
                            ctx.cinfo.currentPage + (style_.max_height < 0 ? 100000 : 0),
                            ctx.cinfo.highlighted, GetTickCount64(), options.second, options.first);
}
