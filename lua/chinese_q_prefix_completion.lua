local M = {}
local loaded = false
local entries = {}

local function load()
  if loaded then return end
  loaded = true
  local path = rime_api.get_user_data_dir() ..
               "/chinese_q_prefix_completion.tsv"
  local file = io.open(path, "r")
  if not file then return end
  for line in file:lines() do
    line = line:gsub("\r$", "")
    local code, text, weight = line:match("^([^#\t]+)\t([^\t]+)\t([0-9.]+)$")
    if code and text then
      entries[code] = entries[code] or {}
      entries[code][#entries[code] + 1] = {
        text = text,
        weight = tonumber(weight) or 0,
      }
    end
  end
  file:close()
end

function M.lookup(code)
  load()
  return entries[code] or {}
end

function M.has(code)
  load()
  return entries[code] ~= nil
end

return M
