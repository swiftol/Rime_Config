#pragma once
#include <string>
#include <vector>
#include <cstddef>

struct JapaneseHoldToken { std::string prefix; size_t start; char key; };

// Raw spellings stay private to matching. Backspace at a hold's right edge
// deletes that entire logical key, also rebasing any later hold tokens.
inline bool JapaneseEraseHoldToken(std::string& input,size_t& caret,
                                  std::vector<JapaneseHoldToken>& tokens) {
  if(caret>input.size())return false;
  size_t start=caret;
  for(auto it=tokens.rbegin();it!=tokens.rend();++it) {
    if(it->prefix.size()==caret && it->start<caret &&
        input.compare(0,caret,it->prefix)==0) {start=it->start;break;}
  }
  if(start==caret)return false;
  const size_t end=caret,count=end-start;
  input.erase(start,count);caret=start;
  std::vector<JapaneseHoldToken> remaining;
  for(auto token:tokens) {
    if(token.prefix.size()<=start) {
      if(input.compare(0,token.prefix.size(),token.prefix)==0)remaining.push_back(std::move(token));
    }else if(token.start>=end && token.prefix.size()>=token.start) {
      token.start-=count;token.prefix.erase(start,count);
      if(input.compare(0,token.prefix.size(),token.prefix)==0)remaining.push_back(std::move(token));
    }
  }
  tokens=std::move(remaining);
  return true;
}
