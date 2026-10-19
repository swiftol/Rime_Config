#pragma once

#include <algorithm>
#include <string>
#include <vector>

// These helpers have no window dependency so the real candidate glyph mask
// can be verified in a background console test without opening an IME host.
static std::wstring HiddenFuzzyValue(const std::wstring& comment,
                                    const std::wstring& name) {
  const std::wstring marker = L"[[RIME_FUZZY_" + name + L":";
  const size_t begin = comment.find(marker);
  if (begin == std::wstring::npos) return L"";
  const size_t end = comment.find(L"]]", begin + marker.size());
  return end == std::wstring::npos
             ? L""
             : comment.substr(begin + marker.size(), end - begin - marker.size());
}

static wchar_t NormalizeKana(wchar_t ch) {
  return ch >= 0x30a1 && ch <= 0x30f6 ? ch - 0x60 : ch;
}

// Mark only glyphs that differ from what the typed romaji would have produced.
// Never infer a character-to-reading map for kanji: that would highlight an
// arbitrary glyph rather than the actual corrected kana.
static std::vector<bool> FuzzyCorrectionMask(const std::wstring& candidate,
                                              const std::wstring& comment) {
  const std::wstring typed = HiddenFuzzyValue(comment, L"TYPED");
  const std::wstring reading = HiddenFuzzyValue(comment, L"READING");
  std::vector<bool> changed(candidate.size(), false);
  if (typed.empty() || reading.empty() || candidate.empty() ||
      typed.size() > 128 || candidate.size() > 128 ||
      reading.size() != candidate.size())
    return changed;
  for (size_t i = 0; i < candidate.size(); ++i) {
    const wchar_t kana = NormalizeKana(candidate[i]);
    if (!((kana >= 0x3041 && kana <= 0x309f) || kana == 0x30fc) ||
        kana != NormalizeKana(reading[i]))
      return changed;
  }
  const size_t rows = typed.size() + 1;
  const size_t columns = candidate.size() + 1;
  std::vector<int> distance(rows * columns, 0);
  const auto at = [&](size_t row, size_t column) -> int& {
    return distance[row * columns + column];
  };
  for (size_t row = 0; row < rows; ++row) at(row, 0) = static_cast<int>(row);
  for (size_t column = 0; column < columns; ++column)
    at(0, column) = static_cast<int>(column);
  for (size_t row = 1; row < rows; ++row) {
    for (size_t column = 1; column < columns; ++column) {
      const int replace = NormalizeKana(typed[row - 1]) ==
                                  NormalizeKana(candidate[column - 1])
                              ? 0 : 1;
      at(row, column) = (std::min)(
          at(row - 1, column - 1) + replace,
          (std::min)(at(row - 1, column) + 1, at(row, column - 1) + 1));
    }
  }
  size_t row = typed.size(), column = candidate.size();
  while (row > 0 || column > 0) {
    if (row > 0 && column > 0) {
      const bool equal = NormalizeKana(typed[row - 1]) ==
                         NormalizeKana(candidate[column - 1]);
      if (at(row, column) == at(row - 1, column - 1) + (equal ? 0 : 1)) {
        changed[column - 1] = !equal;
        --row;
        --column;
        continue;
      }
    }
    if (column > 0 && at(row, column) == at(row, column - 1) + 1) {
      changed[column - 1] = true;
      --column;
    } else {
      --row;
    }
  }
  return changed;
}

// The accent belongs to the reading below a candidate, not to the candidate
// itself.  Keep the brackets and any other annotation text in their normal
// colour, and map only the corrected kana into the displayed comment.
static std::vector<bool> FuzzyCorrectionCommentMask(
    const std::wstring& displayed_comment, const std::wstring& raw_comment) {
  std::vector<bool> changed(displayed_comment.size(), false);
  const std::wstring reading = HiddenFuzzyValue(raw_comment, L"READING");
  if (reading.empty()) return changed;
  const std::wstring bracketed = L"[" + reading + L"]";
  const size_t start = displayed_comment.find(bracketed);
  if (start == std::wstring::npos) return changed;
  const std::vector<bool> reading_mask =
      FuzzyCorrectionMask(reading, raw_comment);
  for (size_t i = 0; i < reading_mask.size(); ++i)
    changed[start + 1 + i] = reading_mask[i];
  return changed;
}
