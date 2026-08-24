local M = {}

local ACCEPTED = 1
local NOOP = 2
local START_MARKER = "# --- RimeSettings common phrases begin ---"
local END_MARKER = "# --- RimeSettings common phrases end ---"

local function load_numeric_codes()
  local prefixes = {}
  local file = io.open(rime_api.get_user_data_dir() .. "/custom_phrase.txt", "r")
  if not file then return prefixes end
  local inside = false
  for line in file:lines() do
    line = line:gsub("\r$", "")
    if line == START_MARKER then
      inside = true
    elseif line == END_MARKER then
      inside = false
    elseif inside then
      local _, code = line:match("^([^\t]+)\t([^\t]+)")
      code = code and code:lower():gsub("%s+", "") or ""
      if code:match("^%d+$") then
        for i = 1, #code do prefixes[code:sub(1, i)] = true end
      end
    end
  end
  file:close()
  return prefixes
end

function M.init(env)
  env.numeric_phrase_prefixes = load_numeric_codes()
end

function M.func(key, env)
  if key:release() or key:ctrl() or key:alt() or key:super() or key:shift() then
    return NOOP
  end
  local key_name = key:repr() or ""
  local context = env.engine.context
  local input = context.input or ""

  -- Only numeric common-phrase composition owns Enter. Letter input keeps the
  -- scheme's existing Return behaviour.
  if (key_name == "Return" or key_name == "KP_Enter") and input:match("^%d+$") then
    local candidate = context:get_selected_candidate()
    -- Downstream filters may wrap the candidate and replace its type. Numeric
    -- composition exists only for configured phrase prefixes, so the selected
    -- candidate itself is the reliable source of truth here.
    if candidate then
      env.engine:commit_text(candidate.text)
      context:clear()
      return ACCEPTED
    end
    return NOOP
  end

  local digit = key_name
  if not digit:match("^%d$") then return NOOP end

  -- Letters are already composing: preserve the normal 1-9 candidate keys.
  if input ~= "" and not input:match("^%d+$") then return NOOP end

  local next_code = input .. digit
  if env.numeric_phrase_prefixes[next_code] then
    context:push_input(digit)
    return ACCEPTED
  end
  -- It is not a configured phrase prefix. Keep ordinary number typing direct.
  if input:match("^%d+$") then context:clear() end
  env.engine:commit_text(next_code)
  return ACCEPTED
end

return M
