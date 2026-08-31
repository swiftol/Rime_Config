-- Query the large fuzzy Japanese prism only while the master switch is on.
-- A permanently attached script_translator queried it on every key and
-- Backspace even when all fuzzy candidates were later discarded.
local M = {}

local function load_custom_rules()
  local rules = {}
  local file = io.open(rime_api.get_user_data_dir() .. "/custom_japanese_fuzzy.tsv", "r")
  if not file then return rules end
  for line in file:lines() do
    local enabled, left, right = line:match("^(%d)\t([a-z]+)\t([a-z]+)$")
    if enabled == "1" and left ~= right and #left <= 16 and #right <= 16 then
      rules[#rules + 1] = { left, right }
    end
  end
  file:close()
  return rules
end

function M.init(env)
  env.memory = Memory(env.engine, Schema("japanese_fuzzy"))
  -- Query the fuzzy prism through a real script translator.  Memory lookup
  -- reads dictionary codes directly and does not apply the prism algebra, so
  -- it cannot by itself resolve sasuka -> sasuga -> さすが.
  env.prism_translator = Component.Translator(
    env.engine, "", "script_translator@japanese_fuzzy_translator")
  -- Corrected spellings must pass through the same script translator used by
  -- normal exact Japanese input. Memory:dict_lookup() does not apply that
  -- translator's prism and can return false even when typing the same spelling
  -- directly produces a candidate (for example namaeha -> 名前は).
  env.exact_translator = Component.Translator(
    env.engine, "", "script_translator@japanese_translator")
  -- Corrected spellings (for example kegi -> kaigi) must first query the
  -- compact exact prism. Looking only in the huge fuzzy prism can bury the
  -- desired whole word beyond its bounded result window.
  env.exact_memory = Memory(env.engine, Schema("japanese"))
  env.custom_rules = load_custom_rules()
end

function M.func(input, segment, env)
  local context = env.engine.context
  local fuzzy_master = context:get_option("japanese_fuzzy_match")
  -- Enabled custom rules are independent user choices.  The built-in master
  -- switch controls only the built-in fuzzy families below.
  if not fuzzy_master and #(env.custom_rules or {}) == 0 then return end
  local compact = (input or ""):lower():gsub("[%s']+", "")
  -- One/two-letter prefixes have an enormous fan-out and exact translators
  -- already cover them.  Starting fuzzy lookup here made held Backspace queue.
  if #compact < 3 then return end

  -- The ranking filter only needs the head of the fuzzy result set.  Keeping
  -- this bounded prevents one-letter input and Backspace from expanding the
  -- entire Japanese dictionary.
  local queries, query_seen, custom_query = {}, {}, {}
  -- Once a correctly typed polite ending is present, fuzzy rules may repair
  -- the lexical stem but must never rewrite the grammar itself.  Rewriting
  -- desuka produced nonsense such as tesuka / dezuka / desuga and made those
  -- candidates look like corrections of the whole sentence.
  local protected_suffix = nil
  for _, suffix in ipairs({ "desuka", "masuka", "desu", "masu" }) do
    if compact:sub(-#suffix) == suffix then
      protected_suffix = suffix
      break
    end
  end
  local function add_query(query, is_custom)
    if protected_suffix and query:sub(-#protected_suffix) ~= protected_suffix then
      return
    end
    if query ~= "" and not query_seen[query] then
      query_seen[query] = true
      queries[#queries + 1] = query
    end
    if is_custom and query ~= "" then custom_query[query] = true end
  end
  add_query(compact)
  -- User-defined rules are literal, bidirectional replacements and are only
  -- applied inside this Japanese translator.  They never enter the global
  -- speller algebra, so a Japanese rule cannot create Chinese candidates.
  local function custom_replace_each(from, to)
    local start = 1
    while true do
      local first, last = compact:find(from, start, true)
      if not first then break end
      add_query(compact:sub(1, first - 1) .. to .. compact:sub(last + 1), true)
      start = first + 1
    end
  end
  for _, rule in ipairs(env.custom_rules or {}) do
    custom_replace_each(rule[1], rule[2])
    custom_replace_each(rule[2], rule[1])
  end
  if fuzzy_master and context:get_option("japanese_fuzzy_ke_kai") then
    -- Restore each mistyped `ke` to `kai` independently (seke -> sekai).
    local start = 1
    while true do
      local first, last = compact:find("ke", start, true)
      if not first then break end
      add_query(compact:sub(1, first - 1) .. "kai" .. compact:sub(last + 1))
      start = first + 1
    end
  end
  if fuzzy_master and context:get_option("japanese_fuzzy_ke_kae_gae") then
    -- Restore each contracted `ke` independently.  Query both the unvoiced
    -- and voiced spellings so kikeru can find kigaeru / 着替える.
    local start = 1
    while true do
      local first, last = compact:find("ke", start, true)
      if not first then break end
      add_query(compact:sub(1, first - 1) .. "kae" .. compact:sub(last + 1))
      add_query(compact:sub(1, first - 1) .. "gae" .. compact:sub(last + 1))
      start = first + 1
    end
  end
  if fuzzy_master and context:get_option("japanese_fuzzy_sei_sai") then
    local function swap_each(from, to)
      local start = 1
      while true do
        local first, last = compact:find(from, start, true)
        if not first then break end
        add_query(compact:sub(1, first - 1) .. to .. compact:sub(last + 1))
        start = first + 1
      end
    end
    swap_each("sei", "sai")
    swap_each("sai", "sei")
  end
  if fuzzy_master and context:get_option("japanese_fuzzy_long_u") then
    -- Restore one independently omitted long -u and query that exact spelling.
    -- The fuzzy prism's generic `ou -> o` lookup can bury a low-frequency
    -- whole word beyond the bounded result window (daitouryo -> daitouryou).
    -- Exact restored queries keep this general without hard-coding words.
    local function add_u_after_each(vowel)
      local start = 1
      while true do
        local position = compact:find(vowel, start, true)
        if not position then break end
        local should_add = true
        if vowel == "o" and compact:sub(position + 1, position + 1) == "u" then
          -- The spelling already contains the long `ou`; only a later
          -- actually omitted `u` may be restored (daitouryo -> daitouryou).
          should_add = false
        end
        if vowel == "u" then
          local following = compact:sub(position + 1)
          local next_letter = following:sub(1, 1)
          local previous_letter = compact:sub(position - 1, position - 1)
          -- `u` before another vowel is part of the next mora, not an
          -- omitted long-u spelling: warui must never become waruui.
          -- A `u` already preceded by a vowel is itself the existing long
          -- vowel marker (tou); it must not become touu either.
          if next_letter:match("[aeiou]") or
             previous_letter:match("[aeiou]") then
            should_add = false
          end
          -- Polite endings do not acquire a long vowel before a particle:
          -- desu-ka / masu-ka must not become desuu-ka / masuu-ka.
          local before = compact:sub(1, position)
          if before:sub(-4) == "desu" or before:sub(-4) == "masu" then
            local particle_after = following == ""
            for _, particle in ipairs({
              "ka", "ga", "ne", "yo", "wa", "no", "to", "mo",
              "de", "ni", "he", "o"
            }) do
              if following:sub(1, #particle) == particle then
                particle_after = true
                break
              end
            end
            if particle_after then should_add = false end
          end
        end
        if should_add then
          add_query(compact:sub(1, position) .. "u" .. compact:sub(position + 1))
        end
        start = position + 1
      end
    end
    add_u_after_each("o")
    add_u_after_each("u")
  end
  if fuzzy_master and context:get_option("japanese_fuzzy_dakuten") then
    -- Query corrected consonants on demand instead of deriving them into the
    -- 1.8M-entry prism.  The prism's existing small-tsu rule then handles
    -- shubatsu -> shupatsu -> shuppatsu and rohyaku -> ropyaku -> roppyaku.
    if compact:find("b", 1, true) then add_query((compact:gsub("b", "p"))) end
    if compact:find("h", 1, true) then add_query((compact:gsub("h", "p"))) end
    if compact:find("d", 1, true) then add_query((compact:gsub("d", "t"))) end
    local function add_single_replacements(from, to)
      local start = 1
      while true do
        local first, last = compact:find(from, start, true)
        if not first then break end
        add_query(compact:sub(1, first - 1) .. to .. compact:sub(last + 1))
        start = first + 1
      end
    end
    add_single_replacements("g", "k")
    add_single_replacements("k", "g")
    add_single_replacements("z", "s")
    add_single_replacements("s", "z")
    if compact:find("p", 1, true) then
      add_query((compact:gsub("p", "b")))
      add_query((compact:gsub("p", "h")))
    end
    if compact:find("t", 1, true) then add_query((compact:gsub("t", "d"))) end
  end
  local seen = {}
  -- Compose a complete polite question from an exact Japanese stem before
  -- querying the broad fuzzy prism.  The normal Japanese sentence candidate
  -- can arrive extremely late (waruidesuka -> 悪いですか), after hundreds
  -- of Chinese/prefix candidates, so a bounded UI filter never sees it.
  -- This is grammar composition, not a word-specific binding: any exact stem
  -- may receive the correctly typed desuka/masuka ending.
  if protected_suffix == "desuka" or protected_suffix == "masuka" then
    local stem = compact:sub(1, #compact - #protected_suffix)
    local kana_suffix = protected_suffix == "desuka" and "ですか" or "ますか"
    if stem ~= "" and env.exact_translator then
      local translation = env.exact_translator:query(stem, segment)
      local composed_count = 0
      local composed_han = false
      local composed_hiragana = false
      if translation then
        for candidate in translation:iter() do
          local candidate_preedit = (candidate.preedit or ""):lower():gsub("[%s']+", "")
          local candidate_text = candidate.text or ""
          local has_han = false
          local has_katakana = false
          for _, cp in utf8.codes(candidate_text) do
            if (cp >= 0x3400 and cp <= 0x9fff) or
               (cp >= 0xf900 and cp <= 0xfaff) then
              has_han = true
            end
            if cp >= 0x30a0 and cp <= 0x30ff then has_katakana = true break end
          end
          local wanted_form = (has_han and not composed_han) or
                              (not has_han and not has_katakana and not composed_hiragana)
          if candidate_preedit == stem and wanted_form then
            local text = (candidate.text or "") .. kana_suffix
            local key = text .. "\0" .. compact
            if not seen[key] then
              seen[key] = true
              local composed = Candidate(
                "japanese_polite_question", segment.start, segment._end,
                text, "[[RIME_LANG:JA]]"
              )
              composed.preedit = compact
              composed.quality = 200
              yield(composed)
              composed_count = composed_count + 1
              if has_han then composed_han = true else composed_hiragana = true end
              if composed_count >= 2 then break end
            end
          end
        end
      end
    end
  end
  -- Query corrected spellings through the real exact Japanese translator.
  -- The original spelling is already handled earlier in the schema pipeline;
  -- querying only changed spellings avoids duplicate exact candidates.
  if env.exact_translator then
    for _, query in ipairs(queries) do
      if query ~= compact then
        local translation = env.exact_translator:query(query, segment)
        if translation then
          for candidate in translation:iter() do
            local candidate_preedit = (candidate.preedit or ""):lower():gsub("[%s']+", "")
            -- A corrected whole-input query also exposes its internal prefix
            -- candidates.  Never label a short prefix such as `warui` with
            -- the full correction `waruidesuka`; fuzzy candidates must cover
            -- the complete corrected spelling.
            if candidate_preedit == query then
              local key = candidate.text .. "\0" .. query
              if not seen[key] then
                seen[key] = true
                local marked = ShadowCandidate(
                  candidate, candidate.type, candidate.text,
                  (custom_query[query] and "[JF:CUSTOM]" or "[JF:COMBINED]") .. query ..
                  "[[JF_TYPED:" .. compact .. "]]"
                )
                marked.quality = 199
                yield(marked)
              end
            end
          end
        end
      end
    end
  end
  -- Finish all exact lookups (typed spelling, then corrected spellings)
  -- before entering the broad fuzzy prism.  Otherwise generic kegi fuzzy
  -- results can precede the exact corrected kaigi / 会議 result.
  for _, memory in ipairs({ env.exact_memory, env.memory }) do
    for _, query in ipairs(queries) do
      local found = memory:dict_lookup(query, false, 48)
      if found then
        for entry in memory:iter_dict() do
          local decoded = memory:decode(entry.code)
          local spelling = decoded and table.concat(decoded, "") or query
          local key = entry.text .. "\0" .. spelling
          if not seen[key] then
            seen[key] = true
            local phrase = Phrase(memory, "japanese_fuzzy", segment.start, segment._end, entry)
            -- Preserve the active segment's original spelling.  context.input
            -- may also contain an already selected prefix (for example
            -- イオン + kontororu), so the downstream filter must not compare
            -- this candidate against the whole composition.
            phrase.comment = (custom_query[query] and "[JF:CUSTOM]" or "[JF:COMBINED]") .. spelling ..
                             "[[JF_TYPED:" .. compact .. "]]"
            local candidate = phrase:toCandidate()
            -- Cross-language priority contract:
            -- Chinese exact (300) > Japanese exact (200) > fuzzy (199) >
            -- assembled candidates.  Lua translators do not inherit the
            -- schema translator's initial_quality automatically.
            -- Keep corrected whole-word candidates inside the filter's
            -- bounded head window.  At 100, a valid correction such as
            -- sasuka -> sasuga -> さすが could be buried behind hundreds of
            -- prefix candidates before the ranking filter ever saw it.
            candidate.quality = 199
            yield(candidate)
          end
        end
      end
    end
  end
  -- Corrected exact spellings must precede the broad fuzzy prism; otherwise
  -- generic prefix candidates can fill the bounded ranking window first.
  if fuzzy_master and env.prism_translator then
    local translation = env.prism_translator:query(compact, segment)
    if translation then
      for candidate in translation:iter() do
        local marked = ShadowCandidate(
          candidate, candidate.type, candidate.text,
          (candidate.comment or "") .. "[[JF_TYPED:" .. compact .. "]]"
        )
        marked.quality = 199
        yield(marked)
      end
    end
  end
end

return M
