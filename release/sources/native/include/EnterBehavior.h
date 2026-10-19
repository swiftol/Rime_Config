#pragma once
#include "rime_api.h"
#include <string>

// Persisted order matches the four settings entries. No host input injection
// or registry access here: both production and isolated tests share the policy.
inline bool ValidEnterMode(int mode) { return mode >= 0 && mode <= 3; }
inline bool EnterForwardsToHost(int mode) { return mode == 2 || mode == 3; }
struct EnterBehaviorResult {
  bool handled = false;
  bool forward = false;
  std::string raw;
};
inline EnterBehaviorResult ApplyEnterBehavior(RimeApi* api, RimeSessionId session,
                                             int mode, int global_first = -1) {
  EnterBehaviorResult result;
  if (!ValidEnterMode(mode)) return result;
  const char* input = api->get_input(session);
  const std::string original = input ? input : "";
  if (original.empty()) return result;  // Empty composition: ordinary app Enter.
  if (mode == 0 || mode == 3) {
    result.raw = original;  // Caller stages this as genuine commit, not preedit.
    api->clear_composition(session);
  } else {
    const bool selected = global_first >= 0
        ? api->select_candidate(session, static_cast<size_t>(global_first))
        : api->select_candidate_on_current_page(session, 0);
    if (!selected) return result;
    const char* remaining = api->get_input(session);
    if (remaining && *remaining) api->commit_composition(session);
  }
  result.handled = true;
  result.forward = EnterForwardsToHost(mode);
  return result;
}

// A host Enter is released once only after BOTH the physical OnKeyDown and
// the commit edit session have completed. Repeated OnTestKeyDown never arms it.
struct EnterReplayGate {
  bool armed = false, applied = false, down = false;
  void Arm() { armed = true; applied = down = false; }
  void Cancel() { armed = applied = down = false; }
  void CommitApplied(bool success) { if (!success) Cancel(); else if (armed) applied = true; }
  void KeyDown() { if (armed) down = true; }
  bool Take() {
    if (!armed || !applied || !down) return false;
    Cancel(); return true;
  }
};
