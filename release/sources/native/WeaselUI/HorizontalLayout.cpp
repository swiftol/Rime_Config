#include "stdafx.h"
#include <CandidateLanguageSplit.h>
#include <SplitTextLayout.h>
#include "HorizontalLayout.h"
#include "CloudBadge.h"

using namespace weasel;

static int GridWidthUnits(const std::wstring& text) {
  int width_units = 0;
  for (wchar_t ch : text)
    width_units += (ch >= 0x2e80 || ch > 0xff) ? 2 : 1;
  return width_units;
}

static std::wstring StripTranslationPrefixes(std::wstring text) {
  // Language/reading markers are transport-only metadata and must never be
  // measured or painted by the expanded horizontal/grid layout.
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
  return WithoutCloudBadgeLine(text);
}

static std::wstring HiddenGridReading(const std::wstring& comment) {
  const std::wstring marker = L"[[RIME_JR:";
  const size_t begin = comment.find(marker);
  if (begin == std::wstring::npos) return L"";
  const size_t end = comment.find(L"]]", begin + marker.length());
  if (end == std::wstring::npos) return L"";
  return comment.substr(begin + marker.length(),
                        end - begin - marker.length());
}

static int CandidateGridSpan(const std::wstring& text,
                             const std::wstring& raw_comment,
                             bool include_comment) {
  // Occupancy follows the widest visible line, not only the candidate text.
  // This keeps a short kanji spelling (共有) as wide as its longer reading
  // (きょうゆう) and prevents one from being ellipsized while the other fits.
  int width_units = GridWidthUnits(text);
  if (!include_comment)
    return max(1, min(6, (width_units + 9) / 10));
  std::wstring comment = StripTranslationPrefixes(raw_comment);
  size_t start = 0;
  while (start <= comment.size()) {
    const size_t end = comment.find(L'\n', start);
    std::wstring line = comment.substr(
        start, end == std::wstring::npos ? std::wstring::npos : end - start);
    width_units = max(width_units, GridWidthUnits(line));
    if (end == std::wstring::npos) break;
    start = end + 1;
  }
  const std::wstring hidden_reading = HiddenGridReading(raw_comment);
  if (!hidden_reading.empty())
    width_units = max(width_units,
                      GridWidthUnits(L"[" + hidden_reading + L"]"));
  // One 100px cell safely carries roughly ten ASCII width-units after its
  // label/padding.  Long annotations start at two cells and may grow further
  // instead of being shortened with an ellipsis.
  return max(1, min(6, (width_units + 9) / 10));
}

static void GridLayoutTrace(const std::string& message) {
  // Disabled in normal use.  The automated layout test enables this registry
  // flag before loading the TSF DLL, so the production binary can prove exact
  // coordinates without carrying permanent high-frequency logging overhead.
  static const bool enabled = [] {
    DWORD value = 0;
    DWORD size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\ZhongriInputMethod",
                        L"LayoutTrace", RRF_RT_REG_DWORD, nullptr, &value,
                        &size) == ERROR_SUCCESS &&
           value != 0;
  }();
  if (!enabled) return;
  wchar_t temp_path[MAX_PATH] = {};
  if (!GetTempPathW(MAX_PATH, temp_path)) return;
  std::wstring path(temp_path);
  path += L"weasel_candidate_expand.log";
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  const std::string line = std::to_string(GetTickCount64()) +
                           " [LAYOUT pid=" +
                           std::to_string(GetCurrentProcessId()) + "] " +
                           message + "\r\n";
  DWORD written = 0;
  WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written,
            nullptr);
  CloseHandle(file);
}

