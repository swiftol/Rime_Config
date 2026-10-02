-- Remove internal scheme/source names that can be appended by the final
-- uniquifier when the same Japanese candidate comes from several translators.
-- Genuine readings and translation annotations must remain untouched.
local SOURCE_LABEL_PATTERNS = {
  "%[雾凇中日[^%]]*%]",
  "%[雾松中日[^%]]*%]",
  "%[雾凇拼音·中日[^%]]*%]",
  "%[中日输入法[^%]]*%]",
}

local function source_label_filter(input, env)
  local choices = require("japanese_input_table").fixed_choices(env.engine.context)
  local iterator, state, control = input:iter()
  local function next_candidate()
    local cand = iterator(state, control)
    control = cand
    return cand
  end
  local head, pinned, seen = {}, {}, {}
  if choices then
    for _ = 1, 128 do
      local cand = next_candidate()
      if not cand then break end
      head[#head + 1] = cand
      for index, text in ipairs(choices) do
        if cand.text == text and not pinned[index] then pinned[index] = cand end
      end
      if pinned[1] and (not choices[2] or pinned[2]) then break end
    end
  end
  local function emit(cand)
    local comment = cand.comment or ""
    local cleaned = comment
    for _, pattern in ipairs(SOURCE_LABEL_PATTERNS) do
      cleaned = cleaned:gsub(pattern, "")
    end
    cleaned = cleaned:gsub("^%s+", ""):gsub("%s+$", "")
    if cleaned ~= comment then
      cand = ShadowCandidate(cand, cand.type, cand.text, cleaned)
    end
    yield(cand)
  end
  for index in ipairs(choices or {}) do
    if pinned[index] then emit(pinned[index]); seen[pinned[index].text] = true end
  end
  for _, cand in ipairs(head) do
    if not seen[cand.text] then emit(cand) end
  end
  for cand in next_candidate do
    if not seen[cand.text] then emit(cand) end
  end
end

return source_label_filter
