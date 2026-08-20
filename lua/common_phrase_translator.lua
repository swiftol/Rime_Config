local M = {}

do
  local file = io.open(rime_api.get_user_data_dir() .. "/common_phrase_load.log", "a")
  if file then
    file:write(os.date("%Y-%m-%d %H:%M:%S"), " translator loaded\n")
    file:close()
  end
end

local START_MARKER = "# --- RimeSettings common phrases begin ---"
local END_MARKER = "# --- RimeSettings common phrases end ---"

local function phrases_for(input_code)
  local result = {}
  local path = rime_api.get_user_data_dir() .. "/custom_phrase.txt"
  local file = io.open(path, "r")
  if not file then return result end

  local inside = false
  for line in file:lines() do
    line = line:gsub("\r$", "")
    if line == START_MARKER then
      inside = true
    elseif line == END_MARKER then
      inside = false
    elseif inside and line ~= "" and line:sub(1, 1) ~= "#" then
      local text, entry_code = line:match("^([^\t]+)\t([^\t]+)")
      if text and entry_code and entry_code:lower():gsub("%s+", "") == input_code then
        table.insert(result, text)
      end
    end
  end
  file:close()
  return result
end

function M.func(input, seg, env)
  local normalized = input:lower():gsub("%s+", "")
  local matches = phrases_for(normalized)
  for _, text in ipairs(matches) do
    local cand = Candidate("common_phrase", seg.start, seg._end, text, "")
    cand.quality = 1000000000
    yield(cand)
  end
end

return M
