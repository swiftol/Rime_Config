#pragma once
#include <cstddef>

namespace weasel {
// Shared by physical TSF navigation and the background bench. Expansion must
// not navigate using the server's independent six-column approximation.
template <class Client, class ConsumeExpansion>
void NavigatePaintedCandidateRow(Client& client, int target, bool expanded,
                                ConsumeExpansion consumeExpansion) {
  if (!expanded) {
    client.ChangePage(false);
    // Style is sent only once after expansion. Consume it before the next
    // request overwrites the transport response buffer.
    consumeExpansion();
  }
  if (target >= 0)
    client.HighlightCandidateOnCurrentPage(static_cast<size_t>(target));
}
template <class Client>
void NavigatePaintedCandidateRow(Client& client, int target, bool expanded) {
  NavigatePaintedCandidateRow(client,target,expanded,[]{});
}
}
