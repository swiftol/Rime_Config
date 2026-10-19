-- Optional, asynchronous Google Input Tools candidate.  The server writes
-- only a completed, full-spelling result; this filter never performs network
-- I/O and never delays local candidate generation.
local cloud_learning = require("google_cloud_learning")
local routing = require("mozc_v2_translator")
local imported = require("japanese_input_table")
local classifier = require("cloud_language_classifier")
local exact_priority = require("japanese_exact_priority")

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
    exact_priority.stream(input)
    return
  end
  local active = context.composition and context.composition:back() or nil
  -- A cloud result for the original whole spelling must not be inserted into
  -- the remainder after the user has selected one or more local segments.
  if active and active.start and active.start > 0 then
    exact_priority.stream(input)
    return
  end
  local code = context.input or ""
  local japanese = env.engine.schema.schema_id == "japanese_mozc_v2" or
    (context:get_option("japanese_input_table_enabled") and
     not context:get_option("japanese_input_table_mixed"))
  local alphabet = context:get_option("japanese_input_table_enabled") and "^[a-zA-Z0-9'%-]+$" or "^[a-zA-Z'%-]+$"
  if #code < 2 or #code > 128 or not code:match(alphabet) then
    exact_priority.stream(input, #code < 2 and 32 or 128)
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
  local suppress_assembled = false
  local decided_language
  local lexical_priority
  if mode == "zh" then ja = ""
  elseif mode == "ja" then zh = ""; priority = "ja"
  elseif mode == "auto" or mode == "both" then
    local recent = context:get_property("cloud_recent_language")
    local age = os.time() - (tonumber(context:get_property("cloud_recent_language_time")) or 0)
    -- Earlier Japanese filters can shadow a same-surface Chinese dictionary
    -- entry. Consult the source translator, not the filtered candidate's label.
    local dictionary_zh = false
    if env.engine.schema.schema_id:match('^rime_ice_japanese') then
      if not env.chinese_exact_translator then
        env.chinese_exact_translator = Component.Translator(env.engine, '', 'script_translator@translator')
      end
      dictionary_zh = next(routing.chinese_exact_words(code, env.chinese_exact_translator, active)) ~= nil
    end
    local verdict = classifier.classify(code, ja, locals, routing,
        age >= 0 and age <= 30 and recent or nil, dictionary_zh)
    context:set_property("cloud_language_evidence", string.format(
        "%s zh=%d ja=%d", verdict.decision, verdict.zh, verdict.ja))
    priority = verdict.priority
    if dictionary_zh and priority == 'zh' then lexical_priority = 'zh' end
    -- No full reading yet (e.g. an imported table retains a final n): keep
    -- provisional local order rather than treating the unfinished prefix as
    -- a complete Chinese/Japanese sentence.
    if ja ~= "" and verdict.decision ~= "both" then decided_language = verdict.decision end
    suppress_assembled = mode == "auto" and verdict.decision == "zh" and not japanese
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
  local final_head = {}
  local function suppressed(candidate)
    if not suppress_assembled then return false end
    local comment, kind = candidate.comment or "", candidate.type or ""
    if comment:find("[[RIME_JA_SENTENCE]]", 1, true) then return false end
    if comment:find("[[RIME_JA_GRAMMAR]]", 1, true) then return false end
    -- An enabled correction with a proven whole-word dictionary spelling is
    -- not speculative sentence assembly. Language guessing may rank it, not
    -- silently remove the user's configured local fuzzy route.
    if comment:find('[[RIME_JA_FUZZY_LEXICAL]]',1,true) then return false end
    return comment:find("[[RIME_JA_ASSEMBLED]]", 1, true) or
        (comment:find("[[RIME_LANG:JA]]", 1, true) and
         (kind:find("fuzzy", 1, true) or
          comment:find("[[RIME_FUZZY_", 1, true) or comment:find("[JF:", 1, true)))
  end
  for index, cand in ipairs(clouds) do
    final_head[#final_head + 1] = cand
    if index == 1 and protected_emoji then
      final_head[#final_head + 1] = protected_emoji
      inserted[protected_emoji.text] = true
    end
  end
  -- Apply strong language evidence to local results as well. Otherwise an
  -- imported table's high-quality raw kana can still take first place for a
  -- clearly Chinese spelling while its cloud response is pending/unavailable.
  -- Ambiguous short readings retain the existing bilingual local order.
  local ordered_locals = locals
  local preferred_language = decided_language or lexical_priority
  if preferred_language then
    ordered_locals = {}
    for _, preferred in ipairs({true, false}) do
      for _, cand in ipairs(locals) do
        local is_ja = (cand.comment or ""):find("[[RIME_LANG:JA]]", 1, true) ~= nil
        if (is_ja == (preferred_language == "ja")) == preferred then
          ordered_locals[#ordered_locals + 1] = cand
        end
      end
    end
  end
  for _, cand in ipairs(ordered_locals) do
    if not inserted[cand.text] and not suppressed(cand) then
      final_head[#final_head + 1] = cand
      inserted[cand.text] = true
    end
  end
  -- As in Metasequoia's provider, a validated local full conversion anchors
  -- the head; asynchronous suggestions follow it rather than replacing it.
  if mode ~= 'zh' and ja ~= '' and priority=='ja' then
    local anchored=false
    for _,evidence_kind in ipairs({'grammar','sentence','lexical'}) do
     for index,cand in ipairs(final_head) do
      local comment=cand.comment or ''
      local original=by_text[cand.text]
      local source=original or cand
      local supported=(evidence_kind=='grammar' and comment:find('[[RIME_JA_GRAMMAR]]',1,true)) or
          (evidence_kind=='sentence' and comment:find('[[RIME_JA_SENTENCE]]',1,true)) or
          (evidence_kind=='lexical' and priority=='ja' and source.type=='japanese_input_table' and
           not comment:find('[[RIME_JA_ASSEMBLED]]',1,true))
      if (cand.type~='google_cloud' or original) and cand.start==0 and cand._end==#code and
         comment:find('[[RIME_LANG:JA]]',1,true) and
         supported then
        if index>1 then table.remove(final_head,index);table.insert(final_head,1,cand) end
        anchored=true
        break
      end
     end
     if anchored then break end
    end
  end
  -- Language detection is ranking evidence, not permission to hide a whole
  -- exact conversion behind dozens of Chinese prefix homophones. Keep one
  -- non-speculative Japanese result visible even when the input is ambiguous.
  if mode ~= 'zh' and ja ~= '' then
    local visible=false
    for _,kind in ipairs({'grammar','sentence','lexical'}) do
      for index,cand in ipairs(final_head) do
        local comment=cand.comment or ''
        local supported=(kind=='grammar' and comment:find('[[RIME_JA_GRAMMAR]]',1,true)) or
          (kind=='sentence' and comment:find('[[RIME_JA_SENTENCE]]',1,true)) or
          (kind=='lexical' and not comment:find('[[RIME_JA_ASSEMBLED]]',1,true))
        if supported and cand.start==0 and cand._end==#code and
           comment:find('[[RIME_LANG:JA]]',1,true) and
           cand.type~='japanese_input_table_prediction' and
           not (cand.type or ''):find('fuzzy',1,true) then
          if index>3 then table.remove(final_head,index);table.insert(final_head,3,cand) end
          visible=true;break
        end
      end
      if visible then break end
    end
  end
  for _, cand in ipairs(exact_priority.order(final_head)) do yield(cand) end
  for cand in iterator do
    if not inserted[cand.text] and not suppressed(cand) then yield(cand) end
  end
end

return filter
