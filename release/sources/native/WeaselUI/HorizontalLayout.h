#pragma once

#include "StandardLayout.h"

namespace weasel {
class HorizontalLayout : public StandardLayout {
 public:
  HorizontalLayout(const UIStyle& style,
                   const Context& context,
                   const Status& status,
                   PDWR pDWR)
      : StandardLayout(style, context, status, pDWR), _unscaledStyle(style) {}
  virtual void DoLayout(CDCHandle dc, PDWR pDWR = NULL);
  virtual std::wstring GetDisplayLabel(int id) const override {
    return id >= 0 && id < MAX_CANDIDATES_COUNT ? _displayLabels[id] : L"";
  }

 private:
  void DoLanguageSplitLayout(CDCHandle dc, PDWR pDWR);
  UIStyle _unscaledStyle;
  std::wstring _displayLabels[MAX_CANDIDATES_COUNT];
};
};  // namespace weasel
