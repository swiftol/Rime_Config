-- Hide extremely low-frequency Chinese single characters for their exact
-- pinyin, while leaving multi-character words and Japanese candidates alone.
local M = {}

local function normalize_code(code)
  return (code or ""):lower():gsub("[%s']", "")
end

function M.init(env)
  local config = env.engine.schema.config
  env.threshold = config:get_int("rare_single_char_filter/frequency_threshold") or 4000
  env.entries = {}
  local path = rime_api.get_user_data_dir() .. "/cn_dicts/8105.dict.yaml"
  local file = io.open(path, "r")
  if not file then return end

  local in_body = false
  for line in file:lines() do
    if line == "..." then
      in_body = true
    elseif in_body and line:sub(1, 1) ~= "#" then
      local text, code, weight = line:match("^([^\t]+)\t([^\t]+)\t(%d+)")
      if text and utf8.len(text) == 1 then
        local entry = env.entries[text] or { max_weight = 0, codes = {} }
        local number = tonumber(weight) or 0
        if number > entry.max_weight then entry.max_weight = number end
        entry.codes[normalize_code(code)] = true
        env.entries[text] = entry
      end
    end
  end
  file:close()
end

function M.func(input, env)
  local code = normalize_code(env.engine.context.input)
  for candidate in input:iter() do
    local entry = env.entries[candidate.text]
    local hide = entry
      and entry.max_weight < env.threshold
      and entry.codes[code]
    if not hide then yield(candidate) end
  end
end

function M.fini(env)
  env.entries = nil
end

return M
