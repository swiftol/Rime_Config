-- Stable four-tier ordering for the Chinese/Japanese Mozc schema:
--   1. exact Chinese
--   2. exact Japanese
--   3. fuzzy Japanese
--   4. Japanese prefix/prediction candidates
-- Everything else keeps its original relative order after these buckets.
local LANGUAGE_ZH = "[[RIME_LANG:ZH]]"
local LANGUAGE_JA = "[[RIME_LANG:JA]]"
local routing = require("mozc_v2_translator")
local chinese_learning = require("chinese_abbreviation_learning")
local RECORD_SEPARATOR = string.char(30)
local TRANSLATION_EN = RECORD_SEPARATOR .. "EN:"
local TRANSLATION_JA = RECORD_SEPARATOR .. "JA:"
local CHINESE_EXACT_OVERRIDES = {
  biaojiaming = "标假名",
  xianzaishuru = "现在输入",
  zonggj = "总感觉",
  jtxqj = "今天星期几",
}

-- Last-resort continuity cache.  Candidate production is normally stateless,
-- but an unfinished extra letter can make every upstream translator return
-- nothing.  A candidate window must never disappear in that situation: reuse
-- the nearest earlier frame that did have results, all the way back to the
-- first typed letter if necessary.
local nonempty_frames, nonempty_frame_order = {}, {}
local NONEMPTY_FRAME_LIMIT = 128

