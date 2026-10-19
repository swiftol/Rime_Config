#pragma once
#include <string>

inline bool IsSingleAnnotationCharacter(const std::wstring& text) {
  if (text.size() == 1) return true;
  // Windows stores supplementary Han characters as a UTF-16 surrogate pair.
  return text.size() == 2 && text[0] >= 0xd800 && text[0] <= 0xdbff &&
         text[1] >= 0xdc00 && text[1] <= 0xdfff;
}

inline std::wstring AnnotationMetadataValue(const std::wstring& comment,
                                             const std::wstring& marker) {
  const auto start = comment.find(marker);
  if (start == std::wstring::npos) return L"";
  const auto begin = start + marker.size();
  const auto end = comment.find(L"]]", begin);
  return end == std::wstring::npos ? L"" : comment.substr(begin, end - begin);
}

inline std::wstring JapaneseCorrectionLine(const std::wstring& comment) {
  const auto reading = AnnotationMetadataValue(comment, L"[[RIME_FUZZY_READING:");
  return reading.empty() ? L"" : L"[" + reading + L"]";
}

inline bool IsJapaneseAnnotationSource(const std::wstring& source,
                                        const std::wstring& comment) {
  if (comment.find(L"[[RIME_LANG:JA]]") != std::wstring::npos) return true;
  for (const auto ch : source)
    if (ch >= 0x3040 && ch <= 0x30ff) return true;
  return false;
}

inline std::wstring IndependentPronunciationMetadata(const std::wstring& comment) {
  std::wstring tags;
  for (size_t pos = 0; (pos = comment.find(L"[[", pos)) != std::wstring::npos;) {
    const auto end = comment.find(L"]]", pos);
    if (end == std::wstring::npos) break;
    if (comment.compare(pos, 12, L"[[RIME_LANG:") == 0 ||
        comment.compare(pos, 10, L"[[RIME_JR:") == 0 ||
        comment.compare(pos, 13, L"[[RIME_FUZZY_") == 0 ||
        comment.compare(pos, 10, L"[[RIME_RS:") == 0)
      tags += comment.substr(pos, end + 2 - pos);
    pos = end + 2;
  }
  return tags;
}

inline std::wstring JapanesePronunciationOnlyComment(const std::wstring& comment) {
  return JapaneseCorrectionLine(comment) + IndependentPronunciationMetadata(comment);
}

// A prefix continuation hint is keyboard guidance, not a translation of a
// complete word. Project from the genuine source *after* provider merging so
// neither a private classifier nor a translation can replace the useful ~A.
inline bool PresentJapanesePrefixHint(std::wstring& displayed,
                                      const std::wstring& source) {
  const std::wstring marker=L"[JP:PREFIX]";
  const auto start=source.find(marker);
  if(start==std::wstring::npos)return false;
  const auto begin=start+marker.size();
  const auto end=source.find(L'[',begin);
  auto hint=source.substr(begin,end==std::wstring::npos?std::wstring::npos:end-begin);
  while(!hint.empty() && (hint.back()==L' '||hint.back()==L'\r'||hint.back()==L'\n'))hint.pop_back();
  displayed=hint+IndependentPronunciationMetadata(source);
  return true;
}

inline void PreserveReservedSpanMetadata(std::wstring& displayed,
                                         const std::wstring& source) {
  const std::wstring marker=L"[[RIME_RS:";
  const auto spans=AnnotationMetadataValue(source,marker);
  for(size_t pos;(pos=displayed.find(marker))!=std::wstring::npos;) {
    const auto end=displayed.find(L"]]",pos);
    if(end==std::wstring::npos){displayed.erase(pos);break;}
    displayed.erase(pos,end+2-pos);
  }
  if(!spans.empty())displayed+=marker+spans+L"]]";
}

inline bool HasLegacyTranslationAnnotation(const std::wstring& comment) {
  if (comment.find(L"[[RIME_NIU_TRANSLATION]]") != std::wstring::npos) return false;
  auto visible = comment;
  // A correction is pronunciation evidence, not a legacy translation. It
  // must coexist with either preferred translation provider.
  const auto correction = JapaneseCorrectionLine(comment);
  const auto correction_pos = correction.empty() ? std::wstring::npos : visible.find(correction);
  if (correction_pos != std::wstring::npos) visible.erase(correction_pos, correction.size());
  for (size_t start; (start = visible.find(L"[[")) != std::wstring::npos;) {
    const auto end = visible.find(L"]]", start);
    if (end == std::wstring::npos) break;
    visible.erase(start, end + 2 - start);
  }
  // IPC uses escaped newlines; a cloud badge alone is not an annotation.
  for (size_t pos; (pos = visible.find(L"\\n")) != std::wstring::npos;) visible.erase(pos, 2);
  return visible.find_first_not_of(L" \t\r\n☁\ufe0f") != std::wstring::npos;
}

inline bool SetNiuTranslationAnnotation(std::wstring& comment,
                                        const std::wstring& translation,
                                        bool prefer_niu, bool escaped,
                                        const std::wstring& translated_reading = L"") {
  if (translation.empty() || (!prefer_niu && HasLegacyTranslationAnnotation(comment))) return false;
  const auto correction = JapaneseCorrectionLine(comment);
  auto tags = IndependentPronunciationMetadata(comment);
  if (!IsJapaneseAnnotationSource(L"", comment)) {
    // Chinese candidates carry a reading of their Japanese *translation*.
    // Replacing that surface invalidates the old reading. Japanese source
    // candidates instead retain their own independent pronunciation.
    for (size_t pos; (pos = tags.find(L"[[RIME_JR:")) != std::wstring::npos;) {
      const auto end = tags.find(L"]]", pos);
      if (end == std::wstring::npos) break;
      tags.erase(pos, end + 2 - pos);
    }
    if (!translated_reading.empty()) tags += L"[[RIME_JR:" + translated_reading + L"]]";
  }
  const auto badge = comment.find(L"☁") == std::wstring::npos ? std::wstring() :
      L"☁" + std::wstring(escaped ? L"\\n" : L"\n");
  // Replacement, never append: dictionary and cached-online data stay separate.
  const std::wstring newline = escaped ? L"\\n" : L"\n";
  comment = badge + (correction.empty() ? L"" : correction + newline) +
            translation + L"[[RIME_NIU_TRANSLATION]]" + tags;
  return true;
}
