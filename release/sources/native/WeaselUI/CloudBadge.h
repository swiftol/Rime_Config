#pragma once
#include <string>

// Presentation only: candidate identity, commits and learning stay untouched.
inline std::wstring CandidateWithCloudBadge(const std::wstring& text,
                                          const std::wstring& comment) {
  return comment.find(L'\u2601') == std::wstring::npos ||
                 text.find(L'\u2601') != std::wstring::npos
             ? text : text + L" \u2601";
}

inline std::wstring WithoutCloudBadgeLine(std::wstring comment) {
  std::wstring output, metadata;
  for (size_t begin = 0; begin < comment.size();) {
    const size_t end = comment.find(L'\n', begin);
    const bool newline = end != std::wstring::npos;
    std::wstring line = comment.substr(begin, newline ? end - begin : end);
    const bool badge = line.find(L'\u2601') != std::wstring::npos;
    size_t pos;
    while ((pos = line.find(L'\u2601')) != std::wstring::npos) {
      line.erase(pos, 1);
      if (pos < line.size() && (line[pos] == L'\ufe0f' || line[pos] == L'\ufe0e'))
        line.erase(pos, 1);
    }
    std::wstring visible = line, hidden;
    while ((pos = visible.find(L"[[RIME_")) != std::wstring::npos) {
      const size_t close = visible.find(L"]]", pos);
      if (close == std::wstring::npos) break;
      hidden += visible.substr(pos, close + 2 - pos);
      visible.erase(pos, close + 2 - pos);
    }
    if (badge && visible.find_first_not_of(L" \t\r") == std::wstring::npos) {
      metadata += hidden;
      if (!newline && !output.empty() && output.back() == L'\n') {
        output.pop_back();
        if (!output.empty() && output.back() == L'\r') output.pop_back();
      }
    } else {
      output += line;
      if (newline) output += L'\n';
    }
    if (!newline) break;
    begin = end + 1;
  }
  return output + metadata;
}

// Normalize before IPC as well as in new renderers: an already loaded old
// client must never receive a standalone badge annotation line. Input strings
// here are decoded display copies, not Rime candidate/commit identity.
inline void PresentInlineCloudBadge(std::wstring& text, std::wstring& comment,
                                   bool source_cloud = false) {
  text = CandidateWithCloudBadge(text, source_cloud ? L"\u2601" : comment);
  comment = WithoutCloudBadgeLine(comment);
}
