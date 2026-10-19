-- Google/Mozc TXT: input<TAB>output<TAB>pending.  Decode before the ordinary
-- romaji/fuzzy routing so q, repeated keys and symbols keep the imported meaning.
local M = {}
local loaded_generation, loaded_table

local function reserved_span_tag(spans)
  if not spans or #spans==0 then return "" end
  local values={}
  for _,span in ipairs(spans) do values[#values+1]=span[1]..":"..span[2] end
  return "[[RIME_RS:"..table.concat(values,";").."]]"
end

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
  -- Standard nasal escape fills a missing key, never replaces an imported
  -- n/nn definition. It also gives arbitrary kana words a way to spell ん.
  if not root.n or not root.n["'"] then alphabet["'"] = true end
  return {root = root, alphabet = alphabet, case_sensitive = case_sensitive, rgt = rgt}
end

-- A reserved phrase is a whole-code shortcut, not a phonetic syllable.
-- Inside a longer word use its ordinary key-table reading, never the phrase's
-- surface (kr=から, ordinary kr=けつ: krdz must decode to けつだん).
local function composable_rule(rule, mapping, whole_code)
  if not rule or not mapping.fixed or not mapping.fixed[rule.input] or
     whole_code == rule.input then return rule, false end
  local normal = mapping.normal and mapping.normal[rule.input]
  local alternative = mapping.alternatives and mapping.alternatives[rule.input]
  if normal then
    return {input = rule.input, output = normal[1], pending = normal[2]}, true
  elseif alternative then
    return {input = rule.input, output = alternative, pending = ""}, true
  end
  return nil, true
end

function M.parse(code, mapping)
  code = code:gsub("%u", function(ch) return mapping.alphabet[ch] and ch or ch:lower() end)
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
    local reserved_without_reading
    best, reserved_without_reading = composable_rule(best, mapping, code)
    if not best and reserved_without_reading then break end
    if not best then
      if remaining == "n" or remaining:sub(1,2) == "n'" then
        output[#output + 1] = "ん"
        remaining = remaining == "n" and "" or remaining:sub(3)
        last_single = false
      else break end
    else
    output[#output + 1] = best.output
    last_single = #best.input == 1 and best.input:match("[b-df-hj-npr-tvw-yz]") ~= nil
    remaining = best.pending .. remaining:sub(length + 1)
    if #remaining > 4096 then return "", code, 0 end
    end
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

-- Each occurrence can choose its own suffix. Keep at most sixteen branches
-- so repeated multi-reading keys cannot create an unbounded candidate list.
function M.variant_readings(code, mapping)
  code = code:gsub("%u", function(ch) return mapping.alphabet[ch] and ch or ch:lower() end)
  local states, results, unique = {{rest = code, output = "", seen = {}, steps = 0}}, {}, {}
  local limit = math.max(256, #code * 8)
  while #states > 0 do
    local next_states = {}
    for _, state in ipairs(states) do
      if state.rest == "" then
        if not unique[state.output] and #results < 16 then
          unique[state.output] = true
          results[#results + 1] = state.output
        end
      elseif not state.seen[state.rest] and state.steps < limit then
        local node, best, length = mapping.root, nil, 0
        for i = 1, #state.rest do
          node = node[state.rest:sub(i, i)]
          if not node then break end
          if node.rule then best, length = node.rule, i end
        end
        if mapping.rgt and state.rest:sub(1, 1) == " " then
          if #next_states < 16 then next_states[#next_states + 1] = {rest = state.rest:sub(2), output = state.output, seen = state.seen, steps = state.steps + 1} end
        elseif best and not (mapping.rgt and node and length < #state.rest) then
          local ordinary, substituted = composable_rule(best, mapping, code)
          for _, reading in ipairs(ordinary and
              (not substituted and mapping.variants[best.input] or {ordinary.output}) or {}) do
            if #next_states < 16 then
              local seen = {}
              for k, v in pairs(state.seen) do seen[k] = v end
              seen[state.rest] = true
              local rest = ordinary.pending .. state.rest:sub(length + 1)
              if #rest <= 4096 then next_states[#next_states + 1] = {rest = rest, output = state.output .. reading, seen = seen, steps = state.steps + 1} end
            end
          end
        end
      end
    end
    states = next_states
  end
  return results
end

-- Experimental route: expand first, convert the complete reading once per
-- bounded variant, then accept only surfaces preserving reserved anchors.
-- No converted ordinary fragment is concatenated into a synthetic word.
local function whole_reserved_combinations(parts,mapping,convert,state_limit)
  state_limit=state_limit or 16
  local states={{reading='',pattern='^',spans={}}}
  for _,part in ipairs(parts) do
    local choices
    if part.literal then choices=part.literals or {part.literal}
    else
      local reading,pending=M.parse(part.code,mapping)
      if reading=='' or pending~='' then return {},0 end
      choices=M.variant_readings(part.code,mapping)
      if #choices==0 then choices={reading} end
    end
    local next_states={}
    for _,prefix in ipairs(states) do for _,text in ipairs(choices) do
      if #next_states<state_limit then
        local spans={};for _,span in ipairs(prefix.spans) do spans[#spans+1]=span end
        local pattern=prefix.pattern
        if part.literal then
          spans[#spans+1]={utf8.len(prefix.reading) or 0,utf8.len(text) or 0}
          pattern=pattern..'()'..text:gsub('([%^%$%(%)%%%.%[%]%*%+%-%?])','%%%1')..'()'
        else pattern=pattern..'.+' end
        next_states[#next_states+1]={reading=prefix.reading..text,pattern=pattern,spans=spans}
      end
    end end
    states=next_states
  end
  local results,seen={},{}
  for _,state in ipairs(states) do
    -- Literal reading remains an explicit fallback even if the model has no
    -- anchor-preserving proposal. It is not labelled as a model conversion.
    if not seen[state.reading] then
      local item={text=state.reading,reading=state.reading,spans=state.spans,whole=true}
      results[#results+1]=item
      seen[state.reading]=item
    end
    local count=0
    local values=convert(state.reading) or {}
    local best_preserved=values[1] and values[1]:match(state.pattern..'$')~=nil
    for _,text in ipairs(values) do
      local positions={text:match(state.pattern..'$')}
      if #positions>0 then
        local spans={}
        for i=1,#positions,2 do spans[#spans+1]={utf8.len(text:sub(1,positions[i]-1)) or 0,utf8.len(text:sub(positions[i],positions[i+1]-1)) or 0} end
        if seen[text] then
          seen[text].model=true;seen[text].best_preserved=best_preserved
        else
          local item={text=text,reading=state.reading,spans=spans,whole=true,model=true,best_preserved=best_preserved}
          results[#results+1]=item;seen[text]=item
        end
      end
      count=count+1;if count>=32 then break end
    end
  end
  return results,#states
end

-- Preserve the meaning of reserved pieces, converting only ordinary pieces.
-- This is a secondary route: けつだん -> 決断 ranks before から + だん/段/断.
local function reserved_parts(code,mapping)
  if #code>48 then return {},false end
  local parts, ordinary, reserved = {}, "", false
  local function flush()
    if ordinary ~= "" then parts[#parts + 1] = {code=ordinary};ordinary="" end
  end
  local pos=1
  while pos<=#code do
    local node,best,length=mapping.root,nil,0
    for index=pos,#code do
      node=node[code:sub(index,index)]
      if not node then break end
      if node.rule then best,length=node.rule,index-pos+1 end
    end
    if best and mapping.fixed and mapping.fixed[best.input] then
      flush();parts[#parts+1]={literal=best.output,literals=mapping.reserved_variants and mapping.reserved_variants[best.input]};reserved=true
    else ordinary=ordinary..code:sub(pos,pos+(best and length or 1)-1) end
    pos=pos+(best and length or 1)
  end
  flush()
  return parts,reserved
end

-- Complete reserved word-break paths, not just a greedy longest token. A
-- longer shortcut may consume the start of the next one (dsk+r vs ds+kr).
-- Ordinary spans are always validated by the current production parser.
-- This is bounded key parsing only: no dictionary/model/IPC work here.
function M.reserved_segmentations(code,mapping)
  if #code>48 then return {} end
  mapping.reserved_path_cache=mapping.reserved_path_cache or {}
  mapping.reserved_path_order=mapping.reserved_path_order or {}
  local cached=mapping.reserved_path_cache[code]
  if cached then return cached end
  local matches,has_fixed={},false
  for pos=1,#code do
    local node=mapping.root
    matches[pos]={}
    for index=pos,#code do
      node=node[code:sub(index,index)];if not node then break end
      if node.rule and mapping.fixed and mapping.fixed[node.rule.input] then
        table.insert(matches[pos],1,node.rule);has_fixed=true
      end
    end
  end
  local paths,parts,ordinary_cache,work={},{},{},0
  local function ordinary(text)
    if mapping.fixed and mapping.fixed[text] then return false end
    if ordinary_cache[text]~=nil then return ordinary_cache[text] end
    local reading,pending,consumed=M.parse(text,mapping)
    local valid=reading~='' and pending=='' and consumed==#text
    ordinary_cache[text]=valid;return valid
  end
  local function walk(pos,reserved)
    work=work+1
    if work>512 or #paths>=16 or #parts>12 then return end
    if pos>#code then
      if reserved then local copy={};for i,part in ipairs(parts) do copy[i]=part end;paths[#paths+1]=copy end
      return
    end
    for _,rule in ipairs(matches[pos]) do
      parts[#parts+1]={literal=rule.output,literals=mapping.reserved_variants and mapping.reserved_variants[rule.input]}
      walk(pos+#rule.input,true);parts[#parts]=nil
      if work>512 or #paths>=16 then return end
    end
    for next_pos=#code,pos+1,-1 do
      if #matches[next_pos]>0 then
        local text=code:sub(pos,next_pos-1)
        if ordinary(text) then
          parts[#parts+1]={code=text};walk(next_pos,reserved);parts[#parts]=nil
          if work>512 or #paths>=16 then return end
        end
      end
    end
    local tail=code:sub(pos)
    if ordinary(tail) then
      parts[#parts+1]={code=tail};walk(#code+1,reserved);parts[#parts]=nil
    end
  end
  if has_fixed then walk(1,false) end
  mapping.reserved_path_cache[code]=paths
  table.insert(mapping.reserved_path_order,code)
  if #mapping.reserved_path_order>128 then
    mapping.reserved_path_cache[table.remove(mapping.reserved_path_order,1)]=nil
  end
  return paths
end

function M.reserved_combinations(code, mapping, convert, context_convert)
  if #code > 48 or (mapping.fixed and mapping.fixed[code]) then return {} end
  local parts,reserved=reserved_parts(code,mapping)
  if mapping.reserved_whole then
    local results,seen,queries,budget={},{},{},16
    local function query(reading)
      if queries[reading]==nil then queries[reading]=(context_convert or convert)(reading) or false end
      return queries[reading] or {}
    end
    -- Keep a complete longest-match interpretation unchanged. Search shorter
    -- boundaries only when it dead-ends; otherwise dsn could become ds+n and
    -- steal a slot from the existing ですね candidates.
    local complete=reserved and #parts<=12
    for _,part in ipairs(parts) do
      if part.code then
        local reading,pending,consumed=M.parse(part.code,mapping)
        if reading=='' or pending~='' or consumed~=#part.code then complete=false;break end
      end
    end
    local paths=complete and {parts} or M.reserved_segmentations(code,mapping)
    for _,path in ipairs(paths) do
      if budget<=0 then break end
      local values,used=whole_reserved_combinations(path,mapping,query,budget)
      budget=budget-used
      for _,item in ipairs(values) do
        if not seen[item.text] then results[#results+1]=item;seen[item.text]=true end
      end
    end
    -- Keep the existing productive clock feature in both modes. This is an
    -- explicitly tagged grammar supplement, not a fabricated Mozc surface.
    local ending=parts[#parts] and parts[#parts].literal
    if #parts==2 and parts[1].code and (ending=='まで' or ending=='から') then
      local count=#results
      local seen={};for _,item in ipairs(results) do seen[item.text]=true end
      for i=1,count do
        local reading=results[i].reading
        local clock=require('japanese_candidate_grammar').clock_expression(reading)
        if clock then
          for _,item in ipairs(results) do if item.text==clock then item.clock=true end end
        end
        if clock and not seen[clock] then
          results[#results+1]={text=clock,reading=reading,whole=true,clock=true,
            spans={{(utf8.len(clock) or 0)-(utf8.len(ending) or 0),utf8.len(ending) or 0}}}
          seen[clock]=true
        end
      end
    end
    return results
  end
  if not reserved or #parts>12 then return {} end
  local results={{text="",reading="",spans={}}}
  local anchor,context_reading,has_ordinary="^","",false
  for _,part in ipairs(parts) do
    local reading, choices
    if part.literal then
      reading,choices=part.literal,part.literals or {part.literal}
      anchor=anchor.."()"..part.literal:gsub("([%^%$%(%)%%%.%[%]%*%+%-%?])","%%%1").."()"
    else
      has_ordinary=true;anchor=anchor..".+"
      local pending
      reading,pending=M.parse(part.code,mapping)
      if reading=="" or pending~="" then return {} end
      choices={reading}
      local seen={[reading]=true}
      for _,text in ipairs(convert(reading) or {}) do
        if not seen[text] then choices[#choices+1]=text;seen[text]=true end
        if #choices>=16 then break end
      end
    end
    context_reading=context_reading..reading
    local next_results={}
    for _,prefix in ipairs(results) do
      for _,text in ipairs(choices) do
        if #next_results<16 then
          local spans={};for _,span in ipairs(prefix.spans) do spans[#spans+1]=span end
          if part.literal then spans[#spans+1]={utf8.len(prefix.text) or 0,utf8.len(text) or 0} end
          next_results[#next_results+1]={text=prefix.text..text,
            reading=prefix.reading..(part.literal and text or reading),spans=spans}
        end
      end
    end
    results=next_results
  end
  -- Context can reveal suffix homophones absent from standalone conversion.
  -- Treat reserved pieces as immutable anchors: a contextual proposal may
  -- fill ordinary slots, but may never replace/convert a reserved surface.
  if context_convert and has_ordinary then
    local seen={}
    for _,item in ipairs(results) do seen[item.text]=true end
    for _,text in ipairs(context_convert(context_reading) or {}) do
      local positions={text:match(anchor.."$")}
      if #positions>0 and not seen[text] then
        local spans={}
        for i=1,#positions,2 do spans[#spans+1]={utf8.len(text:sub(1,positions[i]-1)) or 0,utf8.len(text:sub(positions[i],positions[i+1]-1)) or 0} end
        results[#results+1]={text=text,reading=context_reading,spans=spans};seen[text]=true
      end
      if #results>=32 then break end
    end
  end
  -- Only suffix reserved anchors may enable the productive clock grammar.
  -- Keep the literal/kana and dictionary candidates; add, do not replace.
  local ending=parts[#parts] and parts[#parts].literal
  if #parts==2 and parts[1].code and (ending=='まで' or ending=='から') then
    local clock=require('japanese_candidate_grammar').clock_expression(context_reading)
    if clock then
      local found=false
      for _,item in ipairs(results) do if item.text==clock then found=true;item.clock=true end end
      if not found then results[#results+1]={text=clock,reading=context_reading,clock=true,
        spans={{(utf8.len(clock) or 0)-(utf8.len(ending) or 0),utf8.len(ending) or 0}}} end
    end
  end
  return results
end

local function table_for(context)
  local generation = context:get_property("japanese_input_table_generation") or ""
  if loaded_table and generation == loaded_generation then return loaded_table end
  local rules, rgt, alternatives, fixed, normal, key_actions, variants, reserved_variants = {}, false, {}, {}, {}, {}, {}, {}
  local empty_first, key_tails, single_tail, explicit_key_metadata = {}, {}, false, false
  local reserved_whole=false
  local file = io.open(rime_api.get_user_data_dir() .. "/japanese_input_table.tsv", "r")
  if file then
    for line in file:lines() do
      line = line:gsub("\r$", "")
      local action_key, action = line:match("^# zhongri%-key%-action: (%d+)=(%a+)$")
      if action_key then key_actions[tonumber(action_key)] = action end
      local empty=line:match('^# zhongri%-key%-empty%-first: (%d+)$')
      if empty then empty_first[tonumber(empty)]=true end
      local tail_key,tails=line:match('^# zhongri%-key%-tails: (%d+)=(.+)$')
      if tail_key then
        key_tails[tonumber(tail_key)]={}
        for tail in tails:gmatch('[^/]+') do table.insert(key_tails[tonumber(tail_key)],tail) end
      end
      if line=='# zhongri-single-key-tail: 1' then single_tail=true end
      if line=='# zhongri-reserved-whole: 1' then reserved_whole=true end
      if line:match('^# zhongri%-single%-key%-tail: [01]$') then explicit_key_metadata=true end
      local variant_key, readings = line:match("^# zhongri%-key%-variants: ([^=]+)=(.+)$")
      if variant_key then
        variants[variant_key] = {}
        for reading in readings:gmatch("[^/]+") do variants[variant_key][#variants[variant_key] + 1] = reading end
      end
      if line == "# zhongri-mode: rgt-hpk-2018" then rgt = true end
      local fixed_key = line:match("^# zhongri%-fixed: ([a-z0-9]+)$")
      if fixed_key then fixed[fixed_key] = true end
      local reserved_key,reserved_readings=line:match("^# zhongri%-reserved%-variants: ([a-z0-9]+)=(.+)$")
      if reserved_key then
        reserved_variants[reserved_key]={}
        for reading in reserved_readings:gmatch("[^/]+") do reserved_variants[reserved_key][#reserved_variants[reserved_key]+1]=reading end
      end
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
  loaded_table.key_actions = key_actions
  loaded_table.variants = variants
  loaded_table.reserved_variants = reserved_variants
  loaded_table.empty_first,loaded_table.key_tails,loaded_table.single_tail=empty_first,key_tails,single_tail
  loaded_table.explicit_key_metadata=explicit_key_metadata
  loaded_table.reserved_whole=reserved_whole
  loaded_generation = generation
  return loaded_table
end

-- Input intent is available without pulling late candidates or querying a
-- model. Reuse the exact same segmentation as combination generation.
function M.has_reserved_input(code,context)
  local _,reserved=reserved_parts(code,table_for(context))
  return reserved
end

local function empty_prefix(code,mapping)
  if #code~=1 then return false end
  -- Modern tables explicitly record EmptyFirst, including its absence for H.
  -- The legacy uppercase guess must never override authoritative definitions.
  return mapping.empty_first[string.byte(code)] or
      (not mapping.explicit_key_metadata and code:match('%u') and
       not mapping.root[code].rule and mapping.key_actions[string.byte(code)]=='input')
end

function M.single_key_choices(code,mapping)
  local node=#code==1 and mapping.root[code]
  if not node or node.rule or empty_prefix(code,mapping) then return {} end
  local result,seen={},{}
  local function add(reading,suffix)
    if reading~='' and not seen[reading] and #result<8 then
      seen[reading]=true;result[#result+1]={reading=reading,suffix=suffix}
    end
  end
  if mapping.single_tail then
    for _,reading in ipairs(mapping.key_tails[string.byte(code)] or {}) do add(reading,'') end
  else
    -- One bounded table layer, not a dictionary/model scan per incomplete key.
    local keys={};for key in pairs(node) do if key~='rule' then keys[#keys+1]=key end end
    table.sort(keys)
    for _,key in ipairs(keys) do
      local rule=node[key].rule
      if rule and rule.pending=='' and not mapping.fixed[rule.input] then add(rule.output,key) end
    end
  end
  return result
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
  if mapping.fixed[code] and pending == "" then return kana end
  if mapping.normal[code] then kana, pending = table.unpack(mapping.normal[code]) end
  return pending == "" and kana or nil
end

-- Fuzzy matching operates on decoded Japanese, never on the physical keys.
-- Keep the canonical spelling independent of each imported table's aliases.
local fuzzy_romaji = {}
for _, row in ipairs({{"", "あいうえお"}, {"k", "かきくけこ"}, {"g", "がぎぐげご"},
    {"s", "さしすせそ"}, {"z", "ざじずぜぞ"}, {"t", "たちつてと"}, {"d", "だぢづでど"},
    {"n", "なにぬねの"}, {"h", "はひふへほ"}, {"b", "ばびぶべぼ"}, {"p", "ぱぴぷぺぽ"},
    {"m", "まみむめも"}, {"r", "らりるれろ"}}) do
  local index = 0
  for _, cp in utf8.codes(row[2]) do
    index = index + 1
    fuzzy_romaji[utf8.char(cp)] = row[1] .. ("aiueo"):sub(index, index)
  end
end
for _, pair in ipairs({{"き", "ky"}, {"ぎ", "gy"}, {"し", "sh"}, {"じ", "j"}, {"ち", "ch"},
    {"に", "ny"}, {"ひ", "hy"}, {"び", "by"}, {"ぴ", "py"}, {"み", "my"}, {"り", "ry"}}) do
  for index, cp in ipairs({0x3083, 0x3085, 0x3087}) do
    fuzzy_romaji[pair[1] .. utf8.char(cp)] = pair[2] .. ("auo"):sub(index, index)
  end
end
for kana, roman in pairs({["し"]="shi", ["ち"]="chi", ["つ"]="tsu", ["ふ"]="fu", ["じ"]="ji",
    ["や"]="ya", ["ゆ"]="yu", ["よ"]="yo", ["わ"]="wa", ["を"]="wo", ["ん"]="n", ["ー"]="q",
    ["ふぁ"]="fa", ["ふぃ"]="fi", ["ふぇ"]="fe", ["ふぉ"]="fo", ["てぃ"]="ti", ["でぃ"]="di",
    ["うぃ"]="wi", ["うぇ"]="we", ["うぉ"]="wo", ["しぇ"]="she", ["ちぇ"]="che", ["じぇ"]="je",
    ["ゔ"]="vu", ["ゔぁ"]="va", ["ゔぃ"]="vi", ["ゔぇ"]="ve", ["ゔぉ"]="vo"}) do fuzzy_romaji[kana] = roman end

function M.fuzzy_spelling(code, context)
  if not enabled(context) then return nil end
  local mapping = table_for(context)
  local kana, pending = M.parse(code, mapping)
  if mapping.normal[code] then kana, pending = table.unpack(mapping.normal[code])
  elseif mapping.fixed[code] and mapping.alternatives[code] then kana = mapping.alternatives[code] end
  if pending ~= "" or kana == "" then return nil end
  local chars = {}
  for _, cp in utf8.codes(kana) do
    if cp >= 0x30a1 and cp <= 0x30f6 then cp = cp - 0x60 end
    chars[#chars + 1] = utf8.char(cp)
  end
  local output, index, sokuon = {}, 1, false
  while index <= #chars do
    if chars[index] == "っ" then sokuon = true; index = index + 1
    else
      local roman = fuzzy_romaji[chars[index] .. (chars[index + 1] or "")]
      local length = roman and 2 or 1
      roman = roman or fuzzy_romaji[chars[index]]
      if not roman then return nil end
      if sokuon then
        if not roman:match("^[bcdfghjklmprstvwxyz]") or roman == "q" then return nil end
        output[#output + 1] = roman:sub(1, 1); sokuon = false
      end
      output[#output + 1] = roman; index = index + length
    end
  end
  if sokuon then return nil end
  return table.concat(output), kana
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
  local choices={}
  for _,text in ipairs(mapping.reserved_variants[code] or {kana}) do choices[#choices+1]=text end
  local ordinary=normal and normal[2]=="" and normal[1]~="" and normal[1] or (not normal and mapping.alternatives[code] or nil)
  if ordinary then choices[#choices+1]=ordinary end
  return choices
end

function M.processor(key, env)
  local editing=require('japanese_segment_edit').processor(key,env,function(code,context)
    local selected=context:get_selected_candidate()
    local comment=selected and selected.comment or ''
    -- Respect the selected Japanese route's full reading (including enabled
    -- reserved meanings), never a Chinese candidate's translated annotation.
    local reading=comment:find('[[RIME_LANG:JA]]',1,true) and comment:match('%[%[RIME_JR:(.-)%]%]')
    return reading or M.cloud_reading(code,context)
  end)
  if editing then return editing end
  local context = env.engine.context
  if key:release() or key:ctrl() or key:alt() or key:super() or
     context:get_option("ascii_mode") then
    return 2
  end
  local keycode = key.keycode
  local custom_mapping = enabled(context) and table_for(context) or nil
  local custom_action = custom_mapping and custom_mapping.key_actions[keycode]
  if custom_action and keycode >= 33 and keycode <= 126 then
    local ch = string.char(keycode)
    if custom_action == "input" then
      -- An explicit Shift-letter action must never fall back to the lower key,
      -- even when its mappings were all cleared and no uppercase rules remain.
      if not custom_mapping.case_sensitive and not ch:match("%u") then ch = ch:lower() end
      if env.rgt_generation ~= loaded_generation or env.rgt_input ~= context.input then env.rgt_at = nil end
      local now = os.clock()
      local _, _, _, last_single = M.parse(context.input or "", custom_mapping)
      if custom_mapping.rgt and env.rgt_at and now >= env.rgt_at and
         now - env.rgt_at >= 0.300 and last_single then context:push_input(" ") end
      if custom_mapping.alphabet[ch] then context:push_input(ch) end
      env.rgt_at, env.rgt_input, env.rgt_generation = now, context.input, loaded_generation
      return 1
    end
    -- Paging is handled by the native renderer when a menu exists. With no
    -- menu these keys stay usable as punctuation. Direct-output keys commit
    -- the selected composition first, then their configured literal text.
    if custom_action == "literal" or custom_action == "next" or custom_action == "previous" then
      if context:is_composing() then
        if context:get_selected_candidate() then context:commit()
        else
          env.engine:commit_text(context.input or "")
          context:clear()
        end
      end
      local node = custom_mapping.root[ch]
      local rule = node and node.rule
      env.engine:commit_text(custom_action == "literal" and rule and rule.pending == "" and rule.output or ch)
      return 1
    end
  end
  -- The generic uppercase recognizer accepts digits before Rime's selector
  -- runs (Asi + 1 became Asi1). Handle visible menu ordinals here, ahead of
  -- that recognizer, while keeping explicitly assigned numeric table codes.
  if custom_mapping and not key:shift() and keycode >= 0x31 and keycode <= 0x39 and context:has_menu() then
    local ch = string.char(keycode)
    local numeric_input = false
    if custom_mapping.alphabet[ch] then
      local kana, pending = M.parse((context.input or "") .. ch, custom_mapping)
      numeric_input = custom_mapping.root[ch] ~= nil or (kana ~= "" and pending == "")
    end
    if not numeric_input then
      local segment = context.composition:back()
      local size = math.max(1, env.engine.schema.config:get_int("menu/page_size") or 9)
      local selected = segment and segment.selected_index or 0
      local index = math.floor(selected / size) * size + keycode - 0x31
      if segment and segment.menu and segment.menu:get_candidate_at(index) then context:select(index) end
      -- Missing ordinals are consumed too; they must not corrupt the spelling.
      return 1
    end
  end
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
  -- Keep the actual sokuon key in composition. Expanding it to "ltu" leaked
  -- the internal spelling into preedit and exposed unrelated Chinese words
  -- for ltu. Direct push bypasses the speller; our table segmentor decodes ;.
  if keycode == 0x3b and enabled(context) then
    local node = table_for(context).root[";"]
    local rule = node and node.rule
    if rule ~= nil and rule.output == "っ" and rule.pending == "" then
      -- A bare semicolon is punctuation. Sokuon is only meaningful inside
      -- the current alphabetic composition, never at an empty input boundary.
      if not context:is_composing() or not (context.input or ""):find("[a-zA-Z]") then
        return commit_symbol()
      end
      context:push_input(";")
      env.rgt_at, env.rgt_input = nil, context.input
      return 1
    end
  end
  -- Keep the nasal escape inside composition rather than letting the generic
  -- punctuation path commit the preceding kana and a literal apostrophe.
  if keycode == 0x27 and enabled(context) and context:is_composing() and
     (context.input or ""):sub(-1) == "n" then
    local nasal = table_for(context).root.n
    if not nasal or (not nasal.rule and not nasal["'"]) then
      context:push_input("'")
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
  -- Punctuation confirms the visible selection, not another language's item.
  -- Keep the Japanese pending-key completion path for unfinished table input.
  if keycode == 0x2e or keycode == 0x2c then
    local punctuation_key = string.char(keycode)
    local expected_punctuation = keycode == 0x2e and "。" or "、"
    local rule = mapping.root[punctuation_key] and mapping.root[punctuation_key].rule
    local selected = context:get_selected_candidate()
    local input = context.input or ""
    local selected_japanese = selected and (selected.type == "japanese_input_table" or
      (selected.comment or ""):find("[[RIME_LANG:JA]]", 1, true))
    if rule and rule.output == expected_punctuation and rule.pending == "" and
       context:is_composing() and selected and selected.start == 0 and
       selected._end == #input and not selected_japanese then
      -- Mixed input can have Chinese first even when its romaji is also valid
      -- Japanese. Never scan past that selection and commit the Japanese item.
      -- Use normal context commit so Chinese learning/notifiers are preserved.
      context:commit()
      env.engine:commit_text(context:get_option("ascii_punct") and punctuation_key or
        (keycode == 0x2e and "。" or "，"))
      return 1
    end
    -- Respect imported tables that use these keys for non-punctuation codes.
    if rule and rule.output == expected_punctuation and rule.pending == "" and
       not context:is_composing() then
      env.engine:commit_text(rule.output)
      return 1
    end
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
      if candidate then require("japanese_learning_commit").capture(context, candidate) end
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
  if mapping.alphabet[ch] or (ch:match("%u") and mapping.alphabet[ch:lower()]) or (context:get_option("japanese_input_table_mixed") and ch:match("[a-zA-Z]")) then
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
  local code = segmentation.input:sub(start + 1)
  local mapping = table_for(context)
  local node = #code == 1 and mapping.root[code] or nil
  -- An explicitly assigned prefix-only key (e.g. empty-first A) is waiting
  -- for its second key, not an ordinary English/Chinese spelling. Do not run
  -- every abc dictionary against that intentionally incomplete prefix.
  local waiting_prefix = node and not node.rule and empty_prefix(code,mapping)
  if context:get_option("japanese_input_table_mixed") and not waiting_prefix then segment.tags = Set({"japanese_input_table", "abc"}) end
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
  local combination_only=mapping.reserved_whole and not reserved and (kana=='' or consumed==0) and
      #M.reserved_segmentations(input,mapping)>0
  if not reserved and (kana == "" or consumed == 0) and not combination_only then
    local routing=require('mozc_v2_translator')
    for _,choice in ipairs(M.single_key_choices(input,mapping)) do
      local values=mapping.single_tail and routing.query_kana(choice.reading) or nil
      local text=values and values[1] or choice.reading
      local candidate=Candidate('japanese_input_table_prediction',seg.start,seg._end,text,
          '[[RIME_LANG:JA]][[RIME_JR:'..choice.reading..']][JP:PREFIX]'..
          (choice.suffix~='' and '~'..choice.suffix or ''))
      candidate.preedit=input;candidate.quality=99900;yield(candidate)
    end
    return
  end
  local routing = require("mozc_v2_translator")
  local candidates = kana ~= "" and consumed > 0 and routing.query_kana(kana) or {}
  local best_evidence = candidates[1] and routing.sentence_evidence(kana,candidates[1])
  local grammar_supported = routing.lexical_kana(kana) or (best_evidence and
      best_evidence.unknown==0 and best_evidence.cost<=4000+(utf8.len(kana) or 0)*1500)
  local grammatical
  local grammar=require('japanese_candidate_grammar')
  candidates,grammatical = grammar.rank(kana,candidates,
      function(text) return routing.predicted_reading(kana,text) end,routing.lexical_kana(kana))
  local seen = {}
  local comment = "[[RIME_LANG:JA]][[RIME_JR:" .. kana .. "]]"
  local function emit(text, reading, span, reserved_glyphs)
    if text == "" or seen[text] then return end
    seen[text] = true
    local predicted = routing.predicted_reading(reading or kana, text)
    reading = predicted or reading
    local candidate = Candidate(predicted and "japanese_input_table_prediction" or "japanese_input_table", seg.start,
                                seg.start + (span or consumed), text,
                                reading and "[[RIME_LANG:JA]][[RIME_JR:" .. reading .. "]]" or comment)
    local evidence = routing.sentence_evidence(reading or kana, text)
    if not predicted and routing.lexical_kana(reading or kana) then
      candidate.comment=candidate.comment..'[[RIME_JA_LEXICAL]]'
    end
    -- Full dictionary readings retain their ordinary priority (日本 must not
    -- become 二本 merely for having a counter). For productive non-lexical
    -- readings, an exact whole quantity is stronger than kana/partial forms.
    if not predicted and not reserved_glyphs and not routing.lexical_kana(reading or kana) and
        grammar.counter_surface(text) then
      candidate.comment=candidate.comment..'[[RIME_JA_COUNTER]]'
    end
    -- Mechanical identity spellings remain available, but must not compete
    -- with meaningful conversions. Genuine kana words and fixed shortcuts
    -- are exempt; a character-script heuristic alone is not lexical evidence.
    local same_reading=not reading or reading==kana
    local preferred=same_reading and candidates[1] or nil
    local converted_first=preferred and preferred~=text and not routing.predicted_reading(reading or kana,preferred)
    if not predicted and not reserved_glyphs and text ~= reserved and
       (text==(reading or kana) or text==katakana(reading or kana)) and
       (converted_first or (not routing.lexical_kana(reading or kana) and not grammatical[text] and
       not (grammar_supported and same_reading))) then
      candidate.comment=candidate.comment..'[[RIME_JA_KANA_FALLBACK]]'
    end
    if evidence and evidence.unknown == 0 then
      -- Model cost per kana is bounded ranking evidence, not a probability.
      -- Only the best complete path gets it, never all n-best concatenations.
      local length = utf8.len(reading or kana) or 0
      -- BOS/EOS and word-boundary costs have a fixed component. A purely
      -- per-kana cutoff rejected short ordinary sentences like もう終わりか.
      if length >= 3 and evidence.cost <= 4000 + length * 1500 then
        candidate.comment = candidate.comment .. "[[RIME_JA_SENTENCE]]"
      end
    end
    if not predicted and text ~= (reading or kana) and text ~= katakana(reading or kana) and
       not routing.lexical_kana(reading or kana) and text ~= reserved then
      candidate.comment = candidate.comment .. "[[RIME_JA_ASSEMBLED]]"
    end
    if (grammar_supported or grammar.supports_particle(kana,text)) and not predicted and (not reading or reading==kana) and grammatical[text] then
      candidate.comment=candidate.comment..'[[RIME_JA_GRAMMAR]]'
    end
    candidate.preedit = context:get_option("japanese_input_table_show_letters") and input or
        (reading and reading ~= reserved and reading or kana) .. pending
    candidate.quality = predicted and 99990 or 100000
    if reserved_glyphs then candidate.comment=candidate.comment..reserved_span_tag({{0,utf8.len(text) or 0}}) end
    yield(candidate)
  end
  if reserved then
    for _,text in ipairs(mapping.reserved_variants[input] or {reserved}) do emit(text,text,#input,true) end
    -- Fixed kana phrases are also valid conversion readings. A separate
    -- editable first/second-key fallback must not replace their reading (e.g.
    -- a よろしくおねがいします shortcut still needs よろしくお願いします).
    local kana_phrase = (utf8.len(reserved) or 0) >= 3
    for _, cp in utf8.codes(reserved) do
      if not ((cp >= 0x3041 and cp <= 0x3096) or cp == 0x30fc) then kana_phrase = false;break end
    end
    if kana_phrase then
      for _, reading in ipairs(mapping.reserved_variants[input] or {reserved}) do
        for _, text in ipairs(routing.query_kana(reading) or {}) do emit(text,reading,#input,true) end
      end
    end
    if pending == "" and kana ~= "" then emit(kana) end
  end
  if (kana == "" or consumed == 0) and not combination_only then return end
  local variant_readings = next(mapping.variants) and M.variant_readings(input, mapping) or {}
  if #variant_readings > 1 then
    emit(kana)
    for _, reading in ipairs(variant_readings) do emit(reading, reading, #input) end
  end
  for _, text in ipairs(candidates) do emit(text) end
  emit(kana)
  emit(katakana(kana))
  -- Enumerate alternate suffix readings through the same longest-match parser,
  -- including inside longer compositions. Bound the cross-product per input.
  if #variant_readings > 1 then
    for _, reading in ipairs(variant_readings) do
      if reading ~= kana then
        emit(reading, reading, #input)
        for _, text in ipairs(routing.query_kana(reading) or {}) do emit(text, reading, #input) end
        emit(katakana(reading), reading, #input)
      end
    end
  end
  -- Normal conversion is primary; semantic combinations keep reserved text
  -- immutable. Contextual suffix proposals are anchor-checked, so 体ん cannot
  -- replace から + だん even if Mozc proposes it for the combined pronunciation.
  local function context_choices(reading)
    if mapping.reserved_whole then return routing.query_native_kana(reading) or {} end
    local choices={}
    for _,text in ipairs(routing.query_kana(reading) or {}) do
      if not routing.predicted_reading(reading,text) then choices[#choices+1]=text end
    end
    return choices
  end
  local combination_support={}
  for _,combination in ipairs(M.reserved_combinations(input,mapping,routing.query_kana,context_choices)) do
    if not seen[combination.text] then
      seen[combination.text]=true
      local metadata="[[RIME_LANG:JA]][[RIME_JA_RESERVED_COMBINATION]]"..reserved_span_tag(combination.spans)
      if combination.whole then metadata=metadata..'[[RIME_JA_RESERVED_WHOLE]]' end
      local kana_reading=true
      for _,cp in utf8.codes(combination.reading) do
        if not ((cp>=0x3041 and cp<=0x3096) or (cp>=0x30a1 and cp<=0x30fa) or cp==0x30fc) then kana_reading=false;break end
      end
      if kana_reading then
        metadata=metadata.."[[RIME_JR:"..combination.reading.."]]"
        local support=combination_support[combination.reading]
        if not support then
          local values=(combination.whole and routing.query_native_kana(combination.reading) or routing.query_kana(combination.reading)) or {}
          local first=values[1] and routing.sentence_evidence(combination.reading,values[1])
          local known=routing.lexical_kana(combination.reading) or
              (first and first.unknown==0 and first.cost<=4000+(utf8.len(combination.reading) or 0)*1500)
          support={}
          if combination.whole then
            -- A native candidate carries its own source/rank/cost. Never
            -- borrow the mixed decoder's first-path score for its siblings.
            local best=values[1] and routing.native_evidence(combination.reading,values[1])
            for _,text in ipairs(values) do
              local own=routing.native_evidence(combination.reading,text)
              if own and best and (own.rank==0 or (own.segments==1 and best.cost>=0 and own.cost>=0 and own.cost<=best.cost+3000)) then
                support[text]=true
              end
            end
            support.preferred=values[1]
            support.expression=best and best.segments==1 and best.cost>=0 and
                best.cost<=4000+(utf8.len(combination.reading) or 0)*1500 and
                values[1]==combination.reading
          elseif known then
            support[combination.reading]=true
            for _,text in ipairs(values) do
              if not routing.predicted_reading(combination.reading,text) then support[text]=true end
            end
            -- Respect attested surface order, not reading-first enumeration.
            -- Genuine kana expressions such as ようこそ remain preferred.
            support.preferred=values[1]
            support.expression=routing.lexical_kana(combination.reading)
          end
          combination_support[combination.reading]=support
        end
        local supported=support[combination.text] and (not combination.whole or (combination.model and combination.best_preserved))
        if combination.whole and not supported and not combination.clock then
          metadata=metadata..'[[RIME_JA_RESERVED_UNSUPPORTED]]'
        end
        if supported or combination.clock then
          metadata=metadata..'[[RIME_JA_RESERVED_SUPPORTED]]'
          if combination.whole and supported then metadata=metadata..'[[RIME_JA_MOZC_NATIVE]]' end
          if support.preferred==combination.text or combination.clock then metadata=metadata..'[[RIME_JA_RESERVED_PREFERRED]]' end
          if (combination.text==combination.reading or combination.text==katakana(combination.reading)) and
              support.preferred~=combination.text then
            metadata=metadata..'[[RIME_JA_RESERVED_IDENTITY]]'
          end
          local polite=false
          for _,span in ipairs(combination.spans) do
            if span[1]+span[2]==(utf8.len(combination.text) or 0) then
              local at=utf8.offset(combination.text,span[1]+1)
              local ending=at and combination.text:sub(at) or ''
              for _,suffix in ipairs({'です','ですか','ですね','ですよ','ます','ますか','ますね','ますよ','ません','ませんか','ませんでした'}) do
                if ending==suffix then polite=true;break end
              end
            end
          end
          if support.expression or polite or combination.clock then metadata=metadata..'[[RIME_JA_RESERVED_EXPRESSION]]' end
          if combination.clock then metadata=metadata..'[[RIME_JA_RESERVED_CLOCK]]' end
        end
      end
      local candidate=Candidate("japanese_reserved_combination",seg.start,seg.start+#input,
          combination.text,metadata)
      candidate.preedit=input
      candidate.quality=99900
      yield(candidate)
    end
  end
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
    local window = #(env.engine.context.input or '') == 1 and 32 or 64
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
      if count > window then yield(cand)
      else
      local is_japanese = cand.type == "japanese_input_table" or cand.type == "japanese_input_table_fuzzy" or cand.type == "japanese_input_table_prediction" or cand.type == "japanese_reserved_combination"
      if first_japanese == nil then first_japanese = is_japanese end
      local list = is_japanese and japanese or other
      list[#list + 1] = cand
      if count == window then flush() end
      end
    end
    if count < window then flush() end
    return
  end
  for cand in input:iter() do
    if cand.type == "japanese_input_table" or cand.type == "japanese_input_table_fuzzy" or cand.type == "japanese_input_table_prediction" or cand.type == "japanese_reserved_combination" then yield(cand) end
  end
end

return M
