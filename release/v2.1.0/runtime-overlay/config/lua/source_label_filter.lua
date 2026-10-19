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
  local editing=require('japanese_segment_edit')
  if editing.current(env.engine.context) then editing.emit(env.engine.context);return end
  local table_module=require('japanese_input_table')
  local choices = table_module.fixed_choices(env.engine.context)
  local iterator, state, control = input:iter()
  local function next_candidate()
    local cand = iterator(state, control)
    control = cand
    return cand
  end
  local head, pinned, seen = {}, {}, {}
  local semantic_order=require('japanese_semantic_order')
  local imported=env.engine.context:get_option('japanese_input_table_enabled')
  local reserved_input=imported and table_module.has_reserved_input(env.engine.context.input or '',env.engine.context)
  if choices or imported then
    local window = not choices and #(env.engine.context.input or '') == 1 and 32 or 128
    for _ = 1, window do
      local cand = next_candidate()
      if not cand then break end
      head[#head + 1] = cand
      for index, text in ipairs(choices or {}) do
        if cand.text == text and not pinned[index] then pinned[index] = cand end
      end
      if choices then
        local complete=true
        for index in ipairs(choices) do if not pinned[index] then complete=false;break end end
        if complete then break end
      end
    end
  end
  local function emit(cand)
    local comment = cand.comment or ""
    local cleaned = comment
    for _, pattern in ipairs(SOURCE_LABEL_PATTERNS) do
      cleaned = cleaned:gsub(pattern, "")
    end
    -- Cloud routing has already consumed this proof. It is not pronunciation
    -- metadata: older native readers would include it inside [JF_READING],
    -- corrupting both the closing bracket and correction color alignment.
    cleaned = cleaned:gsub("%[%[RIME_JA_FUZZY_LEXICAL%]%]", "")
    cleaned = cleaned:gsub("^%s+", ""):gsub("%s+$", "")
    if cleaned ~= comment then
      cand = ShadowCandidate(cand, cand.type, cand.text, cleaned)
    end
    yield(cand)
  end
  for index in ipairs(choices or {}) do
    if pinned[index] then emit(pinned[index]); seen[pinned[index].text] = true end
  end
  local fallback={}
  if imported then head,fallback=semantic_order.order(head,reserved_input) end
  for _, cand in ipairs(head) do
    if not seen[cand.text] then emit(cand) end
  end
  for cand in next_candidate do
    if imported and semantic_order.fallback(cand) then fallback[#fallback+1]=cand
    elseif not seen[cand.text] then emit(cand) end
  end
  -- Lazy tail: do not eagerly exhaust the dictionary to move two identity
  -- spellings down. They appear only after the useful stream is consumed.
  for _,cand in ipairs(fallback) do
    if not seen[cand.text] then emit(cand);seen[cand.text]=true end
  end
end

return source_label_filter
