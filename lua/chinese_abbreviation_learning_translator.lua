local learning = require("chinese_abbreviation_learning")
local routing = require("mozc_v2_translator")
local q_completion = require("chinese_q_prefix_completion")
local MARKER = "[[RIME_LANG:ZH]]"

local function translator(input, seg, env)
  local normalized = (input or ""):lower():gsub("[%s']+", "")
  local seen = {}
  -- One-letter learning is applied inside the final mixed-order filter by
  -- moving an existing Chinese candidate.  Emitting a separate billion-
  -- quality stream here collapses the compact native menu to one item.
  if #normalized > 1 then
    for index, item in ipairs(learning.ranked(normalized)) do
      seen[item.text] = true
      local candidate = Candidate("chinese_abbreviation_learning", seg.start,
                                  seg._end, item.text, MARKER)
      candidate.quality = 1000000000 + item.score - index / 1000
      yield(candidate)
    end
  end

  -- Supply bounded Chinese word completion for a partially typed next
  -- syllable (`qun` is also `qu'n...` -> 去年/去哪/取暖...).  Rime normally
  -- commits to the complete syllable `qun` and never explores that second
  -- segmentation, so query it explicitly through the real Chinese script
  -- translator.  This is productive parsing, not hard-coded vocabulary.
  if #normalized < 3 or not normalized:match("^[a-z]+$") then return end
  local alternates = {}
  for _, suffix_length in ipairs({ 2, 1 }) do
    if #normalized > suffix_length then
      local prefix = normalized:sub(1, -suffix_length - 1)
      local suffix = normalized:sub(-suffix_length)
      if routing.is_complete_pinyin(prefix) and
         (suffix_length == 1 or suffix == "zh" or suffix == "ch" or
          suffix == "sh") then
        if suffix == "q" then
          -- A bare q is not a legal pinyin syllable, so librime returns no
          -- translation for yi'q even though it is the natural unfinished
          -- prefix of yi'qi / yi'qian / yi'qu.  Query every real q syllable
          -- and merge them by dictionary quality.  This is general syllable
          -- completion; no output word is hard-coded.
          for _, syllable in ipairs({
            "qi", "qia", "qian", "qiang", "qiao", "qie", "qin",
            "qing", "qiong", "qiu", "qu", "quan", "que", "qun",
          }) do
            alternates[#alternates + 1] = prefix .. "'" .. syllable
          end
        else
          -- Complete an unfinished Mandarin initial through the real Chinese
          -- translator.  This keeps productive Chinese prefixes visible for
          -- inputs such as biao'j... and biao'jia'm..., instead of letting a
          -- permissive Japanese completion monopolize the intermediate row.
          for _, syllable in ipairs(
              routing.pinyin_syllables_starting_with(suffix)) do
            alternates[#alternates + 1] = prefix .. "'" .. syllable
          end
        end
        break
      end
    end
  end
  if #alternates == 0 then return end
  if not env.chinese_prefix_translator then
    env.chinese_prefix_translator = Component.Translator(
      env.engine, "", "script_translator@chinese_exact_translator")
  end
  local collected = {}
  local sequence = 0
  local allow_single_fallback = #normalized == 3 and
      routing.is_valid_japanese_input(normalized)
  local best_single = nil
  if normalized:sub(-1) == "q" then
    local q_entries = q_completion.lookup(normalized)
    for _, entry in ipairs(q_entries) do
      if not seen[entry.text] then
        seen[entry.text] = true
        sequence = sequence + 1
        collected[#collected + 1] = {
          text = entry.text,
          quality = entry.weight,
          sequence = sequence,
        }
      end
    end
    -- The generated q index already merges every possible following vowel.
    alternates = {}
  end
  for _, alternate in ipairs(alternates) do
    local translation = env.chinese_prefix_translator:query(alternate, seg)
    if translation then
      local per_query = 0
      for entry in translation:iter() do
        local length_ok, text_length = pcall(utf8.len, entry.text or "")
        -- A next-syllable completion must actually extend the first syllable.
        -- librime also returns fallback single characters for every alternate
        -- query; for `yix` those filled the ten-item cap with 一/以/已/... and
        -- hid the real 一下/一些 phrases.  Keep only multi-character results
        -- in this dedicated word-completion stream.  Ordinary single-character
        -- candidates remain available from the normal Chinese translator.
        if length_ok and text_length == 1 and allow_single_fallback then
          local quality = tonumber(entry.quality) or 0
          if not best_single or quality > best_single.quality then
            best_single = { text = entry.text, quality = quality }
          end
        elseif length_ok and text_length and text_length > 1 and
               not seen[entry.text] then
          seen[entry.text] = true
          sequence = sequence + 1
          collected[#collected + 1] = {
            text = entry.text,
            quality = tonumber(entry.quality) or 0,
            sequence = sequence,
          }
          per_query = per_query + 1
          if per_query >= 10 then break end
        end
      end
    end
    if #collected >= 60 then break end
  end
  -- Preserve one strongest Chinese fallback for genuinely valid unfinished
  -- Japanese three-key prefixes (`kas` keeps 卡 first when Japanese
  -- association is off), but never let a row of single characters crowd out
  -- the phrase results.  Invalid Japanese `yix` takes the phrase-only path.
  if best_single and not seen[best_single.text] then
    sequence = sequence + 1
    best_single.sequence = sequence
    collected[#collected + 1] = best_single
  end
  table.sort(collected, function(a, b)
    if a.quality ~= b.quality then return a.quality > b.quality end
    return a.sequence < b.sequence
  end)
  local emitted = 0
  for _, entry in ipairs(collected) do
    emitted = emitted + 1
    local candidate = Candidate("chinese_prefix_completion", seg.start,
                                seg._end, entry.text, MARKER)
    candidate.quality = 298 - emitted / 1000
    yield(candidate)
    if emitted >= 10 then break end
  end
end

return translator
