#pragma once
#include <algorithm>
#include <string>
#include <vector>

// Lua uses scalar offsets; DirectWrite uses UTF-16. Presentation only.
inline std::vector<bool> ReservedPhraseMask(const std::wstring& text,
                                           const std::wstring& comment) {
  std::vector<bool> mask(text.size(),false);
  const std::wstring marker=L"[[RIME_RS:";
  const auto start=comment.find(marker);
  if(start==std::wstring::npos)return mask;
  const auto end=comment.find(L"]]",start+marker.size());
  if(end==std::wstring::npos || end-start>8192)return mask;
  std::vector<size_t> offsets{0};
  for(size_t i=0;i<text.size();) {
    if(text[i]>=0xd800 && text[i]<=0xdbff && i+1<text.size() && text[i+1]>=0xdc00 && text[i+1]<=0xdfff)i+=2;
    else ++i;
    offsets.push_back(i);
  }
  size_t pos=start+marker.size(),ranges=0;
  auto number=[&](size_t& value) {
    value=0;const auto begin=pos;
    while(pos<end && comment[pos]>=L'0' && comment[pos]<=L'9') {
      if(value>100000)return false;
      value=value*10+(comment[pos++]-L'0');
    }
    return pos>begin;
  };
  while(pos<end && ++ranges<=128) {
    size_t begin=0,length=0;
    if(!number(begin) || pos>=end || comment[pos++]!=L':' || !number(length))return std::vector<bool>(text.size(),false);
    if(begin<offsets.size()-1) {
      const auto finish=(std::min)(begin+length,offsets.size()-1);
      for(size_t i=offsets[begin];i<offsets[finish];++i)mask[i]=true;
    }
    if(pos<end && comment[pos++]!=L';')return std::vector<bool>(text.size(),false);
  }
  if(pos<end)return std::vector<bool>(text.size(),false);
  if(!text.empty() && text.back()==L'\x2026')mask.back()=false;
  return mask;
}
