#pragma once
#include <algorithm>
#include <string>
#include <vector>

namespace weasel {
// A short grace period after an asynchronous repaint. The snapshot is of
// candidate identities, never stale indexes. No sleeps or delayed commits.
class CandidateSelectionGuard {
 public:
  void Reset() { previous_.clear(); snapshot_.clear(); input_.clear(); active_ = false; }
  void Observe(const std::wstring& input, const std::vector<std::wstring>& words,
               const std::vector<std::wstring>& ordinals, int page, int highlight,
               unsigned long long now, unsigned duration, bool enabled) {
    if (!enabled || input.empty() || input != input_ || page != page_ ||
        (ordinals == previous_ && highlight != highlight_)) {
      active_ = false;
      snapshot_.clear();
    } else if (!previous_.empty() && ordinals != previous_) {
      // Multiple cloud replies in the same grace period must not replace the
      // view the user was originally reacting to with an intermediate view.
      if (!active_ || now - changed_ >= duration) {
        snapshot_ = previous_;
        changed_ = now;
        active_ = true;
      }
    }
    input_ = input; previous_ = ordinals; words_ = words;
    page_ = page; highlight_ = highlight;
  }
  int Resolve(int ordinal, int current, unsigned long long now,
              unsigned duration, bool enabled) const {
    if (!Active(now, duration, enabled)) return current;
    if (ordinal < 0 || ordinal >= static_cast<int>(snapshot_.size()) ||
        snapshot_[ordinal].empty()) return -1;
    const auto found = std::find(words_.begin(), words_.end(), snapshot_[ordinal]);
    // If the old word disappeared, consume the selection; never commit a
    // different word just because it now occupies the same ordinal.
    return found == words_.end() ? -1 : static_cast<int>(found - words_.begin());
  }
  bool Active(unsigned long long now, unsigned duration, bool enabled) const {
    return enabled && active_ && now - changed_ < duration;
  }
 private:
  std::wstring input_;
  std::vector<std::wstring> words_, previous_, snapshot_;
  int page_ = 0, highlight_ = 0;
  unsigned long long changed_ = 0;
  bool active_ = false;
};
}
