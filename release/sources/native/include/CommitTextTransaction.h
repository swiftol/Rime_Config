#pragma once
#include <string>

inline std::wstring SingleParagraphBreaks(const std::wstring& text) {
  std::wstring result;
  for(size_t i=0;i<text.size();++i) {
    if(text[i]==L'\r' || text[i]==L'\n') {
      if(text[i]==L'\r' && i+1<text.size() && text[i+1]==L'\n')++i;
      result+=L'\r';
    } else result+=text[i];
  }
  return result;
}
// The host may normalize text or reset selection while ending composition.
// Use its returned inserted range, and place the caret only AFTER that end.
template<class Replace,class End,class Caret>
bool CommitTextTransaction(Replace replace,End end,Caret caret) {
  return replace() && end() && caret();
}