void HorizontalLayout::DoLanguageSplitLayout(CDCHandle dc, PDWR pDWR) {
  language_split_enabled=true;
  const auto options = EffectiveCandidateSplitOptions(_style);
  const bool compact=_style.max_height>=0;
  const int count = min(candidates_count, MAX_CANDIDATES_COUNT);
  std::vector<bool> japanese;
  for (int i=0;i<count;++i)
    japanese.push_back(SplitCandidateJapanese(candidates[i].str,
        i<int(comments.size())?comments[i].str:L""));
  int width=max(_style.min_width,max(-_style.max_height,int(600*pDWR->dpiScaleLayout)));
  CSize labelSize,textSize;
  GetTextSizeDW(L"9",1,pDWR->pLabelTextFormat,pDWR,&labelSize);
  GetTextSizeDW(L"国",1,pDWR->pTextFormat,pDWR,&textSize);
  const int usable=max(90,width-2*real_margin_x);
  int header[2]={0,0};
  const int maxLeft=min(8,(9*options.left+options.left+options.right-1)/(options.left+options.right));
  const bool hasChinese=std::find(japanese.begin(),japanese.end(),false)!=japanese.end();
  const bool hasJapanese=std::find(japanese.begin(),japanese.end(),true)!=japanese.end();
  // Header topology is expressed in logical units, independent of monitor
  // scaling and per-glyph pixel rounding. Paint still uses native DPI below.
  const float logicalWidth=float(max(_unscaledStyle.min_width,max(-_unscaledStyle.max_height,600)));
  const float logicalUsable=logicalWidth-2*max(abs(_unscaledStyle.margin_x),_unscaledStyle.hilite_padding_x);
  auto logicalTextWidth=[&](const std::wstring& text,IDWriteTextFormat1* format){
    if(text.empty()||!format)return 0.0f;
    if(FAILED(pDWR->CreateTextLayout(text.c_str(),int(text.size()),format,100000.0f,100000.0f)))return 0.0f;
    const DWRITE_TEXT_RANGE range={0,UINT32(text.size())};
    pDWR->pTextLayout->SetFontSize(format->GetFontSize()/pDWR->dpiScaleLayout,range);
    pDWR->pTextLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TEXT_METRICS metrics={};DWRITE_OVERHANG_METRICS overhang={};
    pDWR->GetLayoutMetrics(&metrics);pDWR->GetLayoutOverhangMetrics(&overhang);
    return metrics.widthIncludingTrailingWhitespace+max(0.0f,overhang.left)+max(0.0f,overhang.right);
  };
  const float logicalLabel=logicalTextWidth(L"9",pDWR->pLabelTextFormat.Get());
  for(int side=0;side<2;++side){
    const bool both=hasChinese&&hasJapanese;
    const float budget=both?logicalUsable*(side?options.right:options.left)/(options.left+options.right)-16:logicalUsable;
    const int limit=both?(side?9-maxLeft:maxLeft):9;
    float widest=0;
    for(int i=0;i<count&&header[side]<limit;++i){
      if(japanese[i]!=(side==1))continue;
      const auto text=CandidateWithCloudBadge(candidates[i].str,i<int(comments.size())?comments[i].str:L"");
      const float needed=max(widest,logicalTextWidth(text,pDWR->pTextFormat.Get())+logicalLabel+_unscaledStyle.hilite_spacing+_unscaledStyle.candidate_spacing);
      if(header[side]>0&&needed*(header[side]+1)>budget)break;
      widest=needed;++header[side];
    }
  }
  const auto plan = BuildCandidateSplitPlan(japanese,options,_context.cinfo.currentPage==0,header[0],header[1]);
  expanded_candidate_rows=plan.rows;
  int active=0;
  for(int row=0;row<int(plan.rows.size());++row)
    if(std::find(plan.rows[row].begin(),plan.rows[row].end(),id)!=plan.rows[row].end())active=row;
  grid_total_rows=int(plan.rows.size());
  grid_first_visible_row=compact?active:max(0,active-4);
  grid_visible_rows=min(compact?1:5,grid_total_rows-grid_first_visible_row);
  visible_candidates=count;
  _preeditRect.SetRectEmpty();_auxiliaryRect.SetRectEmpty();
  _statusIconRect.SetRectEmpty();_prePageRect.SetRectEmpty();_nextPageRect.SetRectEmpty();
  _highlightRect.SetRectEmpty();
  for(int i=0;i<MAX_CANDIDATES_COUNT;++i) {
    _candidateRects[i].SetRectEmpty();_candidateTextRects[i].SetRectEmpty();
    _candidateLabelRects[i].SetRectEmpty();_candidateCommentRects[i].SetRectEmpty();
    _displayLabels[i].clear();_roundInfo[i]=IsToRoundStruct();
  }
  const int left=offsetX+real_margin_x;
  const int gap=max(6,int(8*pDWR->dpiScaleLayout));
  int y=offsetY+real_margin_y;
  if(!IsInlinePreedit()&&!_context.preedit.str.empty()) {
    const auto sz=GetPreeditSize(dc,_context.preedit,pDWR->pPreeditTextFormat,pDWR);
    _preeditRect.SetRect(left,y,left+min(sz.cx,usable),y+sz.cy);
    y+=sz.cy+_style.spacing;
  }
  if(!_context.aux.str.empty()) {
    const auto sz=GetPreeditSize(dc,_context.aux,pDWR->pPreeditTextFormat,pDWR);
    _auxiliaryRect.SetRect(left,y,left+min(sz.cx,usable),y+sz.cy);
    y+=sz.cy+_style.spacing;
  }
  auto wrappedHeight=[&](const std::wstring& text, IDWriteTextFormat1* format, int cellWidth) {
    if(text.empty()||!format)return 0;
    if(FAILED(pDWR->CreateTextLayout(text.c_str(),int(text.size()),format,
                                   float(max(1,cellWidth)),100000.0f)))return 0;
    ConfigureSplitTextLayout(pDWR->pTextLayout.Get());
    DWRITE_TEXT_METRICS metrics={};
    if(FAILED(pDWR->GetLayoutMetrics(&metrics)))return 0;
    return int(ceil(metrics.height));
  };
  for(int row=grid_first_visible_row;row<grid_first_visible_row+grid_visible_rows;++row) {
    const auto& region=plan.regions[row];
    const bool mixed=region.left>0&&region.right>0;
    const int divider=left+usable*(region.borrowed?region.left:options.left)/(region.borrowed?region.left+region.right:options.left+options.right);
    const int rowTop=y;
    int rowHeight=max(labelSize.cy,textSize.cy);
    for(const auto& cell:plan.cells) {
      if(cell.row!=row)continue;
      const int i=cell.index;
      const int start=mixed&&cell.japanese?divider+gap:left;
      const int end=mixed&&!cell.japanese?divider-gap:left+usable;
      const int x=start+(end-start)*cell.column/cell.capacity;
      const int right=start+(end-start)*(cell.column+1)/cell.capacity-_style.candidate_spacing;
      const auto& ids=plan.rows[row];
      if(row==active)_displayLabels[i]=std::to_wstring(int(std::find(ids.begin(),ids.end(),i)-ids.begin())+1);
      _candidateLabelRects[i].SetRect(x,y,min(right,x+labelSize.cx),y+labelSize.cy);
      const int textLeft=min(right,x+labelSize.cx+_style.hilite_spacing);
      const int textHeight=max(textSize.cy,wrappedHeight(
          CandidateWithCloudBadge(candidates[i].str,i<int(comments.size())?comments[i].str:L""),
          pDWR->pTextFormat.Get(),right-textLeft));
      _candidateTextRects[i].SetRect(textLeft,y,right,y+textHeight);
      rowHeight=max(rowHeight,textHeight);
      const std::wstring comment=i<int(comments.size())?StripTranslationPrefixes(comments[i].str):L"";
      if(!comment.empty()&&cmtFontValid) {
        const int commentLeft=_style.expanded_comment_align_label?x:textLeft;
        const int cy=y+max(textHeight,labelSize.cy)+_style.hilite_spacing/2;
        const int commentHeight=wrappedHeight(comment,pDWR->pCommentTextFormat.Get(),right-commentLeft);
        _candidateCommentRects[i].SetRect(commentLeft,cy,right,cy+commentHeight);
        rowHeight=max(rowHeight,cy+commentHeight-y);
      }
      _candidateRects[i].SetRect(x,y,right,y+rowHeight);
    }
    for(int i:plan.rows[row])_candidateRects[i].bottom=y+rowHeight;
    y+=rowHeight+max(_style.spacing,2*_style.hilite_padding_y);
    if(mixed){language_split_dividers.emplace_back(divider,rowTop,divider+1,y);if(language_split_divider.IsRectEmpty())language_split_divider=language_split_dividers.back();}
  }
  if(id>=0&&id<count)_highlightRect=_candidateRects[id];
  int height=y+real_margin_y;
  UpdateStatusIconLayout(&width,&height);
  _contentSize.SetSize(width+2*offsetX,height+offsetY);
  _contentRect.SetRect(0,0,_contentSize.cx,_contentSize.cy);
  _bgRect=_contentRect;
}

