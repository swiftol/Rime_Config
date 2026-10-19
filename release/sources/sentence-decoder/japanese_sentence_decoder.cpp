#include "japanese_sentence_decoder.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <string_view>
#include <unordered_set>

namespace
{
#pragma pack(push, 1)
struct ModelHeader
{
    char magic[8];
    std::uint32_t version;
    std::uint32_t token_count;
    std::uint32_t connection_size;
    std::uint32_t reserved;
    std::uint64_t token_offset;
    std::uint64_t connection_offset;
    std::uint64_t string_offset;
    std::uint64_t string_size;
};

struct ModelToken
{
    std::uint32_t reading_offset;
    std::uint16_t reading_length;
    std::uint32_t surface_offset;
    std::uint16_t surface_length;
    std::uint16_t left_id;
    std::uint16_t right_id;
    std::int32_t word_cost;
};
#pragma pack(pop)

constexpr char kMagic[8] = {'M', 'S', 'J', 'P', 'D', 'T', '1', '\0'};
constexpr std::int64_t kUnknownKanaCost = 12000;
constexpr size_t kShortPrefixCandidateCount = 64;

size_t FirstUtf8CodePointLength(std::string_view text)
{
    if (text.empty()) return 0;
    const unsigned char lead = static_cast<unsigned char>(text.front());
    const size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : 4;
    return (std::min)(length, text.size());
}

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
} // namespace

