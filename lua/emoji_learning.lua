local M = {}
local loaded = false
local scores = {}
local path = rime_api.get_user_data_dir() .. "/emoji_learning.tsv"

local function normalize_code(code)
  return (code or ""):lower():gsub("[%s']+", "")
end

function M.load()
  if loaded then return end
  loaded = true
  local file = io.open(path, "r")
  if not file then return end
  for line in file:lines() do
    line = line:gsub("\r$", "")
    local code, text = line:match("^([^\t]+)\t([^\t]+)$")
    code = normalize_code(code)
    if code ~= "" and text and text ~= "" then
      scores[code] = scores[code] or {}
      scores[code][text] = (scores[code][text] or 0) + 1
    end
  end
  file:close()
end

function M.increment(code, text)
  M.load()
  code = normalize_code(code)
  if code == "" or not text or text == "" then return end
  scores[code] = scores[code] or {}
  scores[code][text] = (scores[code][text] or 0) + 1
  local file = io.open(path, "a")
  if file then
    file:write(code, "\t", text, "\n")
    file:close()
  end
end

function M.ranked(code)
  M.load()
  code = normalize_code(code)
  local result = {}
  for text, score in pairs(scores[code] or {}) do
    result[#result + 1] = { text = text, score = score }
  end
  table.sort(result, function(a, b)
    if a.score ~= b.score then return a.score > b.score end
    return a.text < b.text
  end)
  return result
end

return M
