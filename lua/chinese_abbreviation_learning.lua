local M = {}
local routing = require("mozc_v2_translator")
local loaded = false
local scores = {}
local word_scores = {}
local word_path = rime_api.get_user_data_dir() .. "/chinese_word_frequency.tsv"
local path = rime_api.get_user_data_dir() .. "/chinese_abbreviation_learning.tsv"
local pair_path = rime_api.get_user_data_dir() .. "/chinese_pair_observations.tsv"
local pair_counts = {}
local reading_cache, chinese_reverse = {}, nil

local function normalize(code)
  return ((code or ""):lower():gsub("[%s']+", ""))
end

function M.load()
  if loaded then return end
  loaded = true
  local file = io.open(path, "r")
  if file then
    for line in file:lines() do
      line = line:gsub("\r$", "")
      local code, text = line:match("^([^\t]+)\t([^\t]+)$")
      code = normalize(code)
      -- Older builds incorrectly learned the last selected character under
      -- an entire multi-syllable code (yunzi -> 子).  Ignore those impossible
      -- one-character records without deleting the user's history file.
      local old_fragment = text and utf8.len(text) == 1 and #code > 1 and
          not routing.is_single_pinyin_syllable(code)
      if code ~= "" and text and text ~= "" and not old_fragment then
        scores[code] = scores[code] or {}
        scores[code][text] = (scores[code][text] or 0) + 1
      end
    end
    file:close()
  end
  -- Legacy commits wrote both the typed code and its initials. Do not sum
  -- aliases (which would double-count); conservatively seed from their max.
  for _, words in pairs(scores) do
    for text, score in pairs(words) do
      word_scores[text] = math.max(word_scores[text] or 0, score)
    end
  end
  local frequencies = io.open(word_path, "r")
  if frequencies then
    for line in frequencies:lines() do
      local text, value = line:gsub("\r$", ""):match("^([^\t]+)\t(%d+)$")
      if text then word_scores[text] = tonumber(value) end
    end
    frequencies:close()
  end
  local observations = io.open(pair_path, "r")
  if observations then
    for line in observations:lines() do
      local code, text = line:gsub("\r$", ""):match("^([^\t]+)\t([^\t]+)$")
      if code and text then
        local key = normalize(code) .. "\t" .. text
        pair_counts[key] = (pair_counts[key] or 0) + 1
      end
    end
    observations:close()
  end
end

function M.record_word(text)
  M.load()
  if not text or text == "" or text:find("[\t\r\n]") then return end
  word_scores[text] = (word_scores[text] or 0) + 1
  local file = io.open(word_path, "a")
  if file then
    file:write(text, "\t", tostring(word_scores[text]), "\n")
    file:close()
  end
end

function M.word_score(text)
  M.load()
  return word_scores[text] or 0
end

local function matches_reading(code, text)
  local readings = reading_cache[text]
  if readings == nil then
    if not chinese_reverse then
      local ok, reverse = pcall(ReverseLookup, "rime_ice")
      if ok then chinese_reverse = reverse end
    end
    local ok, reading = pcall(function() return chinese_reverse and chinese_reverse:lookup(text) end)
    readings = {}
    if ok and reading then
      for syllable in reading:gmatch("[a-z]+") do readings[#readings + 1] = { syllable } end
    end
    -- Rime's reverse database commonly stores characters, not phrase keys.
    -- Use only unambiguous character readings. Combining all readings of
    -- polyphonic characters would invent invalid whole-word pronunciations.
    if #readings ~= utf8.len(text) and chinese_reverse then
      readings = {}
      for _, cp in utf8.codes(text) do
        local alternatives, seen = {}, {}
        local success, value = pcall(function() return chinese_reverse:lookup(utf8.char(cp)) end)
        if success then
          for syllable in (value or ""):gmatch("[a-z]+") do
            if not seen[syllable] then
              seen[syllable] = true
              alternatives[#alternatives + 1] = syllable
            end
          end
        end
        if #alternatives ~= 1 then alternatives = {} end
        readings[#readings + 1] = alternatives
      end
    end
    reading_cache[text] = readings
  end
  -- Require a verified complete reading for the whole word. Never derive
  -- readings from an ambiguous abbreviation, or admit a partial-word match.
  if #readings ~= utf8.len(text) then return false end
  local positions = { [1] = true }
  for _, alternatives in ipairs(readings) do
    local next_positions = {}
    for position in pairs(positions) do
      for _, syllable in ipairs(alternatives) do
        for _, spelling in ipairs({ syllable, syllable:sub(1, 1) }) do
          if code:sub(position, position + #spelling - 1) == spelling then
            next_positions[position + #spelling] = true
          end
        end
      end
    end
    positions = next_positions
  end
  return positions[#code + 1] == true
end

function M.increment(code, text, alias_only)
  M.load()
  code = normalize(code)
  if code == "" or not text or text == "" then return end
  scores[code] = scores[code] or {}
  scores[code][text] = (scores[code][text] or 0) + 1
  if not alias_only then M.record_word(text) end
  local file = io.open(path, "a")
  if file then
    file:write(code, "\t", text, "\n")
    file:close()
  end
end

-- Two consecutive single-character selections are weaker evidence than a
-- whole composition.  Promote only after the same pair was selected twice;
-- observations survive a restart, while the actual preference still uses the
-- existing learned-candidate ranking for both full pinyin and initials.
function M.observe_pair(code, text, initials)
  M.load()
  code = normalize(code)
  if code == "" or not text or text == "" then return end
  local key = code .. "\t" .. text
  pair_counts[key] = (pair_counts[key] or 0) + 1
  local file = io.open(pair_path, "a")
  if file then
    file:write(key, "\n")
    file:close()
  end
  if pair_counts[key] >= 2 then
    M.increment(code, text)
    if initials and initials ~= code then M.increment(initials, text, true) end
  end
end

function M.ranked(code)
  M.load()
  code = normalize(code)
  local result, seen = {}, {}
  for text, score in pairs(scores[code] or {}) do
    seen[text] = true
    result[#result + 1] = { text = text, score = utf8.len(text) > 1 and M.word_score(text) or score }
  end
  for text, score in pairs(word_scores) do
    if score > 0 and not seen[text] and utf8.len(text) > 1 and matches_reading(code, text) then
      result[#result + 1] = { text = text, score = score }
    end
  end
  table.sort(result, function(a, b)
    if a.score ~= b.score then return a.score > b.score end
    return a.text < b.text
  end)
  return result
end

-- Re-rank only Chinese words already returned for THIS spelling. This shares
-- preference without guessing readings or injecting words under wrong codes.
function M.promote(candidates, start_pos, end_pos)
  local learned, remaining = {}, {}
  for index, candidate in ipairs(candidates) do
    local comment = candidate.comment or ""
    local score = M.word_score(candidate.text)
    if score > 0 and utf8.len(candidate.text) > 1 and
       candidate.start == start_pos and candidate._end == end_pos and
       comment:find("[[RIME_LANG:ZH]]", 1, true) and
       not comment:find("[[RIME_LANG:JA]]", 1, true) then
      learned[#learned + 1] = { candidate = candidate, score = score, index = index }
    else remaining[#remaining + 1] = candidate end
  end
  table.sort(learned, function(a, b)
    if a.score ~= b.score then return a.score > b.score end
    return a.index < b.index
  end)
  local result = {}
  for _, item in ipairs(learned) do result[#result + 1] = item.candidate end
  for _, candidate in ipairs(remaining) do result[#result + 1] = candidate end
  return result
end

return M
