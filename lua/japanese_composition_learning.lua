-- Learn Japanese spellings selected one character at a time.  This is user
-- history, never a built-in dictionary of names.
local M = { counts = {}, observations = {}, loaded = false }

local function path(suffix)
  return rime_api.get_user_data_dir() .. "/japanese_composition_" .. suffix .. ".tsv"
end

local function key(code, text)
  return code .. "\t" .. text
end

local function read_rows(filename, target)
  local file = io.open(path(filename), "r")
  if not file then return end
  for line in file:lines() do
    local code, word, count = line:match("^([^\t]+)\t([^\t]+)\t(%d+)$")
    if code and word and count then target[key(code, word)] = tonumber(count) end
  end
  file:close()
end

local function save_rows(filename, source)
  local rows = {}
  for item, count in pairs(source) do
    rows[#rows + 1] = item .. "\t" .. count
  end
  table.sort(rows)
  local file = io.open(path(filename), "w")
  if not file then return end
  if #rows > 0 then file:write(table.concat(rows, "\n"), "\n") end
  file:close()
end

function M.load()
  if M.loaded then return end
  M.loaded = true
  read_rows("learning", M.counts)
  read_rows("observations", M.observations)
end

function M.increment(code, text)
  if not code or code == "" or not text or text == "" then return end
  M.load()
  local item = key(code, text)
  M.counts[item] = (M.counts[item] or 0) + 1
  save_rows("learning", M.counts)
end

function M.observe(code, text)
  if not code or #code < 4 or #code > 32 or not text or text == "" then return end
  M.load()
  local item = key(code, text)
  M.observations[item] = (M.observations[item] or 0) + 1
  save_rows("observations", M.observations)
  -- A single accidental adjacent pair should not become a permanent name.
  -- Longer sequences need one more confirmation because phrase boundaries are
  -- not available when the user commits each character separately.
  local needed = utf8.len(text) >= 4 and 3 or 2
  if M.observations[item] == needed then M.increment(code, text) end
end

function M.ranked(code)
  M.load()
  local results = {}
  for item, count in pairs(M.counts) do
    local stored_code, word = item:match("^([^\t]+)\t(.+)$")
    if stored_code == code then
      results[#results + 1] = { text = word, score = count }
    end
  end
  table.sort(results, function(a, b)
    if a.score ~= b.score then return a.score > b.score end
    return a.text < b.text
  end)
  return results
end

return M
