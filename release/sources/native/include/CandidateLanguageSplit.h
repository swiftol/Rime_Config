#pragma once
#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>
#include <WeaselIPCData.h>
namespace weasel {
struct CandidateSplitOptions { bool enabled=false; int left=3,right=3; };
inline CandidateSplitOptions ReadCandidateSplitOptions() {
  std::wstring key=L"Software\\ZhongriInputMethod";
  wchar_t test[512]={},managed[MAX_PATH]={};
  if(GetEnvironmentVariableW(L"ZHONGRI_TEST_RUN_ROOT",managed,MAX_PATH) &&
     GetEnvironmentVariableW(L"ZHONGRI_TEST_HOT_SETTINGS_KEY",test,512) &&
     std::wstring(test).rfind(L"Software\\ZhongriInputMethod\\Tests\\",0)==0)key=test;
  auto read=[&](const wchar_t* name,DWORD fallback){DWORD value=fallback,size=sizeof(value);RegGetValueW(HKEY_CURRENT_USER,key.c_str(),name,RRF_RT_REG_DWORD,nullptr,&value,&size);return int(value);};
  CandidateSplitOptions options;options.enabled=read(L"CandidateLanguageSplit",0)!=0;
  options.left=read(L"CandidateSplitChineseColumns",3);options.right=read(L"CandidateSplitJapaneseColumns",3);
  if(options.left<1||options.right<1||options.left+options.right>9)options.left=options.right=3;
  return options;
}
inline CandidateSplitOptions EffectiveCandidateSplitOptions(const UIStyle& style) {
  if(style.candidate_language_split<0)return ReadCandidateSplitOptions();
  CandidateSplitOptions options{style.candidate_language_split!=0,style.candidate_split_chinese_columns,style.candidate_split_japanese_columns};
  if(options.left<1||options.right<1||options.left+options.right>9)options.left=options.right=3;
  return options;
}
inline bool SplitCandidateJapanese(const std::wstring& text,const std::wstring& comment) {
  if(comment.find(L"[[RIME_LANG:ZH]]")!=std::wstring::npos)return false;
  if(comment.find(L"[[RIME_LANG:JA]]")!=std::wstring::npos)return true;
  if(comment.find(L"[[RIME_JR:")!=std::wstring::npos && comment.find(L"[[RIME_LANG:CP]]")==std::wstring::npos)return true;
  for(wchar_t ch:text)if(ch>=0x3041&&ch<=0x30ff)return true;
  return false;
}
struct CandidateSplitCell { int index,row,column,capacity; bool japanese; };
struct CandidateSplitRow { int left,right; bool borrowed; };
struct CandidateSplitPlan { std::vector<std::vector<int>> rows; std::vector<CandidateSplitCell> cells; std::vector<CandidateSplitRow> regions; };
inline CandidateSplitPlan BuildCandidateSplitPlan(const std::vector<bool>& japanese,CandidateSplitOptions options,bool first_header=true,int headerLeft=-1,int headerRight=-1) {
  if(options.left<1||options.right<1||options.left+options.right>9)options.left=options.right=3;
  std::vector<int> sides[2];for(size_t i=0;i<japanese.size();++i)sides[japanese[i]?1:0].push_back(int(i));
  size_t at[2]={};CandidateSplitPlan plan;
  while(at[0]<sides[0].size()||at[1]<sides[1].size()) {
    const int row=int(plan.rows.size());plan.rows.emplace_back();
    int counts[2]={options.left,options.right};
    if(first_header && row==0){counts[0]=(std::min)(8,(9*options.left+options.left+options.right-1)/(options.left+options.right));counts[1]=9-counts[0];}
    if(first_header&&row==0&&headerLeft>=0&&headerRight>=0&&headerLeft+headerRight>0){counts[0]=headerLeft;counts[1]=headerRight;}
    int native[2]={int((std::min)(size_t(counts[0]),sides[0].size()-at[0])),
                   int((std::min)(size_t(counts[1]),sides[1].size()-at[1]))};
    int borrowed[2]={int((std::min)(size_t(counts[0]-native[0]),sides[1].size()-at[1]-native[1])),
                     int((std::min)(size_t(counts[1]-native[1]),sides[0].size()-at[0]-native[0]))};
    const int actual[2]={native[0]+borrowed[1],native[1]+borrowed[0]};
    int capacity[2]={counts[0]+borrowed[1]-borrowed[0],counts[1]+borrowed[0]-borrowed[1]};
    if(!actual[1]){capacity[0]=counts[0]+counts[1];capacity[1]=0;}
    if(!actual[0]){capacity[1]=counts[0]+counts[1];capacity[0]=0;}
    plan.regions.push_back({capacity[0],capacity[1],borrowed[0]+borrowed[1]>0});
    // Borrow capacity, not positions: all Chinese still precede all Japanese.
    for(int side=0;side<2;++side)for(int column=0;column<actual[side];++column) {
      int index=sides[side][at[side]++];plan.rows.back().push_back(index);
      plan.cells.push_back({index,row,column,capacity[side],side==1});
    }
  }
  return plan;
}
inline bool CandidateRowPermutation(const std::vector<std::vector<int>>& rows,int count) {
  if(count<0)return false;std::vector<bool> seen(count,false);int total=0;
  for(const auto& row:rows){if(row.empty())return false;for(int index:row){if(index<0||index>=count||seen[index])return false;seen[index]=true;++total;}}
  return total==count;
}
}
