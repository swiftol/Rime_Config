-- Final, bounded ordering contract, after cloud merging and personal ranking.
-- Learning may reorder corrections among themselves, never above exact input.
local M = {}
local function fuzzy(candidate)
  local comment = candidate.comment or ""
  return comment:find("[JF_READING]", 1, true) ~= nil or
         comment:find("[[RIME_FUZZY_", 1, true) ~= nil or
         candidate.type == "japanese_input_table_fuzzy" or
         candidate.type == "mozc_v2_preferred_correction"
end
local function exact(candidate)
  local comment = candidate.comment or ""
  return not fuzzy(candidate) and
      candidate.type ~= "japanese_input_table_prediction" and
      candidate.type ~= "completion" and
      not comment:find("[JP:PREFIX]", 1, true) and
      comment:find("[[RIME_LANG:JA]]", 1, true) ~= nil
end
function M.order(head)
  local first
  for index, candidate in ipairs(head) do if fuzzy(candidate) then first = index; break end end
  if not first then return head end
  local promoted, rest = {}, {}
  for index = first, #head do
    local candidate = head[index]
    local bucket = exact(candidate) and promoted or rest
    bucket[#bucket + 1] = candidate
  end
  local result = {}
  for index = 1, first - 1 do result[#result + 1] = head[index] end
  for _, candidate in ipairs(promoted) do result[#result + 1] = candidate end
  for _, candidate in ipairs(rest) do result[#result + 1] = candidate end
  return result
end
function M.stream(input, limit)
  local iterator, state, control = input:iter()
  local head = {}
  for _ = 1, limit or 128 do
    local candidate = iterator(state, control)
    control = candidate
    if not candidate then break end
    head[#head + 1] = candidate
  end
  for _, candidate in ipairs(M.order(head)) do yield(candidate) end
  while control do
    local candidate = iterator(state, control)
    control = candidate
    if candidate then yield(candidate) end
  end
end
return M