local function remember_nonempty_frame(typed, candidates)
  if typed == "" or #candidates == 0 then return end
  local snapshot = {}
  for index = 1, math.min(9, #candidates) do
    local cand = candidates[index]
    snapshot[#snapshot + 1] = {
      text = cand.text,
      comment = cand.comment or "",
      type = cand.type or "continuity_fallback",
    }
  end
  if not nonempty_frames[typed] then
    nonempty_frame_order[#nonempty_frame_order + 1] = typed
    if #nonempty_frame_order > NONEMPTY_FRAME_LIMIT then
      local oldest = table.remove(nonempty_frame_order, 1)
      nonempty_frames[oldest] = nil
    end
  end
  nonempty_frames[typed] = snapshot
end

local function nearest_nonempty_frame(typed, start_pos, end_pos)
  for length = #typed - 1, 1, -1 do
    local snapshot = nonempty_frames[typed:sub(1, length)]
    if snapshot and #snapshot > 0 then
      local result = {}
      for index, item in ipairs(snapshot) do
        local cand = Candidate("continuity_fallback", start_pos, end_pos,
                               item.text, item.comment)
        cand.quality = 1 - index * 0.001
        result[#result + 1] = cand
      end
      return result
    end
  end
  return {}
end

local function contains(value, marker)
  return (value or ""):find(marker, 1, true) ~= nil
end

local function has_translation_metadata(comment)
  comment = comment or ""
  return contains(comment, TRANSLATION_EN) or contains(comment, TRANSLATION_JA)
end

-- Learned abbreviations are synthetic high-priority candidates.  A normal
-- dictionary candidate for the same text can arrive later with the EN/JA
-- payload, which used to be discarded by the final uniquifier.  Copy that
-- payload onto the learned candidate before ordering so the first item keeps
-- exactly the same annotations as its dictionary twin.  This is text-generic
-- and adds no dictionary scan to ordinary (non-learned) input.
local function inherit_learned_translation_comments(learned, buckets)
  if #learned == 0 then return end
  local comments = {}
  for _, bucket in ipairs(buckets) do
    for _, cand in ipairs(bucket) do
      if cand.type ~= "chinese_abbreviation_learning" and
         has_translation_metadata(cand.comment) and
         not comments[cand.text] then
        comments[cand.text] = cand.comment
      end
    end
  end
  for index, cand in ipairs(learned) do
    local comment = comments[cand.text]
    if comment and not has_translation_metadata(cand.comment) then
      learned[index] = ShadowCandidate(cand, cand.type, cand.text, comment)
    end
  end
end

local function has_latin_letter(value)
  for _, cp in utf8.codes(value or "") do
    if (cp >= 0x0041 and cp <= 0x005a) or
       (cp >= 0x0061 and cp <= 0x007a) or
       (cp >= 0x00c0 and cp <= 0x024f) or
       (cp >= 0x1e00 and cp <= 0x1eff) then
      return true
    end
  end
  return false
end

local function contains_emoji(value)
  for _, cp in utf8.codes(value or "") do
    if (cp >= 0x1F000 and cp <= 0x1FAFF) or
       (cp >= 0x2190 and cp <= 0x23FF) or
       (cp >= 0x2B00 and cp <= 0x2BFF) or
       (cp >= 0x2600 and cp <= 0x27BF) or
       cp == 0x20E3 then
      return true
    end
  end
  return false
end

local function has_han_and_kana(value)
  local has_han, has_kana = false, false
  for _, cp in utf8.codes(value or "") do
    if (cp >= 0x3400 and cp <= 0x9fff) or
       (cp >= 0xf900 and cp <= 0xfaff) then
      has_han = true
    elseif cp >= 0x3040 and cp <= 0x30ff then
      has_kana = true
    end
  end
  return has_han and has_kana
end

local function reduced_prefix_against(best, alternate, minimum_suffix)
  local left, right = {}, {}
  for _, cp in utf8.codes(best or "") do left[#left + 1] = utf8.char(cp) end
  for _, cp in utf8.codes(alternate or "") do right[#right + 1] = utf8.char(cp) end
  local common = 0
  while common < #left and common < #right and
        left[#left - common] == right[#right - common] do
    common = common + 1
  end
  if common < minimum_suffix or common >= #right then return nil end
  return table.concat(right, "", 1, #right - common),
         table.concat(right, "", #right - common + 1, #right)
end

local function kana_chars(value)
  local result = {}
  for _, cp in utf8.codes(value or "") do
    -- Normalize ordinary katakana to hiragana; preserve the long mark.
    if cp >= 0x30a1 and cp <= 0x30f6 then cp = cp - 0x60 end
    result[#result + 1] = utf8.char(cp)
  end
  return result
end

local function all_kana_reading(value)
  local chars = kana_chars(value)
  if #chars == 0 then return nil end
  for _, char in ipairs(chars) do
    local cp = utf8.codepoint(char)
    if not ((cp >= 0x3041 and cp <= 0x309f) or cp == 0x30fc) then
      return nil
    end
  end
  return table.concat(chars)
end

local function leading_kana_reading(value)
  local result = {}
  for _, cp in utf8.codes(value or "") do
    if cp >= 0x30a1 and cp <= 0x30f6 then cp = cp - 0x60 end
    if not ((cp >= 0x3041 and cp <= 0x309f) or cp == 0x30fc) then break end
    result[#result + 1] = utf8.char(cp)
  end
  return #result > 0 and table.concat(result) or nil
end

local function reading_prefix_before(full_reading, needle, expected_chars)
  local full, target = kana_chars(full_reading), kana_chars(needle)
  if #target == 0 or #target > #full then return nil end
  local best_prefix, best_distance
  for start = 1, #full - #target + 1 do
    local matches = true
    for offset = 1, #target do
      local left, right = full[start + offset - 1], target[offset]
      -- Spoken wa and written particle ha identify the same roman boundary.
      if left ~= right and not (offset == 1 and
          ((left == "わ" and right == "は") or
           (left == "は" and right == "わ"))) then
        matches = false
        break
      end
    end
    if matches then
      local distance = math.abs((start - 1) - expected_chars)
      if not best_distance or distance < best_distance then
        best_distance = distance
        best_prefix = table.concat(full, "", 1, start - 1)
      end
    end
  end
  return best_prefix
end

local function partial_prefix_length(typed, head, common_suffix, alternate)
  local full_reading = routing.romaji_to_hiragana(typed)
  local head_reading = all_kana_reading(head)
  if head_reading and full_reading:sub(1, #head_reading) == head_reading then
    local boundary = routing.prefix_length_for_reading(typed, head_reading)
    if boundary then return boundary end
  end

  -- Most mechanical variants share a suffix beginning with a kana particle
  -- or ending.  Locate that suffix start in the full phonetic reading and map
  -- the preceding reading back to its exact roman boundary.  Choose the
  -- occurrence closest to the candidate-text ratio when a short particle
  -- such as の appears more than once.
  local suffix_reading = leading_kana_reading(common_suffix)
  if suffix_reading then
    local alternate_length = math.max(utf8.len(alternate or "") or 0, 1)
    local expected = math.floor((utf8.len(head or "") or 0) /
                                alternate_length *
                                (utf8.len(full_reading) or 0) + 0.5)
    local before = reading_prefix_before(full_reading, suffix_reading, expected)
    local boundary = before and
                     routing.prefix_length_for_reading(typed, before) or nil
    if boundary then return boundary end
  end

  -- Compatibility fallback for an all-kanji head immediately before the
  -- productive topic particle.  This also covers older candidates lacking a
  -- usable kana suffix marker.
  local position, last_particle_prefix = 1, nil
  while true do
    local found = typed:find("wa", position, true)
    if not found then break end
    if found > 1 and found + 1 < #typed then
      last_particle_prefix = found - 1
    end
    position = found + 2
  end
  if last_particle_prefix then return last_particle_prefix end
  return routing.first_mora_length(typed)
end

-- The phonetic alias wa -> は is useful only when it is visible.  The
-- upstream fuzzy filter already places common particles after the strongest
-- exact Chinese result, but this final four-tier sorter used to collect every
-- Chinese homophone first and push は to the next page.  Preserve one leading
-- Chinese exact candidate, then expose the explicitly requested particle.
local function promote_wa_particle(items, typed)
  if typed ~= "wa" or #items < 2 then return items end
  local particle_index
  for index = 2, #items do
    if items[index].text == "は" then
      particle_index = index
      break
    end
  end
  if not particle_index or particle_index == 2 then return items end
  local particle = table.remove(items, particle_index)
  table.insert(items, 2, particle)
  return items
end

-- A completed single kana spelling (`da` -> だ, `shi` -> し, `tsu` -> つ)
-- must remain visible on the first row of the mixed schema.  Keep the best
-- Chinese candidate in slot 1, then place the exact gojuon form in slot 2;
-- longer Japanese expressions such as desu are handled by normal ranking.
local function promote_exact_gojuon(items, typed)
  if #typed < 2 or #items < 2 then return items end
  local kana = routing.romaji_to_hiragana(typed)
  if not kana or kana == "" or utf8.len(kana) ~= 1 then return items end
  local kana_index
  for index = 2, #items do
    if items[index].text == kana then
      kana_index = index
      break
    end
  end
  if not kana_index or kana_index == 2 then return items end
  local exact = table.remove(items, kana_index)
  table.insert(items, 2, exact)
  return items
end

-- A three-letter doubled consonant is an unfinished Japanese boundary.  Raw
-- dictionary order is usually flooded by one vowel continuation.  Select one
-- candidate for each common continuation first, using the generated kana
-- reading rather than any output-word list, then append the remaining items.
local function diversify_doubled_associations(items, typed)
  if #typed ~= 3 or typed:sub(-1) ~= typed:sub(-2, -2) then return items end
  local final = typed:sub(-1)
  if not final:match("[bcdfghjklmpqrstvwxyz]") then return items end
  local ordered, used = {}, {}
  for _, suffix in ipairs({"an", "ai", "ou", "a", "i", "u", "e", "o"}) do
    local reading = routing.romaji_to_hiragana(typed .. suffix)
    if reading and reading ~= "" then
      local marker = "[[RIME_JR:" .. reading .. "]]"
      for index, cand in ipairs(items) do
        if not used[index] and contains(cand.comment or "", marker) then
          ordered[#ordered + 1] = cand
          used[index] = true
          break
        end
      end
    end
  end
  for index, cand in ipairs(items) do
    if not used[index] then ordered[#ordered + 1] = cand end
  end
  return ordered
end

local function has_productive_chinese_next_initial(typed)
  if not typed:match("^[a-z]+$") then return false end
  for _, suffix_length in ipairs({ 2, 1 }) do
    if #typed > suffix_length then
      local suffix = typed:sub(-suffix_length)
      local valid_initial = suffix_length == 1 and
          suffix:match("^[bcdfghjklmnpqrstvwxyz]$") or
          suffix == "zh" or suffix == "ch" or suffix == "sh"
      if valid_initial and
         routing.is_complete_pinyin(typed:sub(1, -suffix_length - 1)) then
        return true
      end
    end
  end
  return false
end

local function phrases_before_single_characters(items)
  local phrases, singles = {}, {}
  for _, cand in ipairs(items) do
    local ok, length = pcall(utf8.len, cand.text or "")
    local bucket = ok and length and length > 1 and phrases or singles
    bucket[#bucket + 1] = cand
  end
  for _, cand in ipairs(singles) do phrases[#phrases + 1] = cand end
  return phrases
end

local function promote_chinese_prefix_phrases(items, enabled)
  if not enabled then return items end
  local phrases, rest = {}, {}
  for _, cand in ipairs(items) do
    local ok, length = pcall(utf8.len, cand.text or "")
    if contains(cand.comment, LANGUAGE_ZH) and
       ok and length and length > 1 then
      phrases[#phrases + 1] = cand
    else
      rest[#rest + 1] = cand
    end
  end
  for _, cand in ipairs(rest) do phrases[#phrases + 1] = cand end
  return phrases
end

local function promote_first_chinese_prefix_phrase(items, enabled)
  if not enabled then return items end
  local first_phrase
  for index, cand in ipairs(items) do
    local ok, length = pcall(utf8.len, cand.text or "")
    if contains(cand.comment, LANGUAGE_ZH) and
       ok and length and length > 1 then
      first_phrase = index
      break
    end
  end
  if not first_phrase or first_phrase == 1 then return items end
  local result = { items[first_phrase] }
  for index, cand in ipairs(items) do
    if index ~= first_phrase then result[#result + 1] = cand end
  end
  return result
end

local function apply_chinese_prefix_promotion(
    items, enabled, association_disabled, japanese_first)
  if not enabled then return items end
  -- With association enabled in Chinese-first mode, move only the strongest
  -- Chinese word.  Moving every phrase would push 傘 and other genuine
  -- Japanese associations beyond the bounded candidate menu.
  if not association_disabled and not japanese_first then
    return promote_first_chinese_prefix_phrase(items, true)
  end
  return promote_chinese_prefix_phrases(items, true)
end

-- Keep the complete learned order of same-reading Chinese candidates stable
-- while the user completes a one-syllable pinyin (`b` -> 把/吧, then `ba` uses
-- the same relative order).  Only reorder candidates that already exist for
-- the full syllable; never manufacture a reading.  Restricting inheritance to
-- one real Mandarin syllable prevents an initial preference from overriding
-- multi-syllable words such as nihao or Japanese sentences.
local function promote_learned_initial_in_syllable(items, typed)
  if #typed <= 1 or not typed:match("^[a-z]+$") or
     not routing.is_single_pinyin_syllable(typed) then
    return items
  end
  local learned_rank = {}
  for rank, entry in ipairs(chinese_learning.ranked(typed:sub(1, 1))) do
    learned_rank[entry.text] = rank
  end
  if next(learned_rank) == nil then return items end

  local matched, rest = {}, {}
  for sequence, cand in ipairs(items) do
    local rank = learned_rank[cand.text]
    if rank and contains(cand.comment, LANGUAGE_ZH) then
      matched[#matched + 1] = { candidate = cand, rank = rank,
                                sequence = sequence }
    else
      rest[#rest + 1] = cand
    end
  end
  if #matched == 0 then return items end
  table.sort(matched, function(left, right)
    if left.rank ~= right.rank then return left.rank < right.rank end
    return left.sequence < right.sequence
  end)
  local result = {}
  for _, entry in ipairs(matched) do result[#result + 1] = entry.candidate end
  for _, cand in ipairs(rest) do result[#result + 1] = cand end
  return result
end

local function filter(input, env)
  if env.engine.context:get_option("japanese_input_table_enabled") then
    if env.engine.context:get_option("japanese_input_table_mixed") then
      local candidates = {}
      for cand in input:iter() do candidates[#candidates + 1] = cand end
      local learned, learned_indices = {}, {}
      for index, cand in ipairs(candidates) do
        if cand.type == "chinese_abbreviation_learning" then
          learned[#learned + 1], learned_indices[#learned_indices + 1] = cand, index
        end
      end
      inherit_learned_translation_comments(learned, { candidates })
      for index, cand in ipairs(learned) do candidates[learned_indices[index]] = cand end
      local context = env.engine.context
      local active = context.composition:back()
      candidates = chinese_learning.promote(candidates,
          active and active.start or 0, active and active._end or #(context.input or ""))
      -- Imported tables bypass the default sorter, but must still honour
      -- emoji placement after shared Chinese frequency promotion.
      local unique, seen = {}, {}
      for _, cand in ipairs(candidates) do
        if not seen[cand.text] then
          seen[cand.text] = true
          unique[#unique + 1] = cand
        end
      end
      if context:get_option("emoji_second_position") then
        local normal, learned_emoji, generated = {}, {}, {}
        for _, cand in ipairs(unique) do
          if cand.type == "emoji_learning" then
            learned_emoji[#learned_emoji + 1] = cand
          elseif contains_emoji(cand.text) then
            generated[#generated + 1] = cand
          else
            normal[#normal + 1] = cand
          end
        end
        if normal[1] then yield(normal[1]) end
        if learned_emoji[1] then yield(learned_emoji[1]) end
        for index = 2, #normal do yield(normal[index]) end
        for index = 2, #learned_emoji do yield(learned_emoji[index]) end
        for _, cand in ipairs(generated) do yield(cand) end
      else
        for _, cand in ipairs(unique) do yield(cand) end
      end
    else
      for cand in input:iter() do yield(cand) end
    end
    return
  end
  if env.engine.schema.schema_id ~= "rime_ice_japanese_mozc" then
    for cand in input:iter() do yield(cand) end
    return
  end

  local learned_emoji, learned_abbreviation, preferred_chinese = {}, {}, {}
  local single_initial_chinese = {}
  local invalid_input_chinese = {}
  local chinese_exact_single, chinese_exact = {}, {}
  local japanese_exact, preferred_japanese, chinese_completion = {}, {}, {}
  local japanese_continuation = {}
  local japanese_fuzzy, japanese_association, chinese_fallback = {}, {}, {}
  local japanese_core_association, japanese_prefix_heads = {}, {}
  local japanese_character_heads = {}
  local has_japanese_segment_head = false
  local generated_emoji, other = {}, {}
  local context = env.engine.context
  local association_disabled =
      context:get_option("japanese_prefix_completion_disabled")
  local japanese_association_first =
      context:get_option("japanese_prefix_completion_japanese_first")
  local active_input = context.input or ""
  local active = context.composition and context.composition:back() or nil
  if active and active.start and active.start > 0 then
    active_input = active_input:sub(active.start + 1, active._end or #active_input)
  end
  local typed = active_input:lower():gsub("[%s']+", "")
  local complete_single_pinyin =
      typed:match("^[a-z]+$") ~= nil and
      routing.is_single_pinyin_syllable(typed)
  local explicit_long_q = routing.is_explicit_long_q(typed)
  local unfinished_consonant =
      typed:match("[bcdfghjklmprstvwxyz]$") ~= nil and not explicit_long_q
  local valid_japanese_input = routing.is_valid_japanese_input(typed)
  -- A terminal q after a vowel is ambiguous with a new Mandarin initial.
  -- When the complete q-to-minus spelling exists in the Japanese lexicon,
  -- keep that exact conversion ahead of Chinese prefix completions.
  local exact_japanese_terminal_q = false
  if typed:match("[aeiou]q$") and valid_japanese_input then
    env.japanese_exact_memory = env.japanese_exact_memory or
        Memory(env.engine, Schema("japanese"))
    exact_japanese_terminal_q = routing.has_exact_japanese_terminal_q(
        typed, env.japanese_exact_memory)
  end
  -- A complete Mandarin syllable plus the next initial is a word prefix, not
  -- a request for a row of fallback single characters.  Prefer productive
  -- Chinese words whenever Japanese association is off or Chinese-first; a
  -- leading q is never a valid Japanese long mark and follows this rule too.
  local promote_productive_chinese_prefix =
      #typed >= 3 and #typed <= 8 and
      not complete_single_pinyin and
      not exact_japanese_terminal_q and
      has_productive_chinese_next_initial(typed) and
      (association_disabled or not japanese_association_first or
       typed:sub(1, 1) == "q")
  -- This filter must buffer before it can apply the four-tier ordering, but
  -- consuming an unbounded upstream stream makes three/four-letter input wait
  -- behind hundreds of candidates that the compact UI can never display.
  -- The upstream fuzzy filter has already promoted validated whole-word fuzzy
  -- matches into its bounded head, so a short 128-item window preserves the
  -- useful first rows.  Longer spellings keep extra room for sentence and
  -- partial-candidate ranking.
  local scan_limit = (#typed < 5) and 128 or 384
  local scanned = 0

  for cand in input:iter() do
    scanned = scanned + 1
    local comment = cand.comment or ""
    local candidate_type = cand.type or ""
    local quality = tonumber(cand.quality) or 0
    local raw_preedit = (cand.preedit or ""):lower()
    local preedit = raw_preedit:gsub("[%s']+", "")
    -- min'g and ming collapse to the same string, but the former contains
    -- an abbreviated second syllable. It is not a whole-spelling match.
    local full_syllables = true
    for syllable in raw_preedit:gmatch("[^%s']+") do
      if not routing.is_complete_pinyin(syllable) then
        full_syllables = false
        break
      end
    end
    local is_zh = contains(comment, LANGUAGE_ZH)
    local is_ja = contains(comment, LANGUAGE_JA)
    local is_fuzzy = contains(comment, "[JF_READING]") or
                     contains(comment, "[JF:")
    local is_association = is_ja and
      (candidate_type == "completion" or
       candidate_type == "mozc_v2_prefix" or
       (candidate_type == "mozc_v2" and
        (unfinished_consonant or has_latin_letter(cand.text))))

    -- Only whole-code dictionary phrases belong to tier 1.  Chinese sentence
    -- assembly, partial syllables and completions are fallbacks, not "exact
    -- Chinese" (for example 萨苏咖 for sasuka must not outrank 指すか).
    local is_exact_chinese = is_zh and full_syllables and preedit == typed and
      (((candidate_type == "phrase" or candidate_type == "user_phrase") and
        quality >= 299) or
       CHINESE_EXACT_OVERRIDES[typed] == cand.text)
    local is_preferred_chinese = CHINESE_EXACT_OVERRIDES[typed] == cand.text
    local text_length_ok, text_length = pcall(utf8.len, cand.text or "")
    local is_exact_single_chinese =
        complete_single_pinyin and is_zh and full_syllables and
        preedit == typed and text_length_ok and text_length == 1
    if is_ja and has_latin_letter(cand.text) then
      -- A Japanese conversion containing residual Latin letters is an
      -- unfinished/failed conversion, never a useful candidate.  In
      -- particular `aqy` used to expose あーÿ/あーȲ/あーȳ before the genuine
      -- Chinese abbreviation 爱奇艺.  Drop the entire class rather than
      -- special-casing one spelling; completed Japanese such as `aqya` and
      -- `koqhiq` contains no Latin residue and is unaffected.
    elseif candidate_type == "emoji_learning" then
      learned_emoji[#learned_emoji + 1] = cand
    elseif contains_emoji(cand.text) then
      -- OpenCC can manufacture many emoji variants from ordinary Chinese or
      -- Japanese candidates.  Keep at most one suggestion and never let that
      -- generated batch precede real text.  Explicitly learned emoji remain
      -- in the dedicated high-priority bucket above.
      -- Automatic symbol variants have no useful relation to an unfinished
      -- romaji stream. Keep explicit symbol input available, but do not emit
      -- these variants anywhere in ordinary alphabetic composition. Learned
      -- emoji are handled separately above and retain the user's preference.
      if not typed:match("^[a-z]+$") and #generated_emoji == 0 then
        generated_emoji[1] = cand
      end
    elseif candidate_type == "chinese_abbreviation_learning" then
      learned_abbreviation[#learned_abbreviation + 1] = cand
    elseif candidate_type == "mozc_v2_character_head" or
           candidate_type == "mozc_v2_segment_head" then
      japanese_character_heads[#japanese_character_heads + 1] = cand
      if candidate_type == "mozc_v2_segment_head" then
        has_japanese_segment_head = true
      end
    elseif candidate_type == "mozc_v2_context_continuation" then
      japanese_continuation[#japanese_continuation + 1] = cand
    elseif candidate_type == "mozc_v2_preferred_correction" then
      preferred_japanese[#preferred_japanese + 1] = cand
    elseif candidate_type == "mozc_v2_particle_alias" then
      preferred_japanese[#preferred_japanese + 1] = cand
    elseif is_preferred_chinese then
      preferred_chinese[#preferred_chinese + 1] = cand
    elseif is_exact_single_chinese then
      -- A complete Mandarin syllable can also be split mechanically as a
      -- shorter syllable plus a next initial (`ming` = `min` + `g`, `jiang`
      -- = `ji` + `ang`).  Those prefix words are useful only while the whole
      -- input is incomplete.  For a complete one-syllable spelling, keep the
      -- genuine same-reading Han characters in the exact layer.
      chinese_exact_single[#chinese_exact_single + 1] = cand
    elseif #typed == 1 and typed:match("^[a-z]$") and is_zh then
      -- A lone consonant is overwhelmingly used as Chinese initials in this
      -- mixed scheme.  Preserve the Chinese stream's own order (including
      -- pin_cand_filter and user frequency) ahead of Japanese unfinished-kana
      -- associations.  This applies to every letter; no output word is
      -- hard-coded here.
      single_initial_chinese[#single_initial_chinese + 1] = cand
    elseif is_exact_chinese then
      chinese_exact[#chinese_exact + 1] = cand
    elseif candidate_type == "chinese_prefix_completion" then
      chinese_completion[#chinese_completion + 1] = cand
    elseif is_zh and not valid_japanese_input then
      -- For a stream that cannot be parsed as Japanese romaji, genuine
      -- Chinese results must precede permissive Japanese/OpenCC guesses.
      -- Example: ywtia should start with 有问题啊, not つばさ舞.
      invalid_input_chinese[#invalid_input_chinese + 1] = cand
    elseif is_association and association_disabled then
      -- The UI switch is a master control for every unfinished-romaji
      -- prediction, including candidates accidentally leaked by a core or
      -- fuzzy translator.  Exact completed Japanese remains unaffected.
    elseif is_association then
      if candidate_type == "completion" then
        -- The core translator deliberately diversifies doubled-consonant
        -- prefixes (`ipp` -> 一般/いっぱい/一方) without blocking mailbox
        -- requests.  Keep that bounded result ahead of the generic prefix
        -- dictionary stream when association is enabled.
        japanese_core_association[#japanese_core_association + 1] = cand
      else
        japanese_association[#japanese_association + 1] = cand
      end
    elseif is_ja and is_fuzzy then
      japanese_fuzzy[#japanese_fuzzy + 1] = cand
    elseif is_ja then
      japanese_exact[#japanese_exact + 1] = cand
    elseif is_zh then
      chinese_fallback[#chinese_fallback + 1] = cand
    else
      other[#other + 1] = cand
    end
    if scanned >= scan_limit then break end
  end

  japanese_core_association =
      diversify_doubled_associations(japanese_core_association, typed)
  if promote_productive_chinese_prefix then
    invalid_input_chinese =
        phrases_before_single_characters(invalid_input_chinese)
    chinese_fallback = phrases_before_single_characters(chinese_fallback)
  end

  -- When the whole stream cannot be Japanese but the dedicated Chinese
  -- prefix translator has proved a complete-syllable + next-initial parse
  -- (`bei` + `p...`, for example), its multi-character completions are more
  -- specific than the fallback candidates for the completed first syllable.
  -- Keep those words on the first row instead of showing nine `bei` single
  -- characters and hiding 被迫/背叛/etc. on the second page.  Valid Japanese
  -- prefixes retain the user-selected Chinese/Japanese association order.
  local chinese_prefix_before_invalid = {}
  if (association_disabled or not valid_japanese_input) and
     #chinese_completion > 0 then
    chinese_prefix_before_invalid = chinese_completion
    chinese_completion = {}
  end

  local normal_buckets = {
    japanese_continuation,
    preferred_chinese,
  }
  if #typed == 1 and typed:match("^[a-z]$") and
     #single_initial_chinese > 0 then
    -- Single-letter layout contract: one preferred Chinese candidate in slot
    -- 1, then expose the kana/gojuon row, then return to the remaining Chinese
    -- initials.  Promoting the whole Chinese stream would hide all kana from
    -- the first visible row.
    local learned_text = nil
    local ranked = chinese_learning.ranked(typed)
    if ranked[1] then learned_text = ranked[1].text end
    local head_index = 1
    if learned_text then
      for index, cand in ipairs(single_initial_chinese) do
        if cand.text == learned_text then
          head_index = index
          break
        end
      end
    end
    local head = table.remove(single_initial_chinese, head_index)
    normal_buckets[#normal_buckets + 1] = { head }
    -- A lone letter is only an unfinished initial, not a completed gojuon
    -- spelling.  Keep the rest of the real Chinese initials together; exact
    -- kana promotion is handled separately only after inputs such as da/shi
    -- parse to one complete mora.
    normal_buckets[#normal_buckets + 1] = single_initial_chinese
    normal_buckets[#normal_buckets + 1] = learned_abbreviation
    normal_buckets[#normal_buckets + 1] = chinese_exact
    normal_buckets[#normal_buckets + 1] = invalid_input_chinese
    normal_buckets[#normal_buckets + 1] = chinese_completion
    normal_buckets[#normal_buckets + 1] = japanese_exact
    normal_buckets[#normal_buckets + 1] = japanese_fuzzy
    normal_buckets[#normal_buckets + 1] = japanese_core_association
    normal_buckets[#normal_buckets + 1] = japanese_association
    normal_buckets[#normal_buckets + 1] = chinese_fallback
    normal_buckets[#normal_buckets + 1] = other
  else
    -- When an exact conversion is merely a mechanical kanji+kana assembly
    -- and a corrected whole-word result exists, keep only its strongest form.
    -- Repeating every homophonous first kanji with the same kana tail creates
    -- rows such as 負どこ/不どこ/府どこ and hides the useful fuzzy word.
    local collapse_mechanical = #japanese_exact > 1 and
      japanese_exact[1].type ~= "mozc_v2_dictionary_exact" and
      has_han_and_kana(japanese_exact[1].text) and
      (#japanese_fuzzy > 0 or (#typed >= 8 and #japanese_exact >= 4))
    if collapse_mechanical then
      local seen_heads = {}
      local best_text = japanese_exact[1].text
      local minimum_suffix = #typed <= 8 and 2 or 3
      local retained_exact = { japanese_exact[1] }
      for index = 2, #japanese_exact do
        local cand = japanese_exact[index]
        if cand.type == "mozc_v2_dictionary_exact" then
          -- Both 事がある and ことがある are whole entries, not speculative
          -- first-kanji replacements.  Never collapse verified spellings.
          retained_exact[#retained_exact + 1] = cand
        else
          local head, common_suffix = reduced_prefix_against(
            best_text, cand.text, minimum_suffix)
          if head and
             (head:sub(-#"わ") == "わ" or head:sub(-#"は") == "は") then
            head = head:sub(1, -#"わ" - 1)
          end
          local prefix_length = head and partial_prefix_length(
            typed, head, common_suffix, cand.text) or nil
          if prefix_length and head and head ~= "" and not seen_heads[head] then
            seen_heads[head] = true
            local reduced = Candidate("mozc_v2_head", cand.start,
                                      cand.start + prefix_length,
                                      head, LANGUAGE_JA)
            reduced.quality = tonumber(cand.quality) or 0
            japanese_prefix_heads[#japanese_prefix_heads + 1] = reduced
            if #japanese_prefix_heads >= 8 then break end
          end
        end
      end
      japanese_exact = retained_exact
    end
    local verified_japanese_exact, speculative_japanese_exact = {}, {}
    for _, cand in ipairs(japanese_exact) do
      local bucket = cand.type == "mozc_v2_dictionary_exact" and
                     verified_japanese_exact or speculative_japanese_exact
      bucket[#bucket + 1] = cand
    end
    -- Once the lexicon proves the full spelling, show those forms first.
    -- With fuzzy enabled, its corrected words should then precede Mozc's
    -- unverified same-code kanji assemblies (shajou -> 社長 is one example).
    -- Keep the first three complete results (including verified dictionary
    -- forms and katakana) in place, then expose first-character choices before
    -- the long tail of alternative whole-word spellings.  This lets the user
    -- continue selecting 李→俊→才 inside one composition.
    local japanese_exact_buckets
    if #japanese_character_heads > 0 then
      local japanese_front, japanese_tail = {}, {}
      if has_japanese_segment_head then
        local normal, phonetic = {}, {}
        for _, cand in ipairs(speculative_japanese_exact) do
          local bucket = cand.type == "mozc_v2_katakana" and phonetic or normal
          bucket[#bucket + 1] = cand
        end
        speculative_japanese_exact = normal
        for _, cand in ipairs(phonetic) do
          speculative_japanese_exact[#speculative_japanese_exact + 1] = cand
        end
      end
      for _, group in ipairs({verified_japanese_exact,
                               speculative_japanese_exact}) do
        for _, cand in ipairs(group) do
          local bucket = #japanese_front < 3 and japanese_front or japanese_tail
          bucket[#bucket + 1] = cand
        end
      end
      japanese_exact_buckets = {
        japanese_front, japanese_character_heads, japanese_fuzzy, japanese_tail,
      }
    else
      japanese_exact_buckets = #verified_japanese_exact > 0 and
        { verified_japanese_exact, japanese_fuzzy,
          speculative_japanese_exact } or
        { speculative_japanese_exact, japanese_fuzzy }
    end
    for _, bucket in ipairs({
      single_initial_chinese, learned_abbreviation, chinese_exact_single,
      chinese_exact,
      preferred_japanese, chinese_prefix_before_invalid,
      invalid_input_chinese,
    }) do
      normal_buckets[#normal_buckets + 1] = bucket
    end
    for _, bucket in ipairs(japanese_exact_buckets) do
      normal_buckets[#normal_buckets + 1] = bucket
    end
    normal_buckets[#normal_buckets + 1] = japanese_prefix_heads
    if japanese_association_first then
      for _, bucket in ipairs({
        japanese_core_association, japanese_association,
        chinese_completion, chinese_fallback,
      }) do normal_buckets[#normal_buckets + 1] = bucket end
    else
      local chinese_head, completion_tail, fallback_tail = {}, {}, {}
      if #chinese_completion > 0 then
        chinese_head[1] = chinese_completion[1]
        for index = 2, #chinese_completion do
          completion_tail[#completion_tail + 1] = chinese_completion[index]
        end
        fallback_tail = chinese_fallback
      elseif #chinese_fallback > 0 then
        chinese_head[1] = chinese_fallback[1]
        for index = 2, #chinese_fallback do
          fallback_tail[#fallback_tail + 1] = chinese_fallback[index]
        end
      end
      -- Chinese-first means the best Chinese association owns slot 1; it
      -- does not mean hiding Japanese association behind dozens of Chinese
      -- fallbacks.  Keep both languages on the first visible row.
      for _, bucket in ipairs({
        chinese_head,
        japanese_core_association, japanese_association,
        completion_tail, fallback_tail,
      }) do normal_buckets[#normal_buckets + 1] = bucket end
    end
    normal_buckets[#normal_buckets + 1] = other
  end
  inherit_learned_translation_comments(learned_abbreviation, normal_buckets)
  local output = {}
  if context:get_option("emoji_second_position") then
    local normal = {}
    for _, bucket in ipairs(normal_buckets) do
      for _, cand in ipairs(bucket) do normal[#normal + 1] = cand end
    end
    normal = apply_chinese_prefix_promotion(
        normal, promote_productive_chinese_prefix, association_disabled,
        japanese_association_first)
    normal = promote_learned_initial_in_syllable(normal, typed)
    normal = promote_wa_particle(normal, typed)
    normal = promote_exact_gojuon(normal, typed)
    normal = chinese_learning.promote(normal, active and active.start or 0,
                                      active and active._end or #active_input)
    -- The strongest real text is always slot 1.  Only an emoji explicitly
    -- selected by the user may occupy slot 2.  OpenCC-generated emoji are
    -- ordinary suggestions and must stay behind every textual candidate.
    if #normal > 0 then output[#output + 1] = normal[1] end
    if learned_emoji[1] then output[#output + 1] = learned_emoji[1] end
    for index = 2, #normal do output[#output + 1] = normal[index] end
    for index = 2, #learned_emoji do output[#output + 1] = learned_emoji[index] end
    if generated_emoji[1] then output[#output + 1] = generated_emoji[1] end
  else
    local normal = {}
    local buckets = { learned_emoji }
    for _, bucket in ipairs(normal_buckets) do
      buckets[#buckets + 1] = bucket
    end
    buckets[#buckets + 1] = generated_emoji
    for _, bucket in ipairs(buckets) do
      for _, cand in ipairs(bucket) do normal[#normal + 1] = cand end
    end
    normal = apply_chinese_prefix_promotion(
        normal, promote_productive_chinese_prefix, association_disabled,
        japanese_association_first)
    normal = promote_learned_initial_in_syllable(normal, typed)
    normal = promote_wa_particle(normal, typed)
    normal = promote_exact_gojuon(normal, typed)
    normal = chinese_learning.promote(normal, active and active.start or 0,
                                      active and active._end or #active_input)
    output = normal
  end
  if #output == 0 and typed ~= "" then
    local start_pos = active and active.start or 0
    local end_pos = active and active._end or #active_input
    output = nearest_nonempty_frame(typed, start_pos, end_pos)
  end
  -- Learned candidates and their dictionary originals can both survive the
  -- mixed translators (for example an explicitly learned single Han
  -- character).  They represent the same selectable result; keep the first,
  -- already-ranked copy instead of wasting a visible slot on a duplicate.
  local unique_output, seen_text = {}, {}
  for _, cand in ipairs(output) do
    if not seen_text[cand.text] then
      seen_text[cand.text] = true
      unique_output[#unique_output + 1] = cand
    end
  end
  output = unique_output
  remember_nonempty_frame(typed, output)
  for _, cand in ipairs(output) do yield(cand) end
end

return filter
