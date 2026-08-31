-- Persistent selection frequency for fuzzy Japanese candidates.
local M = { counts = {}, loaded = false }

local function path()
  return rime_api.get_user_data_dir() .. "/japanese_fuzzy_learning.tsv"
end

local function key(input, text)
  return (input or "") .. "\t" .. (text or "")
end

function M.load()
  if M.loaded then return end
  M.loaded = true
  local file = io.open(path(), "r")
  if not file then return end
  for line in file:lines() do
    local input, text, count = line:match("^([^\t]+)\t([^\t]+)\t(%d+)$")
    if input and text and count then M.counts[key(input, text)] = tonumber(count) end
  end
  file:close()
end

function M.score(input, text)
  M.load()
  return M.counts[key(input, text)] or 0
end

function M.increment(input, text)
  if not input or input == "" or not text or text == "" then return end
  M.load()
  local item = key(input, text)
  M.counts[item] = (M.counts[item] or 0) + 1
  local file = io.open(path(), "w")
  if not file then return end
  local rows = {}
  for stored, count in pairs(M.counts) do
    local code, word = stored:match("^([^\t]+)\t(.+)$")
    if code and word then rows[#rows + 1] = code .. "\t" .. word .. "\t" .. count end
  end
  table.sort(rows)
  file:write(table.concat(rows, "\n"))
  if #rows > 0 then file:write("\n") end
  file:close()
end

return M
