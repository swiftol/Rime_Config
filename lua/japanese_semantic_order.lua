-- Bounded, stable final ordering. No conversion/network work in this module.
local M = {}
local function marked(cand, tag)
  return (cand.comment or ''):find('[['..tag..']]',1,true) ~= nil
end
function M.fallback(cand)
  return marked(cand,'RIME_JA_KANA_FALLBACK') or marked(cand,'RIME_JA_RESERVED_UNSUPPORTED')
end
function M.order(head,has_reserved_input)
  local lexical, semantic, rest, fallback, counters = {}, {}, {}, {}, {}
  local corrections = {}
  local correction_anchor
  local clock, preferred, supported, identities = {}, {}, {}, {}
  local has_semantic=has_reserved_input or false
  local has_correction, model_first = false, nil
  for _,cand in ipairs(head) do
    if marked(cand,'RIME_JA_FUZZY_LEXICAL') then has_correction=true end
    if not model_first and cand.type=='japanese_input_table' then model_first=cand end
    -- A reserved interpretation exists even when none of its guesses is
    -- supported. Do not lose ordinary exact-word priority in that case.
    if marked(cand,'RIME_JA_RESERVED_COMBINATION') then has_semantic=true end
    if marked(cand,'RIME_JA_RESERVED_SUPPORTED') then
      has_semantic=true
      local bucket=marked(cand,'RIME_JA_RESERVED_CLOCK') and clock or marked(cand,'RIME_JA_RESERVED_IDENTITY') and identities or
          (marked(cand,'RIME_JA_RESERVED_PREFERRED') and preferred or supported)
      bucket[#bucket+1]=cand
    end
  end
  local function proven_lexical(cand)
    if not marked(cand,'RIME_JA_LEXICAL') then return false end
    -- Do not disturb normal model ordering when no full-word repair exists.
    -- Preserve the model's preferred unchanged expression even when it is
    -- productive grammar rather than a standalone dictionary entry.
    if not has_correction or cand==model_first or cand.type~='japanese_input_table' then return true end
    local reading=(cand.comment or ''):match('%[%[RIME_JR:(.-)%]%]')
    return reading and require('mozc_v2_translator').lexical_kana(reading,cand.text)
  end
  local selected, expression={},false
  for _,bucket in ipairs({clock,preferred,supported,identities}) do
    for _,cand in ipairs(bucket) do
      if #semantic<2 then
        semantic[#semantic+1]=cand;selected[cand]=true
        if marked(cand,'RIME_JA_RESERVED_EXPRESSION') then expression=true end
      end
    end
  end
  for _,cand in ipairs(head) do
    local bucket=rest
    if M.fallback(cand) then bucket=fallback
    elseif marked(cand,'RIME_JA_COUNTER') then bucket=counters
    elseif marked(cand,'RIME_JA_FUZZY_LEXICAL') then
      bucket=corrections
      correction_anchor=correction_anchor or (#rest+1)
    elseif proven_lexical(cand) and
        not marked(cand,'RIME_JA_RESERVED_COMBINATION') then bucket=lexical
    end
    if not selected[cand] then bucket[#bucket+1]=cand end
  end
  -- Only proven full-word corrections may cross speculative Japanese
  -- assemblies/predictions. Preserve exact lexical entries, reserved grammar
  -- and Chinese priority; do not turn every fuzzy prefix into a whole word.
  if #corrections > 0 then
    local ordered, inserted = {}, false
    for index,cand in ipairs(rest) do
      local generated = marked(cand,'RIME_LANG:JA') and
          cand.type ~= 'mozc_v2_dictionary_exact' and
          not proven_lexical(cand) and cand~=model_first and
          not marked(cand,'RIME_JA_RESERVED_COMBINATION') and
          not marked(cand,'RIME_JA_GRAMMAR')
      if not inserted and (generated or index >= correction_anchor) then
        for _,correction in ipairs(corrections) do ordered[#ordered+1]=correction end
        inserted=true
      end
      ordered[#ordered+1] = cand
    end
    if not inserted then for _,cand in ipairs(corrections) do ordered[#ordered+1]=cand end end
    rest=ordered
  end
  -- Within the bounded complete-combination group, retain identity spellings
  -- after all converted alternatives. This is not a blanket kana penalty:
  -- the model's preferred kana expression carries no IDENTITY marker.
  local last_combination=0
  local deferred={}
  for index,cand in ipairs(rest) do
    if marked(cand,'RIME_JA_RESERVED_COMBINATION') and not marked(cand,'RIME_JA_RESERVED_IDENTITY') then
      last_combination=index
    end
  end
  if last_combination>0 then
    local ordered={}
    for _,cand in ipairs(rest) do
      if marked(cand,'RIME_JA_RESERVED_IDENTITY') then deferred[#deferred+1]=cand end
    end
    for index,cand in ipairs(rest) do
      if not marked(cand,'RIME_JA_RESERVED_IDENTITY') then ordered[#ordered+1]=cand end
      if index==last_combination then for _,identity in ipairs(deferred) do ordered[#ordered+1]=identity end end
    end
    rest=ordered
  end
  local result={}
  -- A genuine ordinary whole word (e.g. 決断) stays ahead of shortcuts.
  -- Otherwise up to two supported semantic combinations occupy the head.
  -- Whole expressions / polite suffixes disambiguate meaning from ordinary
  -- sound. Other combinations remain secondary to full words such as 決断.
  local buckets=expression and {semantic,lexical,rest} or {lexical,semantic,rest}
  for _,cand in ipairs(counters) do result[#result+1]=cand end
  for _,bucket in ipairs(buckets) do
    for _,cand in ipairs(bucket) do result[#result+1]=cand end
  end
  return result,fallback
end
return M
