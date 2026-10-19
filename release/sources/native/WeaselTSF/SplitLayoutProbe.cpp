// Read-only finished-DLL check: no host composition, input or registry writes.
#include "../WeaselUI/stdafx.h"
#include "../WeaselUI/HorizontalLayout.h"
#include <CandidateLanguageSplit.h>
#include <SplitTextLayout.h>
#include <sstream>
#include <boost/archive/text_wiarchive.hpp>
extern "C" __declspec(dllexport) int __cdecl ZhongriProbeSplitLayout(wchar_t* output,unsigned capacity) {
  using namespace weasel;
  if(!output||capacity<2)return 2;
  ULONG_PTR token=0;Gdiplus::GdiplusStartupInput startup;
  Gdiplus::GdiplusStartup(&token,&startup,nullptr);
  HDC dc=CreateCompatibleDC(nullptr);
  int result=0;std::wostringstream report;
  try {
    const auto options=ReadCandidateSplitOptions();
    report<<L"enabled="<<options.enabled<<L" left="<<options.left<<L" right="<<options.right<<L"\n";
    for(bool expanded:{false,true}) {
      UIStyle style;style.layout_type=UIStyle::LAYOUT_HORIZONTAL;style.max_height=expanded?-900:900;style.min_width=900;
      style.font_face=style.label_font_face=style.comment_font_face=L"Microsoft YaHei";
      style.font_point=20;style.label_font_point=14;style.comment_font_point=12;
      style.margin_x=style.margin_y=8;style.candidate_spacing=8;style.hilite_spacing=4;
      wchar_t runtimeStyle[16384]={};
      if(GetEnvironmentVariableW(L"ZHONGRI_TEST_UI_STYLE",runtimeStyle,16384)) {
        std::wistringstream stream(runtimeStyle);boost::archive::text_wiarchive archive(stream);
        try{archive>>style;}catch(const boost::archive::archive_exception&){style.candidate_language_split=-1;}
        style.max_height=expanded?-abs(style.max_height):abs(style.max_height);
      }
      const auto expected=EffectiveCandidateSplitOptions(style);
      // Distinct fixture colours permit assertions on the actual painted pixels.
      style.japanese_candidate_text_color=static_cast<int>(0xff00ffff);
      style.chinese_candidate_text_color=static_cast<int>(0xffeeeeee);
      report<<L"transport="<<style.candidate_language_split<<L" expected="<<expected.enabled<<L"\n";
      auto dwr=std::make_shared<DirectWriteResources>(style,96);
      Context ctx;Status status;status.composing=true;ctx.preedit.str=L"owned-split-fixture";
      for(int i=0;i<100;++i){Text word,comment,label;word.str=i%2?L"か":L"卡";comment.str=i%2?L"[[RIME_LANG:JA]]":L"[[RIME_LANG:ZH]]";ctx.cinfo.candies.push_back(word);ctx.cinfo.comments.push_back(comment);ctx.cinfo.labels.push_back(label);}
      wchar_t sparse[16]={};
      if(GetEnvironmentVariableW(L"ZHONGRI_TEST_SPLIT_SPARSE",sparse,16)) {
        const bool fewJapanese=wcscmp(sparse,L"Japanese")==0;
        for(int i=0;i<100;++i){const bool jp=i<16?(i%2!=0):!fewJapanese;ctx.cinfo.candies[i].str=jp?L"か":L"卡";ctx.cinfo.comments[i].str=jp?L"[[RIME_LANG:JA]]":L"[[RIME_LANG:ZH]]";}
      }
      wchar_t longText[8]={};
      const bool longFixture=GetEnvironmentVariableW(L"ZHONGRI_TEST_SPLIT_LONG_TEXT",longText,8)>0;
      if(longFixture) {
        ctx.cinfo.candies[0].str=L"是不是今天下午一起去施工现场检查";
        ctx.cinfo.candies[1].str=L"そうではなく明日現場を確認します";
        ctx.cinfo.comments[0].str+=L"Is it today or tomorrow?\nそうではありません";
        ctx.cinfo.comments[1].str+=L"We will check the construction site tomorrow.\n明天检查施工现场";
      }
      HorizontalLayout layout(style,ctx,status,dwr);layout.DoLayout(dc,dwr);
      if(longFixture) {
        for(int i:{0,1}) {
          const auto rc=layout.GetCandidateTextRect(i);
          const auto comment=layout.GetCandidateCommentRect(i);
          const auto cell=layout.GetCandidateRect(i);
          dwr->CreateTextLayout(ctx.cinfo.candies[i].str.c_str(),int(ctx.cinfo.candies[i].str.size()),dwr->pTextFormat.Get(),float(rc.Width()),100000.0f);
          ConfigureSplitTextLayout(dwr->pTextLayout.Get());
          DWRITE_TEXT_METRICS metrics={};dwr->GetLayoutMetrics(&metrics);
          report<<L"long index="<<i<<L" lines="<<metrics.lineCount<<L" measured="<<metrics.height<<L" allocated="<<rc.Height()<<L" commentTop="<<comment.top<<L" textBottom="<<rc.bottom<<L"\n";
          if(metrics.height>rc.Height()||comment.top<rc.bottom||comment.bottom>cell.bottom)result=1;
        }
      }
      const bool split=!layout.language_split_divider.IsRectEmpty();
      if(split) {
        int used=0,last[2]={-1,-1};
        int mixedRows=0;
        for(size_t r=0;r<layout.expanded_candidate_rows.size();++r){const auto& row=layout.expanded_candidate_rows[r];const int capacity=r==0?int(row.size()):expected.left+expected.right;if(row.size()!=size_t((std::min)(100-used,capacity)))result=1;bool seenJapanese=false,seenChinese=false;for(int i:row){const int side=SplitCandidateJapanese(ctx.cinfo.candies[i].str,ctx.cinfo.comments[i].str)?1:0;if(!side&&seenJapanese)result=1;if(side)seenJapanese=true;else seenChinese=true;if(i<=last[side])result=1;last[side]=i;++used;}if(r<size_t(expanded?5:1)&&seenChinese&&seenJapanese)++mixedRows;}
        if(int(layout.language_split_dividers.size())!=mixedRows)result=1;
        int headerJapanese=0;for(int i:layout.expanded_candidate_rows.front())if(SplitCandidateJapanese(ctx.cinfo.candies[i].str,ctx.cinfo.comments[i].str))++headerJapanese;
        report<<L"header total="<<layout.expanded_candidate_rows.front().size()<<L" japanese="<<headerJapanese<<L" dividerSegments="<<layout.language_split_dividers.size()<<L"\n";
        if(longFixture&&layout.expanded_candidate_rows.front().size()>=9)result=1;
        if(used!=100)result=1;
        report<<L"all rows filled when candidates remain; language order retained\n";
      }
      report<<(expanded?L"expanded":L"compact")<<L" divider="<<split<<L" rows="<<layout.expanded_candidate_rows.size()<<L"\n";
      if(split!=expected.enabled)result=1;
      UI rendered;rendered.style()=style;rendered.Create(nullptr);rendered.Update(ctx,status);rendered.Refresh();
      const RECT actual=rendered.GetLanguageSplitDivider();
      if(!rendered.HasSuccessfulPaint())result=1;
      report<<L"factory layout="<<int(rendered.style().layout_type)<<L" maxh="<<rendered.style().max_height<<L" divider="<<actual.left<<L","<<actual.right<<L" rows="<<rendered.GetExpandedCandidateRows().size()<<L"\n";
      if((actual.right>actual.left)!=expected.enabled||rendered.GetExpandedCandidateRows()!=layout.expanded_candidate_rows)result=1;
      if(style.candidate_language_split>=0) {
        SetEnvironmentVariableW(L"ZHONGRI_TEST_CAPTURE_PHASE",L"off");
        rendered.style().candidate_language_split=0;rendered.Refresh();
        const auto off=rendered.GetLanguageSplitDivider();if(off.right>off.left)result=1;
        if(!rendered.HasSuccessfulPaint())result=1;
        SetEnvironmentVariableW(L"ZHONGRI_TEST_CAPTURE_PHASE",L"on");
        rendered.style()=style;rendered.Refresh();
        const auto restored=rendered.GetLanguageSplitDivider();if((restored.right>restored.left)!=expected.enabled)result=1;
        if(!rendered.HasSuccessfulPaint())result=1;
        report<<L"same-window OFF/ON verified\n";
      }
      rendered.Destroy(true);
      if(split) {
        int visible=0;
        for(int i=0;i<100;++i){const auto rc=layout.GetCandidateRect(i);if(rc.IsRectEmpty())continue;++visible;const auto text=layout.GetCandidateTextRect(i);if(text.left<rc.left||text.right>rc.right)result=1;}
        if(!expanded&&(visible!=int(layout.expanded_candidate_rows.front().size())||visible>9))result=1;
        const auto& first=layout.expanded_candidate_rows.front();
        for(size_t d=0;d<first.size();++d)if(layout.GetDisplayLabel(first[d])!=std::to_wstring(d+1))result=1;
      }
    }
  }catch(...){result=1;report<<L"probe exception\n";}
  DeleteDC(dc);Gdiplus::GdiplusShutdown(token);
  wcsncpy_s(output,capacity,report.str().c_str(),_TRUNCATE);
  return result;
}
