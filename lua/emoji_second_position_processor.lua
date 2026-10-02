local M = {}
local path = rime_api.get_user_data_dir() .. "/emoji_second_position.txt"

local function read_enabled()
  local file = io.open(path, "r")
  if not file then return true end
  local value = (file:read("*l") or ""):gsub("%s+", "")
  file:close()
  return value ~= "0"
end

local function write_enabled(enabled)
  local file = io.open(path, "w")
  if not file then return end
  file:write(enabled and "1\n" or "0\n")
  file:close()
end

function M.init(env)
  local enabled = read_enabled()
  env.last_enabled = enabled
  env.engine.context:set_option("emoji_second_position", enabled)
  -- Materialize the default so a later settings/F4 change has one durable
  -- source of truth instead of relying on schema reset semantics.
  write_enabled(enabled)
end

function M.func(_, env)
  local enabled = env.engine.context:get_option("emoji_second_position")
  if enabled ~= env.last_enabled then
    env.last_enabled = enabled
    write_enabled(enabled)
  end
  return 2
end

return M
