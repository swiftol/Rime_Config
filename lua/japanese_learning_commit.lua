-- Capture reading before Rime clears the selected candidate. Formatting is
-- done at the native IPC boundary: dictionary learning keeps the plain word.
local M = {}

local function characters(text)
  local result = {}
  for _, cp in utf8.codes(text) do result[#result + 1] = utf8.char(cp) end
  return result
end
local function normalized(ch)
  local cp = utf8.codepoint(ch)
  return cp >= 0x30a1 and cp <= 0x30f6 and utf8.char(cp - 0x60) or ch
end
local function han(ch)
  local cp = utf8.codepoint(ch)
  return (cp >= 0x3400 and cp <= 0x9fff) or (cp >= 0x20000 and cp <= 0x323af) or
         (cp >= 0xf900 and cp <= 0xfaff) or cp == 0x3005 or cp == 0x3006
end

-- Kana in the written surface are alignment anchors. Only accept a unique
-- alignment: repeated anchors can otherwise assign a plausible but wrong
-- pronunciation to a Han group. Never split a compound into guessed readings.
function M.format_kanji(surface, reading)
  local fallback = surface .. "（" .. reading .. "）"
  local chars, kana, groups = characters(surface), characters(reading), {}
  if #chars > 128 or #kana > 256 then return fallback end
  for _, ch in ipairs(chars) do
    if han(ch) and #groups > 0 and groups[#groups].han then
      groups[#groups].text = groups[#groups].text .. ch
    else groups[#groups + 1] = {text = ch, han = han(ch)} end
  end
  local memo, work = {}, 0
  local function align(index, pos)
    if index > #groups then return pos == #kana + 1 and 1 or 0, "" end
    local key = index .. ":" .. pos
    if memo[key] then return memo[key][1], memo[key][2] end
    work = work + 1
    if work > 4096 then return 2, "" end
    local group, count, text = groups[index], 0, ""
    if not group.han then
      if kana[pos] == normalized(group.text) then
        count, text = align(index + 1, pos + 1)
        text = group.text .. text
      end
    else
      for finish = pos, #kana do
        local ways, suffix = align(index + 1, finish + 1)
        if ways > 0 then
          count = math.min(2, count + ways)
          text = group.text .. "（" .. table.concat(kana, "", pos, finish) .. "）" .. suffix
          if count >= 2 then break end
        end
      end
    end
    memo[key] = {count, text}
    return count, text
  end
  local count, result = align(1, 1)
  return count == 1 and result or fallback
end

function M.capture(context, selected)
  context:set_property("japanese_learning_commit_text", "")
  context:set_property("japanese_learning_commit_reading", "")
  context:set_property("japanese_learning_commit_annotated", "")
  if not context:get_option("japanese_learning_mode") or context:get_option("ascii_mode") then return end
  local candidate = selected or context:get_selected_candidate()
  if not candidate then return end
  local comment = candidate.comment or ""
  if not comment:find("[[RIME_LANG:JA]]", 1, true) then return end
  local reading = comment:match("%[JF_READING%]([^%[]+)") or comment:match("%[%[RIME_JR:(.-)%]%]")
  if not reading then
    local input = context.input or ""
    if context:get_option("japanese_input_table_enabled") then
      reading = require("japanese_input_table").cloud_reading(input, context)
      if reading then reading = require("mozc_v2_translator").predicted_reading(reading, candidate.text) or reading end
    else reading = require("mozc_v2_translator").romaji_to_hiragana(input) end
  end
  if not reading or reading == "" then return end
  local kana = {}
  for _, cp in utf8.codes(reading) do
    if not ((cp >= 0x3041 and cp <= 0x3096) or (cp >= 0x30a1 and cp <= 0x30f6) or cp == 0x30fc) then return end
    if cp >= 0x30a1 and cp <= 0x30f6 then cp = cp - 0x60 end
    kana[#kana + 1] = utf8.char(cp)
  end
  context:set_property("japanese_learning_commit_text", candidate.text)
  reading = table.concat(kana)
  context:set_property("japanese_learning_commit_reading", reading)
  if context:get_option("japanese_learning_kanji") then
    context:set_property("japanese_learning_commit_annotated", M.format_kanji(candidate.text, reading))
  end
end
return M
