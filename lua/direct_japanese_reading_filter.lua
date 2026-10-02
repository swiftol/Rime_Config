-- Controls only readings attached to ordinary Japanese input candidates.
-- Chinese translation readings are added by the following filter, and fuzzy
-- Japanese readings use [JF_READING], so neither is affected here.
local JR_PATTERN = "%[%[RIME_JR:.-%]%]"

local function direct_japanese_reading_filter(input, env)
  local show = env.engine.context:get_option("show_direct_japanese_reading")
  for cand in input:iter() do
    if not show then
      local comment = cand.comment or ""
      if not comment:find("[JF_READING]", 1, true) then
        local cleaned = comment:gsub(JR_PATTERN, "")
        if cleaned ~= comment then
          cand = ShadowCandidate(cand, cand.type, cand.text, cleaned)
        end
      end
    end
    yield(cand)
  end
end

return direct_japanese_reading_filter
