#include "stdafx.h"
#include "CloudBadge.h"
#include "VerticalLayout.h"

using namespace weasel;

void weasel::VerticalLayout::DoLayout(CDCHandle dc, PDWR pDWR) {
  const int space = _style.hilite_spacing;
  int width = 0, height = real_margin_y;

  if ((_style.hilited_mark_color & 0xff000000)) {
    CSize sg;
    if (candidates_count) {
      if (_style.mark_text.empty())
        GetTextSizeDW(L"|", 1, pDWR->pTextFormat, pDWR, &sg);
      else
        GetTextSizeDW(_style.mark_text, _style.mark_text.length(),
                      pDWR->pTextFormat, pDWR, &sg);
    }

    mark_width = sg.cx;
    mark_height = sg.cy;
    if (_style.mark_text.empty()) {
      mark_width = mark_height / 7;
      if (_style.linespacing && _style.baseline)
        mark_width =
            (int)((float)mark_width / ((float)_style.linespacing / 100.0f));
      mark_width = max(mark_width, 6);
    }
    mark_gap = (_style.mark_text.empty()) ? mark_width
                                          : mark_width + _style.hilite_spacing;
  }
  int base_offset = ((_style.hilited_mark_color & 0xff000000)) ? mark_gap : 0;

  // calc page indicator
  CSize pgszl, pgszr;
  if (!IsInlinePreedit()) {
    GetTextSizeDW(pre, pre.length(), pDWR->pPreeditTextFormat, pDWR, &pgszl);
    GetTextSizeDW(next, next.length(), pDWR->pPreeditTextFormat, pDWR, &pgszr);
  }
  bool page_en = (_style.prevpage_color & 0xff000000) &&
                 (_style.nextpage_color & 0xff000000);
  int pgw = page_en ? pgszl.cx + pgszr.cx + _style.hilite_spacing +
                          _style.hilite_padding_x * 2
                    : 0;
  int pgh = page_en ? max(pgszl.cy, pgszr.cy) : 0;

  /*  preedit and auxiliary rectangle calc start */
  CSize size;
  /* Preedit */
  if (!IsInlinePreedit() && !_context.preedit.str.empty()) {
    size = GetPreeditSize(dc, _context.preedit, pDWR->pPreeditTextFormat, pDWR);
    int szx = pgw, szy = max(size.cy, pgh);
    // icon size higher then preedit text
    int yoffset = (STATUS_ICON_SIZE >= szy && ShouldDisplayStatusIcon())
                      ? (STATUS_ICON_SIZE - szy) / 2
                      : 0;
    _preeditRect.SetRect(real_margin_x, height + yoffset,
                         real_margin_x + size.cx, height + yoffset + size.cy);
    height += szy + 2 * yoffset + _style.spacing;
    width = max(width, real_margin_x * 2 + size.cx + szx);
    if (ShouldDisplayStatusIcon())
      width += STATUS_ICON_SIZE;
    _preeditRect.OffsetRect(offsetX, offsetY);
  }

  /* Auxiliary */
  if (!_context.aux.str.empty()) {
    size = GetPreeditSize(dc, _context.aux, pDWR->pPreeditTextFormat, pDWR);
    // icon size higher then auxiliary text
    int yoffset = (STATUS_ICON_SIZE >= size.cy && ShouldDisplayStatusIcon())
                      ? (STATUS_ICON_SIZE - size.cy) / 2
                      : 0;
    _auxiliaryRect.SetRect(real_margin_x, height + yoffset,
                           real_margin_x + size.cx, height + yoffset + size.cy);
    height += size.cy + 2 * yoffset + _style.spacing;
    width = max(width, real_margin_x * 2 + size.cx);
    _auxiliaryRect.OffsetRect(offsetX, offsetY);
  }
  /*  preedit and auxiliary rectangle calc end */

  /* Candidates */
  int max_candidate_width = 0; /* label + text */
  for (auto i = 0; i < candidates_count && i < MAX_CANDIDATES_COUNT; ++i) {
    if (i > 0)
      height += _style.candidate_spacing;

    int w = real_margin_x + base_offset;
    int candidate_width = base_offset;
    /* Label */
    std::wstring label =
        GetLabelText(labels, i, _style.label_text_format.c_str());
    GetTextSizeDW(label, label.length(), pDWR->pLabelTextFormat, pDWR, &size);
    _candidateLabelRects[i].SetRect(w, height, w + size.cx * labelFontValid,
                                    height + size.cy);
    _candidateLabelRects[i].OffsetRect(offsetX, offsetY);
    w += (size.cx + space) * labelFontValid;
    int text_row_height = size.cy;
    candidate_width += (size.cx + space) * labelFontValid;

    /* Text */
    const std::wstring text = CandidateWithCloudBadge(candidates.at(i).str, comments.at(i).str);
    GetTextSizeDW(text, text.length(), pDWR->pTextFormat, pDWR, &size);
    _candidateTextRects[i].SetRect(w, height, w + size.cx * textFontValid,
                                   height + size.cy);
    _candidateTextRects[i].OffsetRect(offsetX, offsetY);
    w += size.cx * textFontValid;
    text_row_height = max(text_row_height, size.cy);
    candidate_width += size.cx * textFontValid;
    max_candidate_width = max(max_candidate_width, candidate_width);

    /* Comment — placed below text row if comment_below, else to the right */
    bool cmtFontNotTrans =
        (i == id && (_style.hilited_comment_text_color & 0xff000000)) ||
        (i != id && (_style.comment_text_color & 0xff000000));
    int cmt_height = 0;
    const std::wstring comment = WithoutCloudBadgeLine(comments.at(i).str);
    if (!comment.empty() && cmtFontValid && cmtFontNotTrans) {
      GetTextSizeDW(comment, comment.length(), pDWR->pCommentTextFormat, pDWR,
                    &size);
      if (true) {  // always comment below (hardcoded)
        // comment below text row
        int cmt_top = height + text_row_height + _style.hilite_spacing / 2;
        int cmt_left = real_margin_x + base_offset;
        _candidateCommentRects[i].SetRect(cmt_left, cmt_top,
                                          cmt_left + size.cx * cmtFontValid,
                                          cmt_top + size.cy);
        _candidateCommentRects[i].OffsetRect(offsetX, offsetY);
        cmt_height = _style.hilite_spacing / 2 + size.cy;
        max_candidate_width = max(max_candidate_width,
                                  (int)(size.cx * cmtFontValid) + candidate_width - (int)(size.cx * textFontValid));
      } else {
        // comment to the right of text
        w += _style.hilite_spacing;
        _candidateCommentRects[i].SetRect(w, height,
                                          w + size.cx * cmtFontValid,
                                          height + size.cy);
        _candidateCommentRects[i].OffsetRect(offsetX, offsetY);
        w += size.cx * cmtFontValid;
        text_row_height = max(text_row_height, size.cy);
        max_candidate_width = max(max_candidate_width,
                                  candidate_width + _style.hilite_spacing + (int)(size.cx * cmtFontValid));
      }
    } else {
      _candidateCommentRects[i].SetRect(0, height + text_row_height,
                                        0, height + text_row_height);
      _candidateCommentRects[i].OffsetRect(offsetX, offsetY);
    }

    int total_height = text_row_height + cmt_height;

    int hlTop = _candidateTextRects[i].top;
    int hlBot = _candidateTextRects[i].bottom;
    if (_candidateLabelRects[i].Height() > 0) {
      hlTop = min(_candidateLabelRects[i].top, hlTop);
    }
    if (_candidateCommentRects[i].Height() > 0) {
      hlBot = max(hlBot, _candidateCommentRects[i].bottom);
    }
    _candidateRects[i].SetRect(real_margin_x + offsetX, hlTop,
                               width - real_margin_x + offsetX, hlBot);

    width = max(width, w);
    height += total_height;
  }

  width = max(width, max_candidate_width + 2 * real_margin_x);

  /* Trim the last spacing if no candidates */
  if (candidates_count == 0)
    height -= _style.spacing;

  height += real_margin_y;

  if (candidates_count) {
    width = max(width, _style.min_width);
    height = max(height, _style.min_height);
  }
  UpdateStatusIconLayout(&width, &height);
  // candidate rectangle align to right side
  for (auto i = 0; i < candidates_count && i < MAX_CANDIDATES_COUNT; ++i) {
    int right =
        max(_candidateRects[i].right,
            _candidateRects[i].left - real_margin_x + width - real_margin_x);
    _candidateRects[i].right = right;
  }

  _contentSize.SetSize(width + offsetX * 2, height + offsetY * 2);

  /* Highlighted Candidate */

  _highlightRect = _candidateRects[id];
  // calc page indicator
  if (page_en && candidates_count && !_style.inline_preedit) {
    int _prex = _contentSize.cx - offsetX - real_margin_x +
                _style.hilite_padding_x - pgw;
    int _prey = (_preeditRect.top + _preeditRect.bottom) / 2 - pgszl.cy / 2;
    _prePageRect.SetRect(_prex, _prey, _prex + pgszl.cx, _prey + pgszl.cy);
    _nextPageRect.SetRect(_prePageRect.right + _style.hilite_spacing, _prey,
                          _prePageRect.right + _style.hilite_spacing + pgszr.cx,
                          _prey + pgszr.cy);
    if (ShouldDisplayStatusIcon()) {
      _prePageRect.OffsetRect(-STATUS_ICON_SIZE, 0);
      _nextPageRect.OffsetRect(-STATUS_ICON_SIZE, 0);
    }
  }
  // calc roundings start
  _contentRect.SetRect(0, 0, _contentSize.cx, _contentSize.cy);
  // background rect prepare for Hemispherical calculation
  CopyRect(_bgRect, _contentRect);
  _bgRect.DeflateRect(offsetX + 1, offsetY + 1);
  _PrepareRoundInfo(dc);

  // truely draw content size calculation
  _contentRect.DeflateRect(offsetX, offsetY);
}
