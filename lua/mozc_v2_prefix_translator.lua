-- High-priority, bounded prefix association for the isolated Mozc V2 scheme.
-- Kept separate from japanese_prefix_translator so the original 1.1 mixed
-- scheme retains its established ranking.
local M = {}
local PREFIX_MARKER = "[JP:PREFIX]"
local LANGUAGE_JA = "[[RIME_LANG:JA]]"
local READING_OPEN = "[[RIME_JR:"
local READING_CLOSE = "]]"
local routing = require("mozc_v2_translator")
local continuation_context = require("japanese_continuation_context")
local HONORIFICS = {
  san = "さん", sama = "様", kun = "君", chan = "ちゃん",
}

function M.init(env)
  env.memory = Memory(env.engine, Schema("japanese"))
end

function M.func(input, segment, env)
  if segment:has_tag("japanese_input_table") then return end
  local context = env.engine.context
  local compact = (input or ""):lower():gsub("[%s']+", "")
  local selected = continuation_context.selected_japanese_prefix(context)
  if selected and #selected.active_code <= 12 then
    local active = selected.active_code
    local seen = {}
    for split = #active, 1, -1 do
      local remainder = active:sub(split + 1)
      local honorific = remainder == "" and "" or HONORIFICS[remainder]
      if honorific then
        local full_code = selected.prefix_code .. active:sub(1, split)
        if env.memory:dict_lookup(full_code, false, 128) then
          for entry in env.memory:iter_dict() do
            local decoded = env.memory:decode(entry.code)
            local spelling = decoded and table.concat(decoded, "") or ""
            local surface = entry.text or ""
            if spelling == full_code and
               surface:sub(1, #selected.text) == selected.text and
               #surface > #selected.text then
              local text = surface:sub(#selected.text + 1) .. honorific
              if not seen[text] then
                seen[text] = true
                local reading = routing.romaji_to_hiragana(active)
                local comment = LANGUAGE_JA
                if reading ~= "" then
                  comment = comment .. READING_OPEN .. reading .. READING_CLOSE
                end
                local candidate = Candidate("mozc_v2_context_continuation",
                  segment.start, segment._end, text, comment)
                candidate.preedit = input
                candidate.quality = 298.5
                yield(candidate)
              end
            end
          end
        end
      end
    end
  end
  if #compact < 3 then return end

  -- Completing the missing final vowel in a standard polite ending is a
  -- deterministic exact conversion, not broad Japanese association.  Keep
  -- these four bounded cases active even when association is disabled.
  local polite_query = nil
  for _, ending in ipairs({
    { "desuk", "desuka" }, { "masuk", "masuka" },
    { "des", "desu" }, { "mas", "masu" },
  }) do
    if compact:sub(-#ending[1]) == ending[1] then
      polite_query = compact:sub(1, #compact - #ending[1]) .. ending[2]
      break
    end
  end
  if polite_query then
    local seen = {}
    for index, text in ipairs(routing.query_clean(polite_query) or {}) do
      if not seen[text] then
        seen[text] = true
        local reading = routing.romaji_to_hiragana(polite_query)
        local comment = PREFIX_MARKER .. LANGUAGE_JA
        if reading ~= "" then
          comment = comment .. READING_OPEN .. reading .. READING_CLOSE
        end
        local candidate = Candidate("mozc_v2_polite_completion", segment.start,
          segment._end, text, comment)
        candidate.quality = 2100000 - index
        yield(candidate)
        if index >= 8 then break end
      end
    end
    return
  end

  if context:get_option("japanese_prefix_completion_disabled") then return end
  if not compact:match("[bcdfghjklmpqrstvwxyz]$") or compact:sub(-1) == "n" then
    return
  end

  local seen, values = {}, {}
  if env.memory:dict_lookup(compact, true, 96) then
    for entry in env.memory:iter_dict() do
      local decoded = env.memory:decode(entry.code)
      local spelling = decoded and table.concat(decoded, "") or ""
      if spelling ~= compact and spelling:sub(1, #compact) == compact and
         not seen[entry.text] then
        seen[entry.text] = true
        values[#values + 1] = {
          text = entry.text,
          spelling = spelling,
          suffix = spelling:sub(#compact + 1),
        }
        if #values >= 8 then break end
      end
    end
  end


  -- The legacy Japanese table uses spellings such as `itpan`, while ordinary
  -- Mozc/Hepburn input uses `ippan`.  For an unfinished doubled consonant
  -- (`ipp`) the bridge can only return `いっp`, which is correctly discarded
  -- as Latin residue.  Probe the five possible following vowels and merge the
  -- clean Mozc results.  Restrict this fallback to a doubled final consonant
  -- so Chinese abbreviations such as `wjdmwt` are not expanded as Japanese.
  local final = compact:sub(-1)
  local has_doubled_final = compact:sub(-2, -2) == final and
                            final:match("[bcdfghjklmpqrstvwxyz]") ~= nil
  if #values < 2 and has_doubled_final then
    for _, vowel in ipairs({"a", "i", "u", "e", "o"}) do
      local spelling = compact .. vowel
      local candidates = routing.query_clean(spelling)
      for _, text in ipairs(candidates or {}) do
        if not seen[text] then
          seen[text] = true
          values[#values + 1] = {
            text = text,
            spelling = spelling,
            suffix = vowel,
          }
          if #values >= 8 then break end
        end
      end
      if #values >= 8 then break end
    end
  end

  for index, item in ipairs(values) do
    local suffix = ""
    if context:get_option("japanese_prefix_completion_suffix") then
      suffix = "~" .. item.suffix:gsub("%-", "q")
    end
    local reading = routing.romaji_to_hiragana(item.spelling)
    local comment = PREFIX_MARKER .. suffix .. LANGUAGE_JA
    if reading ~= "" then
      comment = comment .. READING_OPEN .. reading .. READING_CLOSE
    end
    local candidate = Candidate("mozc_v2_prefix", segment.start,
      segment._end, item.text, comment)
    candidate.quality = 2000000 - index
    yield(candidate)
  end
end

return M
