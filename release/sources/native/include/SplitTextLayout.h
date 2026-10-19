#pragma once
#include <dwrite_1.h>

// Measurement and painting must share bounded, top-aligned wrapping.
inline void ConfigureSplitTextLayout(IDWriteTextLayout* layout) {
  if (!layout) return;
  layout->SetWordWrapping(DWRITE_WORD_WRAPPING_CHARACTER);
  layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
}
