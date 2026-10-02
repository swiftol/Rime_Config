-- Optional, asynchronous Google Input Tools candidate.  The server writes
-- only a completed, full-spelling result; this filter never performs network
-- I/O and never delays local candidate generation.
local cloud_learning = require("google_cloud_learning")
local routing = require("mozc_v2_translator")
local imported = require("japanese_input_table")
local classifier = require("cloud_language_classifier")

local function publish_plan(context, code, zh, ja, priority)
  context:set_property("cloud_query_input", code)
  context:set_property("cloud_query_zh", zh or "")
  context:set_property("cloud_query_ja", ja or "")
  context:set_property("cloud_query_priority", priority or "zh")
end

local function kana_only(reading)
  if not reading or reading == "" then return false end
  for _, cp in utf8.codes(reading) do
    if not ((cp >= 0x3041 and cp <= 0x3096) or
            (cp >= 0x30a1 and cp <= 0x30fa) or cp == 0x30fc) then return false end
  end
  return true
end

local function filter(input, env)
  local context = env.engine.context
  publish_plan(context, context.input or "", "", "", "zh")
  if not context:get_option("google_cloud_candidates") or context:get_option("ascii_mode") then
    for cand in input:iter() do yield(cand) end
    return
  end
  local active = context.composition and context.composition:back() or nil
  -- A cloud result for the original whole spelling must not be inserted into
  -- the remainder after the user has selected one or more local segments.
  if active and active.start and active.start > 0 then
    for cand in input:iter() do yield(cand) end
    return
  end
  local code = context.input or ""
  local japanese = env.engine.schema.schema_id == "japanese_mozc_v2" or
    (context:get_option("japanese_input_table_enabled") and
     not context:get_option("japanese_input_table_mixed"))
  local alphabet = context:get_option("japanese_input_table_enabled") and "^[a-zA-Z0-9'%-]+$" or "^[a-zA-Z'%-]+$"
  if #code < 2 or #code > 128 or not code:match(alphabet) then
    for cand in input:iter() do yield(cand) end
    return
  end
  local limit = math.max(1, math.min(10, tonumber(context:get_property("cloud_candidate_count")) or 2))
  local locals, by_text = {}, {}
  local iterate, state, control = input:iter()
  local function iterator()
    local cand = iterate(state, control)
    control = cand
    return cand
  end
  -- Language evidence must not force the whole dictionary tail to render on
  -- every keystroke. Keep a bounded head and stream the remainder afterwards.
  for _ = 1, 128 do
    local cand = iterator()
    if not cand then break end
    locals[#locals + 1] = cand
    if not by_text[cand.text] then by_text[cand.text] = cand end
  end
  local mode = context:get_property("cloud_language_mode") or "auto"
  local zh = code:match("^[a-z']+$") and #code >= 4 and #code <= 48 and code or ""
  local ja
  if context:get_option("japanese_input_table_enabled") then
    ja = imported.cloud_reading(code, context)
  elseif routing.is_complete_japanese(code) then
    ja = routing.romaji_to_hiragana(code)
  end
  if not kana_only(ja) then ja = "" end
  local priority = japanese and "ja" or "zh"
  if mode == "zh" then ja = ""
  elseif mode == "ja" then zh = ""; priority = "ja"
  elseif mode == "auto" or mode == "both" then
    local recent = context:get_property("cloud_recent_language")
    local age = os.time() - (tonumber(context:get_property("cloud_recent_language_time")) or 0)
    local verdict = classifier.classify(code, ja, locals, routing,
        age >= 0 and age <= 30 and recent or nil)
    context:set_property("cloud_language_evidence", string.format(
        "%s zh=%d ja=%d", verdict.decision, verdict.zh, verdict.ja))
    priority = verdict.priority
    if mode == "auto" then
      if verdict.decision == "ja" then zh = ""
      elseif verdict.decision == "zh" then ja = "" end
    end
  end
  if japanese and mode == "auto" then zh = "" end
  publish_plan(context, code, zh, ja, priority)
  local routes = priority == "ja" and { {"ja", ja}, {"zh", zh} } or { {"zh", zh}, {"ja", ja} }
  local batches = {}
  for _, route in ipairs(routes) do
    batches[#batches + 1] = route[2] ~= "" and cloud_learning.results(route[2], route[1]) or {}
  end
  local inserted, clouds = {}, {}
  -- Interleave independently ranked routes so both are visible even with a
  -- small total limit; never append duplicate texts/annotation sources.
  for rank = 1, 10 do
   for route_index, route in ipairs(routes) do
    local item = batches[route_index][rank]
    if item and #clouds < limit then
    if not inserted[item.text] then
      local cloud_comment = "☁[[RIME_LANG:" .. route[1]:upper() .. "]]"
      if route[1] == "ja" and context:get_option("show_direct_japanese_reading") then
        cloud_comment = cloud_comment .. "[[RIME_JR:" .. ja .. "]]"
      end
      local original = by_text[item.text]
      local comment = original and original.comment or ""
      -- Keep dictionary annotations for the old/new source switch, but use
      -- the cloud identity and position. Never leave the local duplicate
      -- in its old slot merely because it was yielded first.
      if comment ~= "" then
        comment = comment:gsub("%[%[RIME_LANG:[A-Z]+%]%]", ""):gsub("☁", "")
        comment = cloud_comment .. "\n" .. comment
      else
        comment = cloud_comment
      end
      local cloud = Candidate("google_cloud", 0, #code, item.text, comment)
      cloud.quality = 1
      clouds[#clouds + 1] = cloud
      inserted[item.text] = true
    end
    end
   end
   if #clouds >= limit then break end
  end
  local protected_emoji
  if context:get_option("emoji_second_position") then
    for _, cand in ipairs(locals) do
      if cand.type == "emoji_learning" then protected_emoji = cand; break end
    end
  end
  for index, cand in ipairs(clouds) do
    yield(cand)
    if index == 1 and protected_emoji then
      yield(protected_emoji)
      inserted[protected_emoji.text] = true
    end
  end
  for _, cand in ipairs(locals) do
    if not inserted[cand.text] then
      yield(cand)
      inserted[cand.text] = true
    end
  end
  for cand in iterator do
    if not inserted[cand.text] then yield(cand) end
  end
end

return filter
