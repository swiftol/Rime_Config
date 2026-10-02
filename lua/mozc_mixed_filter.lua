local M = {}
local routing = require("mozc_v2_translator")
local continuation_context = require("japanese_continuation_context")

local LANGUAGE_ZH = "[[RIME_LANG:ZH]]"
local nonempty_frames, nonempty_order = {}, {}
local NONEMPTY_LIMIT = 128

local function remember_frame(code, candidates)
  if code == "" or #candidates == 0 then return end
  local snapshot = {}
  for index = 1, math.min(9, #candidates) do
    local cand = candidates[index]
    snapshot[#snapshot + 1] = {
      text = cand.text, comment = cand.comment or "",
    }
  end
  if not nonempty_frames[code] then
    nonempty_order[#nonempty_order + 1] = code
    if #nonempty_order > NONEMPTY_LIMIT then
      nonempty_frames[table.remove(nonempty_order, 1)] = nil
    end
  end
  nonempty_frames[code] = snapshot
end

local function nearest_frame(code, start_pos, end_pos)
  for length = #code - 1, 1, -1 do
    local snapshot = nonempty_frames[code:sub(1, length)]
    if snapshot and #snapshot > 0 then
      local result = {}
      for index, item in ipairs(snapshot) do
        local cand = Candidate("continuity_fallback", start_pos, end_pos,
                               item.text, item.comment)
        cand.quality = 1 - index * 0.001
        result[#result + 1] = cand
      end
      return result
    end
  end
  return {}
end

local function yield_remembered(code, candidates, context)
  remember_frame(code, candidates)
  if context then continuation_context.remember_candidates(context, candidates) end
  for _, cand in ipairs(candidates) do yield(cand) end
end

function M.func(input, env)
  if env.engine.context:get_option("japanese_input_table_enabled") then
    for cand in input:iter() do yield(cand) end
    return
  end
  local schema = env.engine.schema.schema_id or ""
  local context = env.engine.context
  local active_input = context.input or ""
  local active = context.composition and context.composition:back() or nil
  if active and active.start and active.start > 0 then
    active_input = active_input:sub(active.start + 1, active._end or #active_input)
  end
  local code = active_input:lower():gsub("[%s']+", "")
  if schema == "rime_ice_japanese_mozc" and
     continuation_context.selected_japanese_prefix(context) then
    -- A selected kanji-only Japanese name is still Japanese.  Do not reopen
    -- the Chinese stream just because the remaining romaji (da+san) also
    -- happens to be complete Mandarin pinyin.
    local japanese = {}
    for cand in input:iter() do
      local comment = cand.comment or ""
      if (cand.type or ""):match("^mozc_v2") or
         comment:find("[[RIME_LANG:JA]]", 1, true) then
        japanese[#japanese + 1] = cand
      end
    end
    yield_remembered(code, japanese, context)
    return
  end
  if schema ~= "rime_ice_japanese_mozc" or
     not routing.should_prefer_japanese(code) then
    local candidates = {}
    for cand in input:iter() do candidates[#candidates + 1] = cand end
    yield_remembered(code, candidates, context)
    return
  end

  -- Short grammar forms such as desu / masu are valid Japanese romaji and
  -- also split cleanly into Mandarin syllables (de su / ma su).  The legacy
  -- mixed schema kept both languages for these genuinely dual-valid inputs:
  -- Japanese conversion first, Chinese homophones afterwards.  Do not apply
  -- the long-sentence Chinese suppression rule to this case.
  if routing.is_complete_pinyin(code) then
    local japanese, fallback = {}, {}
    for cand in input:iter() do
      local comment = cand.comment or ""
      if (cand.type or ""):match("^mozc_v2") or
         comment:find("[[RIME_LANG:JA]]", 1, true) ~= nil then
        japanese[#japanese + 1] = cand
      else
        fallback[#fallback + 1] = cand
      end
    end
    local candidates = {}
    for _, cand in ipairs(japanese) do candidates[#candidates + 1] = cand end
    for _, cand in ipairs(fallback) do candidates[#candidates + 1] = cand end
    yield_remembered(code, candidates, context)
    return
  end

  local candidates = {}
  for cand in input:iter() do
    local comment = cand.comment or ""
    -- This schema deliberately replaces the legacy Japanese translators with
    -- Mozc.  For strongly Japanese input, do not wait for a Mozc candidate in
    -- the same translation stream before suppressing Chinese: librime may
    -- filter separate translation streams independently.  That old check was
    -- timing-dependent and occasionally let a gibberish Chinese sentence take
    -- first place even though the correct Mozc sentence arrived immediately
    -- afterwards.
    if (cand.type or ""):match("^mozc_v2") or
       comment:find("[[RIME_LANG:JA]]", 1, true) ~= nil then
      candidates[#candidates + 1] = cand
    end
  end
  if #candidates == 0 and code ~= "" then
    local start_pos = active and active.start or 0
    local end_pos = active and active._end or #active_input
    candidates = nearest_frame(code, start_pos, end_pos)
  end
  yield_remembered(code, candidates, context)
end

-- Registered explicitly from rime.lua as `lua_filter@mozc_mixed_filter`.
-- Explicit Lua filters must export the filter function itself; returning the
-- `{ func = ... }` module table silently loads the module but never invokes
-- the filter in a real TSF session.
return M.func
