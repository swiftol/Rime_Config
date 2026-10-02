-- Bounded Japanese romanization prefix completion.
-- Rime's normal script completion only works at valid syllable boundaries;
-- an unfinished code such as "sud" therefore produced no Japanese results.
local M = {}
local PREFIX_MARKER = "[JP:PREFIX]"
local continuation_context = require("japanese_continuation_context")
local routing = require("mozc_v2_translator")
local HONORIFICS = {
  san = "さん", sama = "様", kun = "君", chan = "ちゃん",
}

function M.init(env)
  env.memory = Memory(env.engine, Schema("japanese"))
end

function M.func(input, segment, env)
  local context = env.engine.context
  local selected = continuation_context.selected_japanese_prefix(context)
  if selected and #selected.active_code <= 12 then
    local active = selected.active_code
    local seen = {}
    -- A name may have a voiced second half that is not a standalone word:
    -- 櫻 (sakura) + da + san must offer 田さん, using the attested whole
    -- headword 櫻田 (sakurada), not a fabricated standalone 田=da entry.
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
                local comment = "[[RIME_LANG:JA]]"
                if reading ~= "" then
                  comment = comment .. "[[RIME_JR:" .. reading .. "]]"
                end
                local candidate = Candidate("japanese_context_exact",
                  segment.start, segment._end, text, comment)
                candidate.preedit = input
                candidate.quality = 260
                yield(candidate)
              end
            end
          end
        end
      end
    end
  end
  -- The negative option keeps prefix completion enabled for existing users
  -- whose user.yaml predates this switch, while still allowing an explicit
  -- opt-out in the settings panel.
  if context:get_option("japanese_prefix_completion_disabled") then return end

  local compact = (input or ""):lower():gsub("[%s']+", "")
  if #compact < 3 then return end
  -- The mixed schema must not turn complete Chinese pinyin such as `nihao`
  -- into a Japanese prediction.  This feature targets an unfinished Japanese
  -- tail (`sappor` -> `sapporo`), so only a trailing consonant other than n
  -- activates it.  This is prefix completion, not an alias for small-tsu.
  if not compact:match("[bcdfghjklmpqrstvwxyz]$") or compact:sub(-1) == "n" then
    return
  end

  -- Prefix association must never jump ahead of a complete Japanese spelling.
  -- If the dictionary already contains the exact code, let the normal Japanese
  -- translator rank that word and do not emit longer continuations here.
  if env.memory:dict_lookup(compact, false, 8) then
    for entry in env.memory:iter_dict() do
      local decoded = env.memory:decode(entry.code)
      local spelling = decoded and table.concat(decoded, "") or ""
      if spelling == compact then return end
    end
  end

  local seen, emitted = {}, 0
  -- Search farther than the visible result limit.  Large imported Japanese
  -- dictionaries can have many lower-value spellings before a common headword.
  if env.memory:dict_lookup(compact, true, 96) then
    for entry in env.memory:iter_dict() do
      local decoded = env.memory:decode(entry.code)
      local spelling = decoded and table.concat(decoded, "") or ""
      if spelling ~= compact and spelling:sub(1, #compact) == compact and
         not seen[entry.text] then
        seen[entry.text] = true
        local suffix = ""
        if context:get_option("japanese_prefix_completion_suffix") then
          suffix = "~" .. spelling:sub(#compact + 1):gsub("%-", "q")
        end
        -- The ranking filter removes this internal marker and uses it to keep
        -- kanji-only Japanese completions out of the Chinese completion bucket.
        yield(Candidate("completion", segment.start, segment._end,
          entry.text, PREFIX_MARKER .. suffix))
        emitted = emitted + 1
        if emitted >= 8 then return end
      end
    end
  end
end

return M