void HorizontalLayout::DoLayout(CDCHandle dc, PDWR pDWR) {
  language_split_divider.SetRectEmpty();
  language_split_dividers.clear();
  language_split_enabled=false;
  if(EffectiveCandidateSplitOptions(_style).enabled) {DoLanguageSplitLayout(dc,pDWR);return;}
  for (auto& label : _displayLabels) label.clear();
  CSize size;
  int width = offsetX + real_margin_x, height = offsetY + real_margin_y;
  int w = offsetX + real_margin_x;

  /* calc mark_text sizes */
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
  int pgw = page_en ? (pgszl.cx + pgszr.cx + _style.hilite_spacing +
                       _style.hilite_padding_x * 2)
                    : 0;
  int pgh = page_en ? max(pgszl.cy, pgszr.cy) : 0;

  /* Preedit */
  if (!IsInlinePreedit() && !_context.preedit.str.empty()) {
    size = GetPreeditSize(dc, _context.preedit, pDWR->pPreeditTextFormat, pDWR);
    int szx = pgw, szy = max(size.cy, pgh);
    // icon size higher then preedit text
    int yoffset = (STATUS_ICON_SIZE >= szy && ShouldDisplayStatusIcon())
                      ? (STATUS_ICON_SIZE - szy) / 2
                      : 0;
    _preeditRect.SetRect(w, height + yoffset, w + size.cx,
                         height + yoffset + size.cy);
    height += szy + 2 * yoffset + _style.spacing;
    width = max(width, real_margin_x * 2 + size.cx + szx);
    if (ShouldDisplayStatusIcon())
      width += STATUS_ICON_SIZE;
  }

  /* Auxiliary */
  if (!_context.aux.str.empty()) {
    size = GetPreeditSize(dc, _context.aux, pDWR->pPreeditTextFormat, pDWR);
    // icon size higher then auxiliary text
    int yoffset = (STATUS_ICON_SIZE >= size.cy && ShouldDisplayStatusIcon())
                      ? (STATUS_ICON_SIZE - size.cy) / 2
                      : 0;
    _auxiliaryRect.SetRect(w, height + yoffset, w + size.cx,
                           height + yoffset + size.cy);
    height += size.cy + 2 * yoffset + _style.spacing;
    width = max(width, real_margin_x * 2 + size.cx);
  }

  int row_cnt = 0;
  const bool expanded_grid = _style.max_height < 0;
  const int grid_width = expanded_grid ? max(_style.min_width,
      max(-_style.max_height, static_cast<int>(600 * pDWR->dpiScaleLayout))) : 0;
  const int grid_columns = 6;
  const int grid_visible_rows = 5;
  const int grid_cell_width = expanded_grid ? grid_width / grid_columns : 0;
  // Reuse the actual compact layout, including its width cutoff, annotations,
  // DPI rounding and natural-width cells. Never guess a fixed header count.
  Context compact_context = _context;
  std::unique_ptr<HorizontalLayout> compact_layout;
  int compact_head_count = 0;
  if (expanded_grid && candidates_count && _context.cinfo.currentPage == 0) {
    UIStyle compact_style = _unscaledStyle;
    compact_style.max_height = compact_style.max_height == -1
                                  ? 0 : -compact_style.max_height;
    if (compact_context.cinfo.highlighted >= 9)
      compact_context.cinfo.highlighted = 0;
    compact_layout.reset(new HorizontalLayout(compact_style, compact_context,
                                              _status, pDWR));
    compact_layout->DoLayout(dc, pDWR);
    compact_head_count = compact_layout->visible_candidates;
  }
  // Only the expanded view uses the six-column grid.  The normal one-line
  // view remains content-sized and may show fewer candidates when long text
  // reaches the configured maximum width.
  // Never derive this from the current label. Empty labels can be measured as
  // zero during one refresh and non-zero after the active row changes, which
  // makes all candidate text oscillate horizontally.
  const int grid_label_slot_width =
      expanded_grid ? max(18, _style.label_font_point + 6) : 0;
  int grid_slot[MAX_CANDIDATES_COUNT] = {0};
  int grid_span[MAX_CANDIDATES_COUNT] = {0};
  int grid_abs_row[MAX_CANDIDATES_COUNT] = {0};
  int grid_row_number[MAX_CANDIDATES_COUNT] = {0};
  int grid_measured_width[MAX_CANDIDATES_COUNT] = {0};
  int number_in_row = 0;
  int previous_row = -1;
  expanded_candidate_rows.clear();
  if (expanded_grid) {
    int next_slot = compact_head_count ? grid_columns : 0;
    for (int i = 0; i < candidates_count && i < MAX_CANDIDATES_COUNT; ++i) {
      if (i < compact_head_count) {
        if (expanded_candidate_rows.empty()) expanded_candidate_rows.resize(1);
        expanded_candidate_rows[0].push_back(i);
        grid_row_number[i] = i + 1;
        continue;
      }
      const std::wstring grid_text = CandidateWithCloudBadge(candidates.at(i).str, comments.at(i).str);
      const std::wstring& grid_comment = comments.at(i).str;
      int span = CandidateGridSpan(grid_text, grid_comment,
                                   _style.expanded_comment_width);
      // Character-count estimates are not reliable for mixed CJK/Latin text.
      // Measure every visible line with the exact DirectWrite formats used to
      // paint it, then allocate from the widest of candidate/comment/reading.
      int widest_px = 0;
      CSize measured;
      GetTextSizeDW(grid_text, grid_text.length(), pDWR->pTextFormat, pDWR,
                    &measured);
      widest_px = max(widest_px, measured.cx);
      if (_style.expanded_comment_width) {
        std::wstring measured_comment = StripTranslationPrefixes(grid_comment);
        size_t measured_begin = 0;
        while (measured_begin <= measured_comment.size()) {
          const size_t measured_end =
              measured_comment.find(L'\n', measured_begin);
          const std::wstring line = measured_comment.substr(
              measured_begin,
              measured_end == std::wstring::npos
                  ? std::wstring::npos
                  : measured_end - measured_begin);
          GetTextSizeDW(line, line.length(), pDWR->pCommentTextFormat, pDWR,
                        &measured);
          widest_px = max(widest_px, measured.cx);
          if (measured_end == std::wstring::npos) break;
          measured_begin = measured_end + 1;
        }
        const std::wstring hidden_reading = HiddenGridReading(grid_comment);
        if (!hidden_reading.empty()) {
          const std::wstring reading_line = L"[" + hidden_reading + L"]";
          GetTextSizeDW(reading_line, reading_line.length(),
                        pDWR->pCommentTextFormat, pDWR, &measured);
          widest_px = max(widest_px, measured.cx);
        }
      }
      const int content_overhead =
          grid_label_slot_width + _style.hilite_spacing +
          _style.candidate_spacing;
      span = max(span,
                 min(grid_columns,
                     max(1, (widest_px + content_overhead +
                             grid_cell_width - 1) /
                                grid_cell_width)));
      grid_measured_width[i] = widest_px;
      int column = next_slot % grid_columns;
      if (column + span > grid_columns) {
        next_slot += grid_columns - column;
        column = 0;
      }
      grid_slot[i] = next_slot;
      grid_span[i] = span;
      grid_abs_row[i] = next_slot / grid_columns;
      if (grid_abs_row[i] >=
          static_cast<int>(expanded_candidate_rows.size()))
        expanded_candidate_rows.resize(grid_abs_row[i] + 1);
      expanded_candidate_rows[grid_abs_row[i]].push_back(i);
      next_slot += span;
      if (grid_abs_row[i] != previous_row) {
        previous_row = grid_abs_row[i];
        number_in_row = 0;
      }
      grid_row_number[i] = ++number_in_row;
    }
    std::string trace = "LAYOUT highlighted=" + std::to_string(id) + " items=";
    for (int i = 0; i < candidates_count && i < MAX_CANDIDATES_COUNT; ++i) {
      if (i) trace += ",";
      trace += std::to_string(i) + "@r" + std::to_string(grid_abs_row[i]) +
               "c" + std::to_string(grid_slot[i] % grid_columns) + "s" +
               std::to_string(grid_span[i]) + "w" +
               std::to_string(grid_measured_width[i]) + "t" +
               std::to_string(candidates.at(i).str.size()) + "m" +
               std::to_string(comments.at(i).str.size());
    }
    GridLayoutTrace(trace);
  }
  const int active_grid_row = expanded_grid ? grid_abs_row[id] : 0;
  const int first_grid_row = expanded_grid
                                 ? max(0, active_grid_row -
                                              (grid_visible_rows - 1))
                                 : 0;
  int grid_start = 0;
  int grid_end = candidates_count;
  if (expanded_grid) {
    while (grid_start < candidates_count &&
           grid_abs_row[grid_start] < first_grid_row)
      ++grid_start;
    grid_end = grid_start;
    while (grid_end < candidates_count &&
           grid_abs_row[grid_end] < first_grid_row + grid_visible_rows)
      ++grid_end;
    grid_total_rows = candidates_count ? grid_abs_row[candidates_count - 1] + 1 : 0;
    grid_first_visible_row = first_grid_row;
    this->grid_visible_rows = min(grid_visible_rows, grid_total_rows);
  } else {
    grid_total_rows = 0;
    grid_first_visible_row = 0;
    this->grid_visible_rows = 0;
  }
  int max_width_of_rows = 0;
  int height_of_rows[MAX_CANDIDATES_COUNT] = {0};    // height of every row
  int row_of_candidate[MAX_CANDIDATES_COUNT] = {0};  // row info of every cand
  int mintop_of_rows[MAX_CANDIDATES_COUNT] = {0};
  // The compact candidate bar uses the number keys directly.  Keep it to
  // 1..9 so labels never wrap through 0 and back to 1/2.  The expanded grid
  // remains independent and may still expose the full scrollable result set.
  const int compact_candidate_limit = 9;
  const int layout_candidate_count =
      expanded_grid ? candidates_count
                    : min(candidates_count, compact_candidate_limit);
  int visible_count = layout_candidate_count;
  // only when there are candidates
  if (candidates_count) {
    visible_count = expanded_grid ? grid_end : layout_candidate_count;
    visible_candidates = visible_count; // initialize
    w = offsetX + real_margin_x;
    for (auto i = 0; i < layout_candidate_count &&
                     i < MAX_CANDIDATES_COUNT; ++i) {
      if (expanded_grid) {
        if (i < grid_start || i >= visible_count) {
          _candidateRects[i].SetRectEmpty();
          _candidateLabelRects[i].SetRectEmpty();
          _candidateTextRects[i].SetRectEmpty();
          _candidateCommentRects[i].SetRectEmpty();
          continue;
        }
        if (i < compact_head_count) {
          _candidateRects[i] = compact_layout->GetCandidateRect(i);
          _candidateLabelRects[i] = compact_layout->GetCandidateLabelRect(i);
          _candidateTextRects[i] = compact_layout->GetCandidateTextRect(i);
          _candidateCommentRects[i] = compact_layout->GetCandidateCommentRect(i);
          _displayLabels[i] = active_grid_row == 0
                                 ? compact_layout->GetDisplayLabel(i) : L"";
          mintop_of_rows[0] = _candidateRects[i].top;
          height_of_rows[0] = max(height_of_rows[0], _candidateRects[i].Height());
          row_of_candidate[i] = 0;
          max_width_of_rows = max(max_width_of_rows,
              max(offsetX + real_margin_x + grid_width,
                  _candidateRects[i].right));
          continue;
        }
        const int new_row = grid_abs_row[i] - first_grid_row;
        const int column = grid_slot[i] % grid_columns;
        if (new_row > row_cnt) {
          height += height_of_rows[row_cnt] + _style.candidate_spacing;
          row_cnt = new_row;
        }
        w = offsetX + real_margin_x + column * grid_cell_width;
        mintop_of_rows[row_cnt] = height;
        row_of_candidate[i] = row_cnt;
      }
      int current_cand_width = 0;
      if (!expanded_grid && i > 0)
        w += _style.candidate_spacing;
      if (id == i && !expanded_grid)
        w += base_offset;
      /* Label */
      std::wstring label;
      if (expanded_grid) {
        if (grid_abs_row[i] == active_grid_row)
          label = std::to_wstring(grid_row_number[i]);
      } else {
        label = GetLabelText(labels, i, _style.label_text_format.c_str());
      }
      _displayLabels[i] = label;
      GetTextSizeDW(label, label.length(), pDWR->pLabelTextFormat, pDWR, &size);
      const int label_slot_width =
          expanded_grid ? grid_label_slot_width : size.cx * labelFontValid;
      _candidateLabelRects[i].SetRect(w, height, w + label_slot_width,
                                      height + size.cy);
      w += label_slot_width;
      current_cand_width += label_slot_width;

      /* Text */
      w += _style.hilite_spacing;
      const std::wstring text = CandidateWithCloudBadge(candidates.at(i).str, comments.at(i).str);
      GetTextSizeDW(text, text.length(), pDWR->pTextFormat, pDWR, &size);
      _candidateTextRects[i].SetRect(w, height, w + size.cx * textFontValid,
                                     height + size.cy);
      if (expanded_grid) {
        const int column = grid_slot[i] % grid_columns;
        const int cell_right = offsetX + real_margin_x +
                               (column + grid_span[i]) * grid_cell_width -
                               _style.candidate_spacing;
        // Use the whole allocated span.  Keeping the rect at the exact
        // measured glyph width makes the drawing pass measure the same text
        // a fraction wider because of DirectWrite pixel rounding, which then
        // incorrectly adds an ellipsis despite sufficient grid space.
        _candidateTextRects[i].right = cell_right;
      }
      w += size.cx * textFontValid;
      current_cand_width += (size.cx + _style.hilite_spacing) * textFontValid;

      /* Comment — placed below text, supports \n multiline */
      bool cmtFontNotTrans =
          (i == id && (_style.hilited_comment_text_color & 0xff000000)) ||
          (i != id && (_style.comment_text_color & 0xff000000));
      const std::wstring comment = StripTranslationPrefixes(comments.at(i).str);
      if (!comment.empty() && cmtFontValid && cmtFontNotTrans) {
        GetTextSizeDW(comment, comment.length(), pDWR->pCommentTextFormat, pDWR,
                      &size);
        // comment rect starts at left edge of label, below the text row
        // Compact candidates have always aligned annotations with the numeric
        // label.  Expanded candidates may either keep the legacy text-aligned
        // appearance or reclaim the label column for a wider annotation.
        int cmt_left = expanded_grid && !_style.expanded_comment_align_label
                           ? _candidateTextRects[i].left
                           : _candidateLabelRects[i].left;
        if (i == id && !expanded_grid) cmt_left -= base_offset;
        int cmt_top = height + max(_candidateLabelRects[i].Height(),
                                   _candidateTextRects[i].Height())
                              + _style.hilite_spacing / 2;
        _candidateCommentRects[i].SetRect(cmt_left, cmt_top,
                                          cmt_left + size.cx * cmtFontValid,
                                          cmt_top + size.cy);
        if (expanded_grid) {
          const int column = grid_slot[i] % grid_columns;
          const int cell_right = offsetX + real_margin_x +
                                 (column + grid_span[i]) * grid_cell_width -
                                 _style.candidate_spacing;
          _candidateCommentRects[i].right = cell_right;
        }
        // The normal single-row layout is content-sized.  Reserve the wider
        // of the visible annotation and the hidden Japanese reading so Shift
        // can change what is painted without moving the following candidate.
        if (!expanded_grid) {
          int reserved_right = _candidateCommentRects[i].right;
          const std::wstring hidden_reading =
              HiddenGridReading(comments.at(i).str);
          if (!hidden_reading.empty()) {
            const std::wstring reading_line = L"[" + hidden_reading + L"]";
            CSize reading_size;
            GetTextSizeDW(reading_line, reading_line.length(),
                          pDWR->pCommentTextFormat, pDWR, &reading_size);
            reserved_right = max(
                reserved_right,
                cmt_left + reading_size.cx * cmtFontValid);
          }
          // A long translation/reading must not consume several candidate
          // slots in the compact row.  Keep the full annotation available in
          // the expanded grid, while the compact painter ellipsizes it inside
          // a bounded one-slot cell.  Candidate text itself is never clipped
          // by this cap.
          if (_style.max_height > 0) {
            const int compact_left =
                (i == id ? _candidateLabelRects[i].left - base_offset
                         : _candidateLabelRects[i].left);
            const std::wstring& raw_comment = comments.at(i).str;
            const bool sentence_translation = i == 0 &&
                raw_comment.find(L"[[RIME_NIU_TRANSLATION]]") !=
                    std::wstring::npos;
            const int compact_right =
                compact_left + max(1, sentence_translation
                    ? _style.max_height * 2 / 5 : _style.max_height / 6);
            reserved_right =
                max(_candidateTextRects[i].right,
                    min(reserved_right, compact_right));
          }
          _candidateCommentRects[i].right = reserved_right;
          w = max(w, reserved_right);
        }
        current_cand_width = max(current_cand_width, size.cx * cmtFontValid);
      } else /* Used for highlighted candidate calculation below */
        _candidateCommentRects[i].SetRect(w, height, w, height + size.cy);

      int base_left = (i == id && !expanded_grid)
                          ? _candidateLabelRects[i].left - base_offset
                          : _candidateLabelRects[i].left;
      // Truncate candidates that exceed max_height (used as max width in horizontal layout)
      int cand_right = max(_candidateTextRects[i].right,
                           _candidateCommentRects[i].right);
      if (!expanded_grid && _style.max_height > 0 && i > 0 &&
          (cand_right - offsetX + real_margin_x > _style.max_height)) {
        visible_count = i;
        visible_candidates = i;  // tell WeaselPanel to stop drawing here
        break;
      }
      max_width_of_rows = expanded_grid
                              ? offsetX + real_margin_x + grid_width
                              : max(max_width_of_rows, w);
      // calculate height of current row: text row + comment below
      mintop_of_rows[row_cnt] = height;
      int text_row_h = max(_candidateLabelRects[i].Height(),
                           _candidateTextRects[i].Height());
      int cmt_h = _candidateCommentRects[i].Height() > 0
                      ? (_style.hilite_spacing / 2 + _candidateCommentRects[i].Height())
                      : 0;
      height_of_rows[row_cnt] =
          max(height_of_rows[row_cnt], text_row_h + cmt_h);
      // set row info of current candidate
      row_of_candidate[i] = row_cnt;
    }

    // reposition for alignment
    visible_candidates = visible_count;
    for (auto i = grid_start; i < visible_count && i < MAX_CANDIDATES_COUNT; ++i) {
      if (i < compact_head_count) continue; // already aligned by compact layout
      int base_left = (i == id && !expanded_grid)
                          ? _candidateLabelRects[i].left - base_offset
                          : _candidateLabelRects[i].left;
      _candidateRects[i].SetRect(base_left, mintop_of_rows[row_of_candidate[i]],
                                 _candidateTextRects[i].right,
                                 mintop_of_rows[row_of_candidate[i]] +
                                     height_of_rows[row_of_candidate[i]]);
      // comment rect right extends candidate rect
      if (_candidateCommentRects[i].right > _candidateRects[i].right)
        _candidateRects[i].right = _candidateCommentRects[i].right;
      // Extend candidate rect bottom to include comment
      if (_candidateCommentRects[i].Height() > 0 &&
          _candidateCommentRects[i].bottom > _candidateRects[i].bottom)
        _candidateRects[i].bottom = _candidateCommentRects[i].bottom;
      if (expanded_grid) {
        const int column = grid_slot[i] % grid_columns;
        _candidateRects[i].right = offsetX + real_margin_x +
                                   (column + grid_span[i]) * grid_cell_width -
                                   _style.candidate_spacing;
      } else {
        _candidateRects[i].right =
            max(_candidateTextRects[i].right,
                _candidateCommentRects[i].right);
      }
      int ol = 0, ot = 0;
      if (_style.align_type == UIStyle::ALIGN_CENTER) {
        int text_row_h_i = max(_candidateLabelRects[i].Height(),
                               _candidateTextRects[i].Height());
        ol = (text_row_h_i - _candidateLabelRects[i].Height()) / 2;
        ot = (text_row_h_i - _candidateTextRects[i].Height()) / 2;
      } else if (_style.align_type == UIStyle::ALIGN_BOTTOM) {
        int text_row_h_i = max(_candidateLabelRects[i].Height(),
                               _candidateTextRects[i].Height());
        ol = text_row_h_i - _candidateLabelRects[i].Height();
        ot = text_row_h_i - _candidateTextRects[i].Height();
      }
      _candidateLabelRects[i].OffsetRect(0, ol);
      _candidateTextRects[i].OffsetRect(0, ot);
    }
    {
      std::string rect_trace =
          std::string("RECTS mode=") + (expanded_grid ? "GRID" : "NORMAL") +
          " id=" + std::to_string(id) +
          " maxh=" + std::to_string(_style.max_height) + " ";
      const int trace_end = min(visible_count, grid_start + 12);
      for (int i = grid_start; i < trace_end; ++i) {
        if (i > grid_start) rect_trace += ",";
        rect_trace += std::to_string(i) + "[l=" +
                      std::to_string(_candidateLabelRects[i].left) +
                      ",t=" + std::to_string(_candidateTextRects[i].left) +
                      ",c=" +
                      std::to_string(_candidateCommentRects[i].left) + "]";
      }
      GridLayoutTrace(rect_trace);
    }
    height = mintop_of_rows[row_cnt] + height_of_rows[row_cnt] - offsetY;
    width = max(width, max_width_of_rows);
  } else {
    height -= _style.spacing + offsetY;
    width += _style.hilite_spacing + _style.border;
  }

  width += real_margin_x;
  height += real_margin_y;

  if (candidates_count) {
    width = max(width, _style.min_width);
    height = max(height, _style.min_height);
  }
  if (candidates_count) {
    for (auto i = grid_start; i < visible_count && i < MAX_CANDIDATES_COUNT; ++i) {
      // make rightest candidate's rect right the same for better look
      if ((i < candidates_count - 1 &&
           row_of_candidate[i] < row_of_candidate[i + 1]) ||
          (i == visible_count - 1))
        if (i >= compact_head_count)
          _candidateRects[i].right = width - real_margin_x;
    }
  }
  _highlightRect = _candidateRects[id];
  UpdateStatusIconLayout(&width, &height);
  _contentSize.SetSize(width + offsetX, height + 2 * offsetY);
  _contentRect.SetRect(0, 0, _contentSize.cx, _contentSize.cy);

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

  // prepare temp rect _bgRect for roundinfo calculation
  CopyRect(_bgRect, _contentRect);
  _bgRect.DeflateRect(offsetX + 1, offsetY + 1);
  // prepare round info for single row status, only for single row situation
  _PrepareRoundInfo(dc);
  // readjust for multi rows
  if (row_cnt)  // row_cnt > 0, at least 2 candidates
  {
    _roundInfo[0].IsBottomLeftNeedToRound = false;
    _roundInfo[candidates_count - 1].IsTopRightNeedToRound = false;
    for (auto i = 1; i < candidates_count; i++) {
      _roundInfo[i].Hemispherical = _roundInfo[0].Hemispherical;
      if (row_of_candidate[i] == row_cnt &&
          row_of_candidate[i - 1] == row_cnt - 1)
        _roundInfo[i].IsBottomLeftNeedToRound = true;
      if (row_of_candidate[i] == 0 && row_of_candidate[i + 1] == 1)
        _roundInfo[i].IsTopRightNeedToRound = _style.inline_preedit;
    }
  }
  // truely draw content size calculation
  _contentRect.DeflateRect(offsetX, offsetY);
}
