-- Query the large fuzzy Japanese prism only while the master switch is on.
-- A permanently attached script_translator queried it on every key and
-- Backspace even when all fuzzy candidates were later discarded.
local M = {}

function M.init(env)
  env.memory = Memory(env.engine, Schema("japanese_fuzzy"))
  -- Corrected spellings (for example kegi -> kaigi) must first query the
  -- compact exact prism. Looking only in the huge fuzzy prism can bury the
  -- desired whole word beyond its bounded result window.
  env.exact_memory = Memory(env.engine, Schema("japanese"))
end

function M.func(input, segment, env)
  local context = env.engine.context
  if not context:get_option("japanese_fuzzy_match") then return end
  local compact = (input or ""):lower():gsub("[%s']+", "")
  -- One/two-letter prefixes have an enormous fan-out and exact translators
  -- already cover them.  Starting fuzzy lookup here made held Backspace queue.
  if #compact < 3 then return end

  -- The ranking filter only needs the head of the fuzzy result set.  Keeping
  -- this bounded prevents one-letter input and Backspace from expanding the
  -- entire Japanese dictionary.
  local queries, query_seen = {}, {}
  local function add_query(query)
    if query ~= "" and not query_seen[query] then
      query_seen[query] = true
      queries[#queries + 1] = query
    end
  end
  add_query(compact)
  if context:get_option("japanese_fuzzy_ke_kai") then
    -- Restore each mistyped `ke` to `kai` independently (seke -> sekai).
    local start = 1
    while true do
      local first, last = compact:find("ke", start, true)
      if not first then break end
      add_query(compact:sub(1, first - 1) .. "kai" .. compact:sub(last + 1))
      start = first + 1
    end
  end
  if context:get_option("japanese_fuzzy_ke_kae_gae") then
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
  if context:get_option("japanese_fuzzy_sei_sai") then
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
  if context:get_option("japanese_fuzzy_long_u") then
    -- Restore one independently omitted long -u and query that exact spelling.
    -- The fuzzy prism's generic `ou -> o` lookup can bury a low-frequency
    -- whole word beyond the bounded result window (daitouryo -> daitouryou).
    -- Exact restored queries keep this general without hard-coding words.
    local function add_u_after_each(vowel)
      local start = 1
      while true do
        local position = compact:find(vowel, start, true)
        if not position then break end
        add_query(compact:sub(1, position) .. "u" .. compact:sub(position + 1))
        start = position + 1
      end
    end
    add_u_after_each("o")
    add_u_after_each("u")
  end
  if context:get_option("japanese_fuzzy_dakuten") then
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
  -- Finish all exact lookups (typed spelling, then corrected spellings)
  -- before entering the broad fuzzy prism.  Otherwise generic kegi fuzzy
  -- results can precede the exact corrected kaigi / 会議 result.
  for _, memory in ipairs({ env.exact_memory, env.memory }) do
    for _, query in ipairs(queries) do
      if memory:dict_lookup(query, false, 48) then
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
            phrase.comment = "[JF:COMBINED]" .. spelling .. string.char(30) .. compact
            local candidate = phrase:toCandidate()
            -- Cross-language priority contract:
            -- Chinese exact (300) > Japanese exact (200) > fuzzy (100) >
            -- assembled candidates.  Lua translators do not inherit the
            -- schema translator's initial_quality automatically.
            candidate.quality = 100
            yield(candidate)
          end
        end
      end
    end
  end
end

return M
