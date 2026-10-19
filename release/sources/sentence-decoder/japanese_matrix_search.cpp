#include "japanese_matrix_search.h"
#include <algorithm>
#include <unordered_set>

namespace
{
constexpr std::int64_t kUnknownKanaCost = 12000;

std::vector<size_t> Utf8Boundaries(const std::string &text)
{
    std::vector<size_t> boundaries{0};
    size_t index = 0;
    while (index < text.size())
    {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : 4;
        index = (std::min)(text.size(), index + length);
        boundaries.push_back(index);
    }
    return boundaries;
}

struct MatrixNode
{
    std::string text;
    std::int64_t cost = 0;
    std::uint16_t right_id = 0;
};

void KeepBest(std::vector<MatrixNode> &row, size_t limit)
{
    if (row.size() <= limit) return;
    std::partial_sort(row.begin(), row.begin() + static_cast<std::ptrdiff_t>(limit), row.end(),
                      [](const MatrixNode &a, const MatrixNode &b) { return a.cost < b.cost; });
    row.resize(limit);
}

void AppendUnique(std::vector<japanese::SentenceCandidate> &out, std::unordered_set<std::string> &seen,
                  std::string text, std::int64_t cost, size_t limit)
{
    if (text.empty() || out.size() >= limit) return;
    if (seen.insert(text).second) out.push_back({std::move(text), cost});
}
} // namespace

namespace japanese
{
JapaneseMatrixSearch::JapaneseMatrixSearch(const JapaneseSentenceDecoder &decoder) : decoder_(decoder) {}

std::vector<SentenceCandidate> JapaneseMatrixSearch::Search(std::string_view romaji, size_t limit) const
{
    return SearchConverted(ConvertRomaji(romaji), limit);
}

std::vector<SentenceCandidate> JapaneseMatrixSearch::SearchConverted(const RomajiConversion &conversion,
                                                                     size_t limit) const
{
    return SearchReading(conversion.hiragana, conversion.pending, limit);
}

std::vector<SentenceCandidate> JapaneseMatrixSearch::SearchReading(const std::string &reading,
                                                                   const std::string &pending, size_t limit) const
{
    if (!decoder_.ready() || limit == 0) return {};
    const auto pending_kana = KanaForRomajiPrefix(pending);
    if (reading.empty())
    {
        std::vector<SentenceCandidate> result;
        std::unordered_set<std::string> seen;
        for (const auto &kana : pending_kana)
        {
            for (const auto &lemma : decoder_.PrefixLemmas(kana, 24))
            {
                AppendUnique(result, seen, lemma.surface, lemma.word_cost, limit);
                if (result.size() == limit) return result;
            }
        }
        return result;
    }

    const auto boundaries = Utf8Boundaries(reading);
    const size_t mora_count = boundaries.size() - 1;
    std::vector<std::vector<MatrixNode>> rows(mora_count + 1);
    rows[0].push_back({{}, 0, 0});

    for (size_t start_mora = 0; start_mora < mora_count; ++start_mora)
    {
        if (rows[start_mora].empty()) continue;
        const size_t start_byte = boundaries[start_mora];
        const size_t max_end = (std::min)(mora_count, start_mora + kMaxLemmaMora);
        for (size_t end_mora = start_mora + 1; end_mora <= max_end; ++end_mora)
        {
            const std::string key = reading.substr(start_byte, boundaries[end_mora] - start_byte);
            for (const auto &lemma : decoder_.ExactLemmas(key, 24))
            {
                for (const auto &previous : rows[start_mora])
                {
                    rows[end_mora].push_back({previous.text + lemma.surface,
                                              previous.cost + lemma.word_cost +
                                                  decoder_.ConnectionCost(previous.right_id, lemma.left_id),
                                              lemma.right_id});
                }
            }
        }

        const size_t next_mora = start_mora + 1;
        const std::string kana = reading.substr(start_byte, boundaries[next_mora] - start_byte);
        for (const auto &previous : rows[start_mora])
            rows[next_mora].push_back({previous.text + kana, previous.cost + kUnknownKanaCost, 0});

        for (size_t end_mora = start_mora + 1; end_mora <= max_end; ++end_mora)
            KeepBest(rows[end_mora], kMaxNodeARow);
    }

    std::vector<SentenceCandidate> result;
    std::unordered_set<std::string> seen;
    auto finals = rows[mora_count];
    for (auto &node : finals)
        node.cost += decoder_.ConnectionCost(node.right_id, 0);
    std::sort(finals.begin(), finals.end(),
              [](const MatrixNode &a, const MatrixNode &b) { return a.cost < b.cost; });
    if (!finals.empty())
        AppendUnique(result, seen, finals.front().text, finals.front().cost, limit);

    if (!pending.empty())
    {
        for (const auto &kana : pending_kana)
        {
            for (const auto &lemma : decoder_.ExactLemmas(reading + kana, 16))
                AppendUnique(result, seen, lemma.surface, lemma.word_cost, limit);
        }
        for (const auto &lemma : decoder_.PrefixLemmasContinuing(reading, pending_kana, 48))
            AppendUnique(result, seen, lemma.surface, lemma.word_cost, limit);
    }

    for (size_t index = 1; index < finals.size(); ++index)
        AppendUnique(result, seen, std::move(finals[index].text), finals[index].cost, limit);

    size_t lemma_mora = mora_count;
    while (lemma_mora > 0 && result.size() < limit)
    {
        const std::string key = reading.substr(0, boundaries[lemma_mora]);
        for (const auto &lemma : decoder_.ExactLemmas(key, 16))
        {
            AppendUnique(result, seen, lemma.surface, lemma.word_cost, limit);
            if (result.size() == limit) break;
        }
        --lemma_mora;
    }
    return result;
}
} // namespace japanese