namespace japanese
{
JapaneseSentenceDecoder::JapaneseSentenceDecoder(std::string model_path)
{
    if (model_path.empty())
    {
        // Zhongri supplies an explicit bundled model path; do not access
        // another application's data directory implicitly.
        return;
    }
    ready_ = Load(model_path);
}

bool JapaneseSentenceDecoder::Load(const std::string &path)
{
    std::ifstream stream(path, std::ios::binary);
    ModelHeader header{};
    if (!stream.read(reinterpret_cast<char *>(&header), sizeof(header)) ||
        std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0 || header.version != 1 ||
        header.connection_size == 0 || header.token_count > 2000000 || header.string_size > (1ull << 32))
        return false;

    std::vector<ModelToken> records(header.token_count);
    stream.seekg(static_cast<std::streamoff>(header.token_offset));
    if (!stream.read(reinterpret_cast<char *>(records.data()),
                     static_cast<std::streamsize>(records.size() * sizeof(ModelToken))))
        return false;

    const std::uint64_t connection_count =
        static_cast<std::uint64_t>(header.connection_size) * header.connection_size;
    if (connection_count > 20000000) return false;
    connection_costs_.resize(static_cast<size_t>(connection_count));
    stream.seekg(static_cast<std::streamoff>(header.connection_offset));
    if (!stream.read(reinterpret_cast<char *>(connection_costs_.data()),
                     static_cast<std::streamsize>(connection_costs_.size() * sizeof(std::int16_t))))
        return false;

    strings_.assign(static_cast<size_t>(header.string_size), '\0');
    stream.seekg(static_cast<std::streamoff>(header.string_offset));
    if (!stream.read(strings_.data(), static_cast<std::streamsize>(strings_.size()))) return false;

    connection_size_ = header.connection_size;
    tokens_.reserve(records.size());
    for (const auto &record : records)
    {
        if (static_cast<std::uint64_t>(record.reading_offset) + record.reading_length > strings_.size() ||
            static_cast<std::uint64_t>(record.surface_offset) + record.surface_length > strings_.size() ||
            record.left_id >= connection_size_ || record.right_id >= connection_size_)
            return false;
        Token token;
        token.reading_offset = record.reading_offset;
        token.reading_length = record.reading_length;
        token.surface_offset = record.surface_offset;
        token.surface_length = record.surface_length;
        token.left_id = record.left_id;
        token.right_id = record.right_id;
        token.word_cost = record.word_cost;
        tokens_.push_back(std::move(token));
    }

    // Version 1 models are emitted in reading order. Keeping the file order lets us
    // binary-search the compact records directly and avoids building two million-scale
    // indexes on the first Japanese keystroke.
    if (!std::is_sorted(tokens_.begin(), tokens_.end(), [this](const Token &a, const Token &b) {
            return Reading(a) < Reading(b);
        }))
        return false;

    // Pending romaji such as "k" expands to several one-kana prefix queries. Those
    // ranges are by far the largest, so cache their best token IDs while the model is
    // already being warmed in the background. Readings are sorted, making each group
    // contiguous and keeping this pass linear with only a tiny permanent index.
    size_t group_start = 0;
    while (group_start < tokens_.size())
    {
        const auto first_reading = Reading(tokens_[group_start]);
        const auto prefix_length = FirstUtf8CodePointLength(first_reading);
        if (prefix_length == 0) return false;
        const std::string prefix(first_reading.substr(0, prefix_length));
        std::vector<std::uint32_t> best;
        best.reserve(kShortPrefixCandidateCount);
        size_t group_end = group_start;
        for (; group_end < tokens_.size(); ++group_end)
        {
            const auto reading = Reading(tokens_[group_end]);
            if (reading.size() < prefix_length || reading.compare(0, prefix_length, prefix) != 0)
                break;
            KeepBestToken(best, static_cast<std::uint32_t>(group_end), kShortPrefixCandidateCount);
        }
        SortTokenIds(best);
        short_prefix_index_.emplace(prefix, std::move(best));
        group_start = group_end;
    }
    return !tokens_.empty();
}

int JapaneseSentenceDecoder::ConnectionCost(std::uint16_t right_id, std::uint16_t left_id) const
{
    if (right_id >= connection_size_ || left_id >= connection_size_) return 10000;
    return connection_costs_[static_cast<size_t>(right_id) * connection_size_ + left_id];
}

JapaneseLemma JapaneseSentenceDecoder::MakeLemma(std::uint32_t token_id) const
{
    const Token &token = tokens_[token_id];
    return {std::string(Reading(token)), std::string(Surface(token)), token.left_id, token.right_id,
            token.word_cost, token_id};
}

std::string_view JapaneseSentenceDecoder::Reading(const Token &token) const
{
    return {strings_.data() + token.reading_offset, token.reading_length};
}

std::string_view JapaneseSentenceDecoder::Surface(const Token &token) const
{
    return {strings_.data() + token.surface_offset, token.surface_length};
}

size_t JapaneseSentenceDecoder::LowerBoundReading(std::string_view reading) const
{
    const auto it = std::lower_bound(tokens_.begin(), tokens_.end(), reading,
                                     [this](const Token &token, std::string_view value) {
                                         return Reading(token) < value;
                                     });
    return static_cast<size_t>(it - tokens_.begin());
}

bool JapaneseSentenceDecoder::TokenCheaper(std::uint32_t a, std::uint32_t b) const
{
    if (tokens_[a].word_cost != tokens_[b].word_cost)
        return tokens_[a].word_cost < tokens_[b].word_cost;
    return a < b;
}

void JapaneseSentenceDecoder::KeepBestToken(std::vector<std::uint32_t> &best,
                                            std::uint32_t token_id, size_t limit) const
{
    const auto cheaper = [this](std::uint32_t a, std::uint32_t b) { return TokenCheaper(a, b); };
    if (best.size() < limit)
    {
        best.push_back(token_id);
        if (best.size() == limit) std::make_heap(best.begin(), best.end(), cheaper);
    }
    else if (TokenCheaper(token_id, best.front()))
    {
        std::pop_heap(best.begin(), best.end(), cheaper);
        best.back() = token_id;
        std::push_heap(best.begin(), best.end(), cheaper);
    }
}

void JapaneseSentenceDecoder::SortTokenIds(std::vector<std::uint32_t> &token_ids) const
{
    std::sort(token_ids.begin(), token_ids.end(),
              [this](std::uint32_t a, std::uint32_t b) { return TokenCheaper(a, b); });
}

std::vector<JapaneseLemma> JapaneseSentenceDecoder::MakeLemmas(
    const std::vector<std::uint32_t> &token_ids, size_t limit) const
{
    std::vector<JapaneseLemma> result;
    const size_t count = (std::min)(limit, token_ids.size());
    result.reserve(count);
    for (size_t index = 0; index < count; ++index)
        result.push_back(MakeLemma(token_ids[index]));
    return result;
}

std::vector<JapaneseLemma> JapaneseSentenceDecoder::BestLemmas(
    const std::vector<std::uint32_t> &token_ids, size_t limit) const
{
    std::vector<std::uint32_t> best;
    best.reserve((std::min)(limit, token_ids.size()));
    for (const auto token_id : token_ids)
        KeepBestToken(best, token_id, limit);
    SortTokenIds(best);
    return MakeLemmas(best, limit);
}

std::vector<JapaneseLemma> JapaneseSentenceDecoder::ExactLemmas(const std::string &reading, size_t limit) const
{
    if (!ready_ || reading.empty() || limit == 0) return {};
    std::vector<std::uint32_t> matches;
    for (size_t index = LowerBoundReading(reading); index < tokens_.size(); ++index)
    {
        if (Reading(tokens_[index]) != reading) break;
        matches.push_back(static_cast<std::uint32_t>(index));
    }
    return BestLemmas(matches, limit);
}

std::vector<JapaneseLemma> JapaneseSentenceDecoder::PrefixLemmas(const std::string &reading_prefix,
                                                                 size_t limit) const
{
    if (!ready_ || reading_prefix.empty() || limit == 0) return {};
    const auto cached = short_prefix_index_.find(reading_prefix);
    if (cached != short_prefix_index_.end() && limit <= kShortPrefixCandidateCount)
        return MakeLemmas(cached->second, limit);

    std::vector<std::uint32_t> matches;
    for (size_t index = LowerBoundReading(reading_prefix); index < tokens_.size(); ++index)
    {
        const auto token_reading = Reading(tokens_[index]);
        if (token_reading.size() < reading_prefix.size() ||
            token_reading.compare(0, reading_prefix.size(), reading_prefix) != 0)
            break;
        matches.push_back(static_cast<std::uint32_t>(index));
    }
    return BestLemmas(matches, limit);
}

std::vector<JapaneseLemma> JapaneseSentenceDecoder::LongerPrefixLemmas(
    const std::string &reading_prefix, size_t limit) const
{
    if (!ready_ || reading_prefix.empty() || limit == 0) return {};
    std::vector<std::uint32_t> best;
    best.reserve(limit);
    for (size_t index = LowerBoundReading(reading_prefix); index < tokens_.size(); ++index)
    {
        const auto token_reading = Reading(tokens_[index]);
        if (token_reading.compare(0, reading_prefix.size(), reading_prefix) != 0) break;
        // Exclude exact readings before applying the limit, not afterwards.
        if (token_reading.size() > reading_prefix.size())
            KeepBestToken(best, static_cast<std::uint32_t>(index), limit);
    }
    SortTokenIds(best);
    return MakeLemmas(best, limit);
}

std::vector<JapaneseLemma> JapaneseSentenceDecoder::PrefixLemmasContinuing(
    const std::string &reading_prefix, const std::vector<std::string> &next_kana, size_t limit) const
{
    if (!ready_ || reading_prefix.empty() || next_kana.empty() || limit == 0) return {};
    std::vector<std::uint32_t> matches;
    for (size_t index = LowerBoundReading(reading_prefix); index < tokens_.size(); ++index)
    {
        const auto token_reading = Reading(tokens_[index]);
        if (token_reading.compare(0, reading_prefix.size(), reading_prefix) != 0)
            break;
        if (token_reading.size() <= reading_prefix.size()) continue;
        const std::string_view remaining(token_reading.data() + reading_prefix.size(),
                                         token_reading.size() - reading_prefix.size());
        bool matched = false;
        for (const auto &kana : next_kana)
        {
            if (remaining.size() >= kana.size() && remaining.compare(0, kana.size(), kana) == 0)
            {
                matched = true;
                break;
            }
        }
        if (matched) matches.push_back(static_cast<std::uint32_t>(index));
    }
    return BestLemmas(matches, limit);
}

std::vector<SentenceCandidate> JapaneseSentenceDecoder::Decode(const std::string &reading, size_t limit) const
{
    if (!ready_ || reading.empty() || limit == 0) return {};
    struct Path { std::string text; std::int64_t cost; std::uint16_t right_id; size_t unknown = 0; };
    const auto boundaries = Utf8Boundaries(reading);
    std::vector<std::vector<Path>> paths(reading.size() + 1);
    paths[0].push_back({{}, 0, 0});
    const size_t beam = (std::max)(size_t{16}, limit * 4);
    // Repeated kana substrings share dictionary lookups within this request.
    // Keep the original POS connection scoring and search result budget.
    std::unordered_map<std::string, std::vector<JapaneseLemma>> lemma_cache;

    for (size_t boundary_index = 0; boundary_index + 1 < boundaries.size(); ++boundary_index)
    {
        const size_t start = boundaries[boundary_index];
        if (paths[start].empty()) continue;
        for (size_t end_index = boundary_index + 1; end_index < boundaries.size(); ++end_index)
        {
            const size_t end = boundaries[end_index];
            const std::string key = reading.substr(start, end - start);
            auto found = lemma_cache.find(key);
            if (found == lemma_cache.end()) found = lemma_cache.emplace(key, ExactLemmas(key, 24)).first;
            const auto &lemmas = found->second;
            for (const auto &lemma : lemmas)
            {
                for (const auto &previous : paths[start])
                {
                    paths[end].push_back({previous.text + lemma.surface,
                                          previous.cost + lemma.word_cost +
                                              ConnectionCost(previous.right_id, lemma.left_id),
                                          lemma.right_id, previous.unknown});
                }
            }
        }

        const size_t next = boundaries[boundary_index + 1];
        const std::string kana = reading.substr(start, next - start);
        for (const auto &previous : paths[start])
            paths[next].push_back({previous.text + kana, previous.cost + kUnknownKanaCost, 0, previous.unknown + 1});

        for (size_t end_index = boundary_index + 1; end_index < boundaries.size(); ++end_index)
        {
            auto &bucket = paths[boundaries[end_index]];
            if (bucket.size() > beam)
            {
                std::partial_sort(bucket.begin(), bucket.begin() + beam, bucket.end(),
                                  [](const Path &a, const Path &b) { return a.cost < b.cost; });
                bucket.resize(beam);
            }
        }
    }

    auto finals = std::move(paths[reading.size()]);
    for (auto &path : finals) path.cost += ConnectionCost(path.right_id, 0);
    std::sort(finals.begin(), finals.end(), [](const Path &a, const Path &b) { return a.cost < b.cost; });
    std::vector<SentenceCandidate> result;
    std::unordered_set<std::string> seen;
    for (auto &path : finals)
    {
        if (seen.insert(path.text).second) result.push_back({std::move(path.text), path.cost, path.unknown});
        if (result.size() == limit) break;
    }
    return result;
}
} // namespace japanese
