-- Runtime Chinese spelling rules. No dictionary/prism rebuild on edits.
local M = {}
local function compact(s) return (s or ''):lower():gsub("[%s']+",''):gsub('nve','nue'):gsub('lve','lue') end
local function read(name)
  local f=io.open(rime_api.get_user_data_dir()..'/'..name,'r')
  if not f then return nil end
  local text=f:read('*a'); f:close(); return text
end
local function state(env)
  local generation=env.engine.context:get_property('chinese_correction_generation') or ''
  if env.chinese_hot and env.chinese_hot.generation==generation then return env.chinese_hot end
  local payload=read('chinese_correction_rules.tsv')
  -- First-run compatibility: a fresh user may edit a custom rule before ever
  -- opening the built-in correction page. Seed logically from the existing
  -- source flags without writing user files or rebuilding the prism.
  if not payload then
    local source=read('rime_ice_japanese.schema.yaml')
    local rows,inside={},false
    for line in (source or ''):gmatch('[^\r\n]+') do
      if line:find('### 自动纠错',1,true) then inside=true
      elseif inside and line:match('^menu:') then break
      elseif inside then
        local rule=line:match('(derive/.+/)%s*$')
        if rule then rows[#rows+1]=(line:find('CF_OFF',1,true) and '0' or '1')..'\t'..rule end
      end
    end
    if #rows>0 then payload=table.concat(rows,'\n') end
  end
  local value={generation=generation,enabled=payload~=nil,map={},custom={},keys={}}
  env.chinese_hot=value
  if not payload then return value end
  local initials={'','b','p','m','f','d','t','n','l','g','k','h','j','q','x','zh','ch','sh','r','z','c','s','y','w'}
  local finals={'a','o','e','ai','ei','ao','ou','an','en','ang','eng','ong','i','ia','ie','iao','iu','ian','in','iang','ing','iong','u','ua','uo','uai','ui','uan','un','uang','ueng','ue','v','ve','van','vn','er'}
  for line in payload:gmatch('[^\r\n]+') do
    local on,rule=line:match('^([01])\t(derive/.+/)$')
    if on=='1' then
      local expression=rule:gsub('^derive/','xform/')
      local ok,projection=pcall(Projection,{expression})
      if ok then
        for _,initial in ipairs(initials) do for _,final in ipairs(finals) do
          local canonical=initial..final
          local success,typed=pcall(function() return projection:apply(canonical,true) end)
          if success and typed and typed~=canonical and typed:match('^[a-z]+$') and #typed<=12 then
            value.map[typed]=value.map[typed] or {}
            value.map[typed][canonical]=true
          end
        end end
      end
    end
  end
  for line in (read('custom_chinese_fuzzy.tsv') or ''):gmatch('[^\r\n]+') do
    local on,left,right=line:match('^([01])\t([a-z]+)\t([a-z]+)')
    if on=='1' and left~=right and #left<=16 and #right<=16 then
      value.custom[#value.custom+1]={left,right};value.custom[#value.custom+1]={right,left}
    end
  end
  for key in pairs(value.map) do value.keys[#value.keys+1]=key end
  table.sort(value.keys,function(a,b) return #a==#b and a<b or #a>#b end)
  return value
end
local function pair_allowed(raw,canonical,s,completion)
  raw=compact(raw);canonical=compact(canonical)
  if raw==canonical or ((completion or #raw==1 or raw=='zh' or raw=='ch' or raw=='sh') and canonical:sub(1,#raw)==raw) then return true end
  if s.map[raw] and s.map[raw][canonical] then return true end
  for _,rule in ipairs(s.custom) do
    local position=canonical:find(rule[1],1,true)
    while position do
      if canonical:sub(1,position-1)..rule[2]..canonical:sub(position+#rule[1])==raw then return true end
      position=canonical:find(rule[1],position+1,true)
    end
  end
  return false
end
function M.canonical(cand,env)
  local ok,reading,chinese=pcall(function()
    local dictionary=env.engine.schema.config:get_string('translator/dictionary') or ''
    if dictionary:find('japanese',1,true) then return nil,false end
    local phrase=cand:get_genuine():to_phrase()
    if not phrase then return nil,false end
    env.chinese_hot_memory=env.chinese_hot_memory or Memory(env.engine,env.engine.schema,'translator')
    if phrase.lang_name~=env.chinese_hot_memory.lang_name then return nil,false end
    return table.concat(env.chinese_hot_memory:decode(phrase.code),' '),true
  end)
  if ok then return reading,chinese end
  return nil,nil
end
function M.allow_candidate(cand,env)
  if cand.type~='phrase' and cand.type~='user_phrase' and cand.type~='sentence' and cand.type~='completion' then return true end
  local s=state(env);if not s.enabled then return true end
  local original,chinese=M.canonical(cand,env)
  if chinese==false then return true end
  original=original or (cand.comment or ''):gsub('^［',''):gsub('］$','')
  if original=='' or not original:match("^[a-z ']+$") then return true end
  local raw=cand.preedit or ''
  if pair_allowed(compact(raw),compact(original),s,cand.type=='completion') then return true end
  local a,b={},{}
  for token in raw:gmatch('[a-z]+') do a[#a+1]=token end
  for token in original:gmatch('[a-z]+') do b[#b+1]=token end
  if #a==0 or #a>#b then return false end
  for i,token in ipairs(a) do if not pair_allowed(token,b[i],s,cand.type=='completion' and i==#a) then return false end end
  return true
end
function M.variants(code,env)
  local s=state(env);if not s.enabled or #code>48 or not code:match("^[a-z']+$") then return {} end
  local seen={[code]=true};local frontier={code};local result={}
  for _=1,2 do
    local next_frontier={}
    local function add(query)
      if not seen[query] and #result<24 then
        seen[query]=true;result[#result+1]=query;next_frontier[#next_frontier+1]=query
      end
    end
    for _,current in ipairs(frontier) do
      for _,typed in ipairs(s.keys) do
        local pos=current:find(typed,1,true)
        while pos do
          local codes={};for canonical in pairs(s.map[typed]) do codes[#codes+1]=canonical end;table.sort(codes)
          for _,canonical in ipairs(codes) do add(current:sub(1,pos-1)..canonical..current:sub(pos+#typed)) end
          pos=current:find(typed,pos+1,true)
        end
      end
      for _,rule in ipairs(s.custom) do
        local pos=current:find(rule[1],1,true)
        while pos do add(current:sub(1,pos-1)..rule[2]..current:sub(pos+#rule[1]));pos=current:find(rule[1],pos+1,true) end
      end
    end
    frontier=next_frontier
  end
  return result
end
function M.translate(input,seg,env)
  local queries=M.variants(input:lower(),env)
  if #queries==0 then return end
  env.chinese_hot_translator=env.chinese_hot_translator or Component.Translator(env.engine,'','script_translator@translator')
  local seen={};local emitted=0
  for _,query in ipairs(queries) do
    local segment=Segment(seg.start,seg.start+#query);segment.tags=Set{'abc'}
    local translation=env.chinese_hot_translator:query(query,segment)
    if translation then
      local examined=0
      for cand in translation:iter() do
      examined=examined+1;if examined>128 then break end
      -- Never promote a prefix/completion or a second correction produced by
      -- a legacy prism while asking for the corrected, exact spelling.
      local original=M.canonical(cand,env) or (cand.comment or ''):gsub('^［',''):gsub('］$','')
      if compact(cand.preedit)==compact(query) and
         (original=='' or compact(original)==compact(query)) and not seen[cand.text] then
        seen[cand.text]=true
        local result=Candidate('chinese_hot_correction',seg.start,seg._end,cand.text,'[[RIME_LANG:ZH]]')
        result.preedit=input;result.quality=(cand.quality or 0)-1
        yield(result);emitted=emitted+1;if emitted>=48 then return end
      end
    end end
  end
end
return M
