-- Turn a user-selected Chinese cloud suggestion into personal spelling
-- preferences.  The provider's syllable annotation, not the shorthand sent
-- in the request, is the source of the full-pinyin and initials aliases.
local learning = require("chinese_abbreviation_learning")
local routing = require("mozc_v2_translator")
local M = {}

local local_data = os.getenv("LOCALAPPDATA")
local path = local_data and
  (local_data .. "/ZhongriInputMethod/google_cloud_candidate.tsv") or nil
local cache_path = local_data and
  (local_data .. "/ZhongriInputMethod/google_cloud_candidate_cache.tsv") or nil

local function normalize(code)
  return ((code or ""):lower():gsub("[%s']+", ""))
end

function M.results(code, mode)
  if not path then return {} end
  -- The server keeps a bounded, atomic multi-code cache.  The old one-line
  -- file remains a compatibility fallback during rolling upgrades.
  for _, filename in ipairs({ cache_path, path }) do
    if filename then
      local file = io.open(filename, "rb")
      if file then
        local results, seen = {}, {}
        for line in file:lines() do
          line = line:gsub("\r$", "")
          line = line:gsub("\t10$", "")
          local stored_mode, spelling, text, reading =
            line:match("^([a-z][a-z])\t([^\t\r\n]+)\t([^\t\r\n]+)\t([a-z ]*)$")
          if not stored_mode then
            stored_mode, spelling, text =
              line:match("^([a-z][a-z])\t([^\t\r\n]+)\t([^\t\r\n]+)$")
          end
          if stored_mode == mode and spelling == code then
            if not seen[text] then
              results[#results + 1] = { text = text, reading = reading }
              seen[text] = true
            end
            if #results >= 10 then break end
          end
        end
        file:close()
        if #results > 0 then return results end
      end
    end
  end
  return {}
end

function M.result(code, mode, selected_text)
  for _, item in ipairs(M.results(code, mode)) do
    if not selected_text or item.text == selected_text then return item.text, item.reading end
  end
  return nil
end

function M.codes(text, reading)
  if not text or not reading or reading == "" or
     not reading:match("^[a-z ]+$") then return nil end
  local ok, length = pcall(utf8.len, text)
  if not ok or not length or length < 2 or length > 12 then return nil end
  for _, cp in utf8.codes(text) do
    if not ((cp >= 0x3400 and cp <= 0x4dbf) or
            (cp >= 0x4e00 and cp <= 0x9fff) or
            (cp >= 0xf900 and cp <= 0xfaff)) then return nil end
  end
  local syllables, initials = {}, {}
  for syllable in reading:gmatch("[a-z]+") do
    if not routing.is_single_pinyin_syllable(syllable) then return nil end
    syllables[#syllables + 1] = syllable
    initials[#initials + 1] = syllable:sub(1, 1)
  end
  if #syllables ~= length then return nil end
  return table.concat(syllables), table.concat(initials)
end

local function has_entry(code, text)
  for _, item in ipairs(learning.ranked(code)) do
    if item.text == text then return true end
  end
  return false
end

function M.learn(input, text, reading)
  local full, initials = M.codes(text, reading)
  if not full then return false end
  -- One actual selection, irrespective of how many spelling aliases we add.
  learning.record_word(text)
  local seen = {}
  for _, code in ipairs({ full, initials, normalize(input) }) do
    if code ~= "" and not seen[code] then
      seen[code] = true
      if not has_entry(code, text) then learning.increment(code, text, true) end
    end
  end
  return true
end

return M
