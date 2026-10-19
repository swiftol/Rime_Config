#pragma once
#include <string>

// Presentation only. Dictionary learning receives the original plain word.
// Newline is text separation, never a simulated Enter/application action.
inline bool FormatJapaneseLearningCommit(std::string& committed,
    bool enabled, const std::string& surface, const std::string& reading,
    const std::string& annotated, bool kanji, bool space, bool newline) {
  if (!enabled || surface.empty() || reading.empty() ||
      committed.compare(0, surface.size(), surface) != 0) return false;
  if (kanji && !annotated.empty()) committed.replace(0, surface.size(), annotated);
  else committed.insert(surface.size(), std::string(u8"（") + reading + u8"）");
  // Keep any punctuation from this same commit before the separator.
  if (space) committed += " ";
  // TSF text stores use one paragraph marker. CR+LF may be processed as two
  // separate breaks by the host's IMM compatibility layer.
  if (newline) committed += "\r";
  return true;
}
