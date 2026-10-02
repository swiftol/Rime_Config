-- F4 is schema-only. Seed settings-panel defaults without overriding values
-- explicitly saved in user.yaml (including false).
local M = {}

local true_defaults = {
  emoji = true,
  show_english_annotation = true,
  show_japanese_annotation = true,
  show_single_character_annotation = true,
  japanese_continuation_lock = true,
  japanese_prefix_completion_disabled = true,
  japanese_prefix_completion_japanese_first = true,
  japanese_fuzzy_sokuon = true,
  japanese_fuzzy_long_i = true,
  japanese_fuzzy_long_u = true,
  japanese_fuzzy_long_mark = true,
  japanese_fuzzy_chi_ji = true,
  japanese_fuzzy_hu_fu = true,
  japanese_fuzzy_shu_sho = true,
  japanese_fuzzy_ke_kai = true,
  japanese_fuzzy_ke_kae_gae = true,
  japanese_fuzzy_sei_sai = true,
  japanese_fuzzy_dakuten = true,
}

local function saved_options()
  local present = {}
  local file = io.open(rime_api.get_user_data_dir() .. "/user.yaml", "r")
  if not file then return present end
  for line in file:lines() do
    local name, value = line:match("^    ([%w_]+):%s*(%a+)%s*$")
    if name and (value == "true" or value == "false") then
      present[name] = true
    end
  end
  file:close()
  return present
end

function M.init(env)
  local present = saved_options()
  for name in pairs(true_defaults) do
    if not present[name] then env.engine.context:set_option(name, true) end
  end
end

function M.func()
  return 2
end

return M
