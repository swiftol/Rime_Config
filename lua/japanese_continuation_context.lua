-- A selected kanji-only Japanese name is still Japanese.  The installed
-- librime-lua cannot inspect earlier composition segments, so retain a small
-- snapshot of the final candidate menu and identify the selected prefix from
-- the preedit after a partial selection.
local M = {}
local frames, order = {}, {}
local FRAME_LIMIT = 48

function M.remember_candidates(context, candidates)
  local composition = context.composition
  local active = composition and composition:back() or nil
  if not active then return end
  local raw = context.input or ""
  if raw == "" then return end
  local selected = active.start > 0 and M.selected_japanese_prefix(context) or nil
  if active.start > 0 and not selected then return end
  local entries = active.start == 0 and {} or (frames[raw] or {})
  local seen = {}
  for _, item in ipairs(entries) do
    seen[item.text .. ":" .. tostring(item.finish)] = true
  end
  -- Match the mixed sorter's maximum scan window so a Japanese character
  -- chosen from a later candidate page retains its language too.
  for index = 1, math.min(#candidates, 384) do
    local cand = candidates[index]
    local text = (selected and selected.text or "") .. (cand.text or "")
    local finish = cand._end
    local key = text .. ":" .. tostring(finish)
    if text ~= "" and finish and not seen[key] and #entries < 768 then
      seen[key] = true
      local comment = cand.comment or ""
      entries[#entries + 1] = {
        text = text,
        finish = finish,
        japanese = comment:find("[[RIME_LANG:JA]]", 1, true) ~= nil or
          (cand.type or ""):match("^mozc_v2") ~= nil,
      }
    end
  end
  if not frames[raw] then
    order[#order + 1] = raw
    if #order > FRAME_LIMIT then frames[table.remove(order, 1)] = nil end
  end
  frames[raw] = entries
end

function M.selected_japanese_prefix(context)
  if not context:get_option("japanese_continuation_lock") then return nil end
  local composition = context.composition
  if not composition or composition:empty() then return nil end
  local active = composition:back()
  if not active or not active.start or active.start <= 0 then return nil end
  local raw = context.input or ""
  local shown = (context:get_preedit() or {}).text or ""
  local selected
  for _, item in ipairs(frames[raw] or {}) do
    if item.finish == active.start and
       shown:sub(1, #item.text) == item.text and
       (not selected or #item.text > #selected.text) then
      selected = item
    end
  end
  if not selected or not selected.japanese then return nil end
  local prefix_code = raw:sub(1, active.start):lower():gsub("[%s']+", "")
  local active_code = raw:sub(active.start + 1, active._end or #raw)
    :lower():gsub("[%s']+", "")
  if prefix_code == "" or active_code == "" then return nil end
  return {
    text = selected.text,
    prefix_code = prefix_code,
    active_code = active_code,
  }
end

return M
