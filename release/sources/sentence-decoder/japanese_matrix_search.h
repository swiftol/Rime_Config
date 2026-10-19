#pragma once

#include "japanese_sentence_decoder.h"
#include "romaji_converter.h"
#include <string>
#include <string_view>
#include <vector>

namespace japanese
{
// Japanese whole-sentence search modeled on Google Pinyin MatrixSearch
// (googlepinyinime-rev: matrixsearch.cpp / ngram.cpp / dicttrie.cpp).
//
// Mapping from the Pinyin engine:
//   Pinyin letters              -> romaji letters
//   SpellingParser + SpellingId -> ConvertRomaji mora (hiragana chunks)
//   Half spelling id (shengmu)  -> pending romaji prefix (k, ky, sh, ...)
//   Lemma (word + spell ids)    -> Mozc token (reading + surface + cost)
//   DictTrie exact/prefix walk  -> binary search over reading-sorted model records
//   NGram unigram score         -> word_cost + connection_cost (right_id, left_id)
//   Matrix row per Pinyin char  -> matrix row per mora
//   Candidate 0                 -> Viterbi/n-best path covering the reading
//   prepare_candidates          -> lemmas matching a prefix of the remaining reading
//
// Search procedure (same as MatrixSearch::search + prepare_candidates):
// 1. Parse romaji into a mora sequence; leftover letters are a half-id.
// 2. DP over mora steps. From step s keep k-best nodes. Extend with lemmas
//    whose reading equals mora[s, s+len), plus a single unknown-kana backoff.
// 3. Score(node) = score(prev) + lemma.word_cost + ConnectionCost(prev.right, lemma.left).
// 4. Candidate 0 is the best path that covers every mora (and, if pending is set,
//    one extra lemma whose next kana matches the half-id).
// 5. Remaining candidates are lemmas from step 0, longest reading first.
class JapaneseMatrixSearch
{
  public:
    static constexpr size_t kMaxNodeARow = 8;
    static constexpr size_t kMaxLemmaMora = 16;

    explicit JapaneseMatrixSearch(const JapaneseSentenceDecoder &decoder);

    std::vector<SentenceCandidate> Search(std::string_view romaji, size_t limit = 16) const;
    std::vector<SentenceCandidate> SearchConverted(const RomajiConversion &conversion,
                                                   size_t limit = 16) const;
    std::vector<SentenceCandidate> SearchReading(const std::string &reading,
                                                 const std::string &pending, size_t limit = 16) const;

  private:
    const JapaneseSentenceDecoder &decoder_;
};
} // namespace japanese
