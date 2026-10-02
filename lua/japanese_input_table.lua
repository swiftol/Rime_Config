-- Google/Mozc TXT: input<TAB>output<TAB>pending.  Decode before the ordinary
-- romaji/fuzzy routing so q, repeated keys and symbols keep the imported meaning.
local M = {}
local loaded_generation, loaded_table

function M.build(rules, rgt)
  local root, alphabet, case_sensitive = {}, {}, false
  for _, rule in ipairs(rules) do
    if rule.input:match("[A-Z]") then case_sensitive = true end
    local node = root
    for index = 1, #rule.input do
      local ch = rule.input:sub(index, index)
      alphabet[ch] = true
      node[ch] = node[ch] or {}
      node = node[ch]
    end
    node.rule = rule
  end
  return {root = root, alphabet = alphabet, case_sensitive = case_sensitive, rgt = rgt}
end

function M.parse(code, mapping)
  if not mapping.case_sensitive then code = code:lower() end
  local output, remaining, seen = {}, code, {}
  local last_single = false
  local steps = 0
  while #remaining > 0 do
    if mapping.rgt and remaining:sub(1, 1) == " " then
      remaining = remaining:sub(2)
      last_single = false
    else
    steps = steps + 1
    if seen[remaining] or steps > math.max(256, #code * 8) then
      return "", code, 0
    end
    seen[remaining] = true
    local node, best, length = mapping.root, nil, 0
    for index = 1, #remaining do
      node = node[remaining:sub(index, index)]
      if not node then break end
      if node.rule then best, length = node.rule, index end
    end
    -- A longer unfinished romaji prefix (ky/sh/ch/...) cancels the
    -- provisional single tap. Exact single taps still display immediately.
    if mapping.rgt and node and length < #remaining then break end
    if not best then break end
    output[#output + 1] = best.output
    last_single = #best.input == 1 and best.input:match("[b-df-hj-npr-tvw-yz]") ~= nil
    remaining = best.pending .. remaining:sub(length + 1)
    if #remaining > 4096 then return "", code, 0 end
    end
  end
  local consumed = #code
  if remaining ~= "" then
    -- Only expose a selectable prefix when the retained bytes are a real
    -- suffix of the user's input.  Arbitrary third-column rewrites stay
    -- unconfirmed until a later key finishes them.
    consumed = code:sub(-#remaining) == remaining and #code - #remaining or 0
  end
  return table.concat(output), remaining, consumed, last_single and remaining == ""
end

local function table_for(context)
  local generation = context:get_property("japanese_input_table_generation") or ""
  if loaded_table and generation == loaded_generation then return loaded_table end
  local rules, rgt, alternatives, fixed, normal = {}, false, {}, {}, {}
  local file = io.open(rime_api.get_user_data_dir() .. "/japanese_input_table.tsv", "r")
  if file then
    for line in file:lines() do
      line = line:gsub("\r$", "")
      if line == "# zhongri-mode: rgt-hpk-2018" then rgt = true end
      local fixed_key = line:match("^# zhongri%-fixed: ([a-z0-9]+)$")
      if fixed_key then fixed[fixed_key] = true end
      local alternate_key, alternate_reading = line:match("^# zhongri%-alternative: ([a-z0-9]+)=([^\t\r\n]+)$")
      if alternate_key then alternatives[alternate_key] = alternate_reading end
      local normal_key, normal_reading, normal_pending = line:match("^# zhongri%-normal%-reading: ([a-z0-9]+)=([^;]*);([^;]*)$")
      if normal_key then normal[normal_key] = {normal_reading, normal_pending} end
      local input, output, pending = line:match("^([^\t]+)\t([^\t]*)\t?([^\t]*)$")
      if input then
        rules[#rules + 1] = {input = input, output = output, pending = pending}
      end
    end
    file:close()
  end
  loaded_table = M.build(rules, rgt)
  loaded_table.alternatives, loaded_table.fixed = alternatives, fixed
  loaded_table.normal = normal
  loaded_generation = generation
  return loaded_table
end

local function enabled(context)
  return context:get_option("japanese_input_table_enabled") and
         not context:get_option("ascii_mode")
end

-- Cloud queries must use the imported table's decoded reading, never treat
-- AZIK/RGT key sequences as ordinary romaji. Do not query an unfinished tail.
function M.cloud_reading(code, context)
  local mapping = table_for(context)
  local kana, pending = M.parse(code, mapping)
  if mapping.normal[code] then kana, pending = table.unpack(mapping.normal[code]) end
  return pending == "" and kana or nil
end

function M.fixed_choices(context)
  if not enabled(context) then return nil end
  local active = context.composition and context.composition:back() or nil
  if active and active.start > 0 then return nil end
  local code = context.input or ""
  local mapping = table_for(context)
  if not mapping.fixed[code] then return nil end
  local kana, pending = M.parse(code, mapping)
  if pending ~= "" or kana == "" then return nil end
  local normal = mapping.normal[code]
  return {kana, normal and normal[2] == "" and normal[1] ~= "" and normal[1] or
      (not normal and mapping.alternatives[code] or nil)}
end

function M.processor(key, env)
  local context = env.engine.context
  if key:release() or key:ctrl() or key:alt() or key:super() or
     context:get_option("ascii_mode") then
    return 2
  end
  local keycode = key.keycode
  local function commit_symbol()
    if context:is_composing() then
      if context:get_selected_candidate() then context:commit()
      else
        env.engine:commit_text(context.input or "")
        context:clear()
      end
    end
    env.engine:commit_text(string.char(keycode))
    return 1
  end
  -- A mapped hyphen after letters is Japanese choon, not a literal minus.
  -- At the start of input it remains a direct ASCII symbol. Keep this
  -- exception table-scoped so ordinary Chinese/ASCII symbols are unchanged.
  local spelling = context.input or ""
  if keycode == 0x2d and enabled(context) and context:is_composing() and
     spelling:match("^[a-zA-Z][a-zA-Z%-]*$") and spelling:match("[a-zA-Z]$") then
    local long_mapping = table_for(context)
    local rule = long_mapping.root["-"] and long_mapping.root["-"].rule
    if rule and rule.output == "ー" and rule.pending == "" then
      context:push_input("-")
      env.rgt_at, env.rgt_input = nil, context.input
      return 1
    end
  end
  -- Other literal keyboard symbols must not become imported romaji input, even
  -- when a table assigns that key to kana. Keep comma/period's Japanese
  -- punctuation path and candidate-number keys separate.
  if keycode >= 33 and keycode <= 126 and
     string.char(keycode):match("%p") and keycode ~= 0x2c and keycode ~= 0x2e then
    return commit_symbol()
  end
  if not enabled(context) then return 2 end
  local mapping = table_for(context)
  -- The imported table runs before space_commit_raw in the schema.
  -- Honour the panel's raw/preview mode before table-specific selection or
  -- RGT boundary handling can consume space. In first-candidate mode the
  -- ordinary table behaviour below remains available.
  if keycode == 0x20 then
    local result = require("space_commit_raw")(key, env)
    if result ~= 2 then return result end
  end
  if env.rgt_generation ~= loaded_generation or env.rgt_input ~= context.input then
    env.rgt_at, env.rgt_input = nil, nil
    env.rgt_generation = loaded_generation
  end
  -- Windows' MSVC CRT clock is elapsed wall time. No busy wait or timer is
  -- needed: freeze a provisional tap when the next key arrives after 300 ms.
  local now = os.clock()
  local function provisional()
    local _, _, _, last_single = M.parse(context.input or "", mapping)
    return last_single
  end
  if mapping.rgt and context:is_composing() and keycode == 0x20 and provisional() then
    context:push_input(" ")
    env.rgt_at, env.rgt_input = nil, nil
    return 1
  end
  -- Period and comma share exactly the same punctuation-commit path.
  -- Do not send it through URL recognition or commit a leading Chinese match.
  if keycode == 0x2e or keycode == 0x2c then
    local punctuation_key = string.char(keycode)
    local expected_punctuation = keycode == 0x2e and "。" or "、"
    local rule = mapping.root[punctuation_key] and mapping.root[punctuation_key].rule
    -- Respect imported tables that use these keys for non-punctuation codes.
    if rule and rule.output == expected_punctuation and rule.pending == "" and
       not context:is_composing() then
      env.engine:commit_text(rule.output)
      return 1
    end
    local input = context.input or ""
    local kana, pending, consumed = M.parse(input, mapping)
    local punctuated, remainder, punctuated_consumed = M.parse(input .. punctuation_key, mapping)
    -- AZIT retains the last consonant (ds -> de + pending s) and its old
    -- comma/period completion rows differ (s, -> shi / s. -> su). Match the
    -- existing period's completion, changing only the committed punctuation.
    if keycode == 0x2c and pending ~= "" then
      local period_rule = mapping.root["."] and mapping.root["."].rule
      if period_rule and period_rule.output == "。" and period_rule.pending == "" then
        local period_text, period_pending, period_consumed = M.parse(input .. ".", mapping)
        if period_pending == "" and period_text:sub(-#period_rule.output) == period_rule.output then
          punctuated = period_text:sub(1, #period_text - #period_rule.output) .. expected_punctuation
          remainder, punctuated_consumed = period_pending, period_consumed
        end
      end
    end
    if context:is_composing() and rule and rule.output == expected_punctuation and rule.pending == "" and
       punctuated:sub(-#rule.output) == rule.output and remainder == "" and
       punctuated_consumed == #input + 1 then
      local function complete_japanese(candidate)
        return candidate and candidate.start == 0 and candidate._end == #input and
          (candidate.type == "japanese_input_table" or
           (candidate.comment or ""):find("[[RIME_LANG:JA]]", 1, true))
      end
      local candidate = pending == "" and consumed == #input and context:get_selected_candidate() or nil
      if not complete_japanese(candidate) then
        candidate = nil
        local segment = context.composition:back()
        local menu = segment and segment.menu
        if menu and pending == "" and consumed == #input then
          for index = 0, 63 do
            local item = menu:get_candidate_at(index)
            if not item then break end
            if complete_japanese(item) then candidate = item; break end
          end
        end
      end
      local text = candidate and candidate.text .. rule.output
      if not text then
        local candidates = require("mozc_v2_translator").query_kana(punctuated) or {}
        text = candidates[1] or punctuated
      end
      env.engine:commit_text(text)
      context:clear()
      return 1
    end
    -- A custom table may assign comma/period to a non-punctuation code.
    -- They must still emit directly instead of accumulating in preedit.
    return commit_symbol()
  end
  if keycode == 0xff08 and context:is_composing() then
    context:pop_input(1)
    return 1
  end
  if (keycode == 0x20 or keycode == 0xff0d) and context:is_composing() then
    local candidate = context:get_selected_candidate()
    if candidate then context:commit()
    else
      local kana, pending = M.parse(context.input or "", mapping)
      env.engine:commit_text(kana .. pending)
      context:clear()
    end
    return 1
  end
  if keycode < 33 or keycode > 126 then return 2 end
  local ch = string.char(keycode)
  if not mapping.case_sensitive then ch = ch:lower() end
  -- Numeric suffixes are table input only when they complete a real rule.
  -- A lone 0 and unrelated candidate-number selection remain available.
  if ch:match("%d") and mapping.alphabet[ch] and not mapping.root[ch] then
    local code = (context.input or "") .. ch
    local kana, pending = M.parse(code, mapping)
    if not context:is_composing() or kana == "" or pending ~= "" then return 2 end
  end
  -- Candidate numbers stay available whenever the imported table does not
  -- assign those keys. All other printable keys obey the imported alphabet.
  if mapping.alphabet[ch] or (context:get_option("japanese_input_table_mixed") and ch:match("[a-zA-Z]")) then
    if mapping.rgt and env.rgt_at and now >= env.rgt_at and
       now - env.rgt_at >= 0.300 and provisional() then context:push_input(" ") end
    context:push_input(ch)
    env.rgt_at, env.rgt_input = now, context.input
    return 1
  end
  return 2
end

function M.segmentor(segmentation, env)
  local context = env.engine.context
  if not enabled(context) then return true end
  local start = segmentation:get_current_start_position()
  local finish = #segmentation.input
  if start >= finish then return true end
  local segment = Segment(start, finish)
  segment.tags = Set({"japanese_input_table"})
  if context:get_option("japanese_input_table_mixed") then segment.tags = Set({"japanese_input_table", "abc"}) end
  segmentation:add_segment(segment)
  return false
end

local function katakana(text)
  local result = {}
  for _, cp in utf8.codes(text) do
    if cp >= 0x3041 and cp <= 0x3096 then cp = cp + 0x60 end
    result[#result + 1] = utf8.char(cp)
  end
  return table.concat(result)
end

function M.translator(input, seg, env)
  local context = env.engine.context
  if not enabled(context) or not seg:has_tag("japanese_input_table") then return end
  local mapping = table_for(context)
  local kana, pending, consumed = M.parse(input, mapping)
  local reserved = mapping.fixed[input] and pending == "" and kana or nil
  if mapping.normal[input] then
    kana, pending = table.unpack(mapping.normal[input])
    consumed = pending == "" and #input or
        (input:sub(-#pending) == pending and #input - #pending or 0)
  elseif reserved and mapping.alternatives[input] then
    kana, pending, consumed = mapping.alternatives[input], "", #input
  end
  if not reserved and (kana == "" or consumed == 0) then return end
  local routing = require("mozc_v2_translator")
  local candidates = kana ~= "" and consumed > 0 and routing.query_kana(kana) or {}
  local seen = {}
  local comment = "[[RIME_LANG:JA]][[RIME_JR:" .. kana .. "]]"
  local function emit(text, reading, span)
    if text == "" or seen[text] then return end
    seen[text] = true
    local candidate = Candidate("japanese_input_table", seg.start,
                                seg.start + (span or consumed), text,
                                reading and "[[RIME_LANG:JA]][[RIME_JR:" .. reading .. "]]" or comment)
    candidate.preedit = context:get_option("japanese_input_table_show_letters") and input or kana .. pending
    candidate.quality = 100000
    yield(candidate)
  end
  if reserved then
    emit(reserved, reserved, #input)
    if pending == "" and kana ~= "" then emit(kana) end
  end
  if kana == "" or consumed == 0 then return end
  for _, text in ipairs(candidates) do emit(text) end
  emit(kana)
  emit(katakana(kana))
end

function M.filter(input, env)
  if not enabled(env.engine.context) then
    for cand in input:iter() do yield(cand) end
    return
  end
  if env.engine.context:get_option("japanese_input_table_mixed") then
    -- Keep both routes accessible on page one instead of letting either
    -- language's large candidate list bury the other. Preserve each route's
    -- own ranking and whichever route had the highest-quality first result.
    local japanese, other, first_japanese = {}, {}, nil
    local count = 0
    local function flush()
      -- The imported route is raised above long Chinese prefix lists so it
      -- reaches this bounded buffer. Exact/learned Chinese words retain
      -- first place; assembled abbreviations stay below a complete Japanese
      -- table match. Then interleave the routes for page-one access.
      if #japanese > 0 and #other > 0 then first_japanese = other[1].quality < 300 end
      for index = 1, math.max(#japanese, #other) do
        local first, second = first_japanese and japanese or other, first_japanese and other or japanese
        if first[index] then yield(first[index]) end
        if second[index] then yield(second[index]) end
      end
    end
    for cand in input:iter() do
      count = count + 1
      if count > 64 then yield(cand)
      else
      local is_japanese = cand.type == "japanese_input_table"
      if first_japanese == nil then first_japanese = is_japanese end
      local list = is_japanese and japanese or other
      list[#list + 1] = cand
      if count == 64 then flush() end
      end
    end
    if count < 64 then flush() end
    return
  end
  for cand in input:iter() do
    if cand.type == "japanese_input_table" then yield(cand) end
  end
end

return M
