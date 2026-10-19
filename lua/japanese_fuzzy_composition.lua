-- Optional bounded composition of enabled fuzzy operations. No model calls,
-- physical-key changes, vocabulary substitutions or sentence synthesis here.
local M = {}
local options = {
  dakuten='japanese_fuzzy_dakuten', sokuon='japanese_fuzzy_sokuon',
  long_i='japanese_fuzzy_long_i', long_u='japanese_fuzzy_long_u',
  long_mark='japanese_fuzzy_long_mark', chi_ji='japanese_fuzzy_chi_ji',
  hu_fu='japanese_fuzzy_hu_fu', shu_sho='japanese_fuzzy_shu_sho',
  ke_kai='japanese_fuzzy_ke_kai', ke_kae='japanese_fuzzy_ke_kae_gae',
  sei_sai='japanese_fuzzy_sei_sai',
}
function M.enabled(context)
  if not context:get_option('japanese_fuzzy_match') then return false end
  -- Direct small-file reading also supports already-loaded native components.
  -- Settings publishes the existing fuzzy revision to refresh compositions.
  local file=io.open(rime_api.get_user_data_dir()..'/japanese_fuzzy_multiple.flag','r')
  if file then local value=file:read('*l');file:close();return value=='1' end
  return context:get_option('japanese_fuzzy_multiple')
end
function M.valid_rules(rules,context)
  if not M.enabled(context) then return false end
  local count=0
  for id in (rules or ''):gmatch('[^,]+') do
    count=count+1
    if id~='custom' and (not options[id] or not context:get_option(options[id])) then return false end
  end
  return count>=2 and count<=3
end
function M.lexical(candidate,spelling,env)
  if not spelling or not spelling:match('^[a-z%-]+$') or #spelling>48 or candidate.type=='sentence' then return false end
  env.multi_lexical_memory=env.multi_lexical_memory or Memory(env.engine,Schema('japanese'))
  -- Exact complete spelling + identical surface, never a prefix or a sentence
  -- assembled by a permissive prism. Cache is bounded and model-only.
  env.multi_lexical_cache=env.multi_lexical_cache or {}
  local key=spelling..'\0'..candidate.text
  if env.multi_lexical_cache[key]~=nil then return env.multi_lexical_cache[key] end
  local memory=env.multi_lexical_memory;local found=false
  if memory:dict_lookup(spelling,false,48) then
    for entry in memory:iter_dict() do
      if entry.text==candidate.text then
        local code=memory:decode(entry.code)
        if code and table.concat(code,'')==spelling then found=true;break end
      end
    end
  end
  env.multi_lexical_count=(env.multi_lexical_count or 0)+1
  if env.multi_lexical_count>512 then env.multi_lexical_cache={};env.multi_lexical_count=1 end
  env.multi_lexical_cache[key]=found
  return found
end
local cache_key,cache_values
function M.variants(code,context,custom)
  if not M.enabled(context) or #code<3 or #code>24 or not code:match("^[a-z%-]+$") then return {} end
  local enabled={};local stamp={code,context:get_property('japanese_fuzzy_rules_generation') or ''}
  for id,option in pairs(options) do enabled[id]=context:get_option(option) end
  for _,id in ipairs({'dakuten','sokuon','long_i','long_u','long_mark','chi_ji','hu_fu','shu_sho','ke_kai','ke_kae','sei_sai'}) do stamp[#stamp+1]=enabled[id] and id or '' end
  for _,pair in ipairs(custom or {}) do stamp[#stamp+1]=pair[1]..'>'..pair[2] end
  local key=table.concat(stamp,'|')
  if key==cache_key then return cache_values end
  local queue={{code=code,depth=0,rules=''}};local seen={[code]=true};local cursor=1
  while cursor<=#queue and #queue<512 do
    local node=queue[cursor];cursor=cursor+1
    if node.depth<3 then
      local source=node.code
      local function add(value,id)
        if #queue>=512 or value==source or seen[value] or #value>30 then return end
        seen[value]=true
        queue[#queue+1]={code=value,depth=node.depth+1,rules=node.rules=='' and id or node.rules..','..id}
      end
      local function replace(from,to,id)
        local start=1
        while true do
          local first,last=source:find(from,start,true);if not first then break end
          add(source:sub(1,first-1)..to..source:sub(last+1),id);start=first+1
        end
      end
      if enabled.dakuten then
        for _,pair in ipairs({{'k','g'},{'s','z'},{'t','d'},{'b','p'},{'h','p'},{'b','h'}}) do
          replace(pair[1],pair[2],'dakuten');replace(pair[2],pair[1],'dakuten')
        end
      end
      for pos=1,#source do
        local letter=source:sub(pos,pos);local next_letter=source:sub(pos+1,pos+1);local previous=source:sub(pos-1,pos-1)
        if enabled.sokuon and letter:match('[bcdfghjklmpqrstvwxyz]') and previous~=letter and next_letter~=letter then
          add(source:sub(1,pos)..source:sub(pos),'sokuon')
        end
        if enabled.long_i and (letter=='e' or letter=='i') and not next_letter:match('[aeiou]') and previous~=letter then
          add(source:sub(1,pos)..'i'..source:sub(pos+1),'long_i')
        end
        if enabled.long_u and (letter=='o' or letter=='u') and not next_letter:match('[aeiou]') and
           (letter~='u' or not previous:match('[aeiou]')) and previous~=letter then
          add(source:sub(1,pos)..'u'..source:sub(pos+1),'long_u')
        end
        if enabled.long_mark and letter:match('[aeiou]') and next_letter~='-' then add(source:sub(1,pos)..'-'..source:sub(pos+1),'long_mark') end
      end
      local swaps={
        chi_ji={{'cha','ja'},{'chu','ju'},{'cho','jo'},{'chi','ji'},{'tya','dya'},{'tyu','dyu'},{'tyo','dyo'},{'ti','di'}},
        hu_fu={{'hu','fu'}},shu_sho={{'shu','sho'}},sei_sai={{'sei','sai'}},
      }
      for _,id in ipairs({'chi_ji','hu_fu','shu_sho','sei_sai'}) do if enabled[id] then
        for _,pair in ipairs(swaps[id]) do replace(pair[1],pair[2],id);replace(pair[2],pair[1],id) end
      end end
      if enabled.ke_kai then replace('ke','kai','ke_kai');replace('kai','ke','ke_kai') end
      if enabled.ke_kae then replace('ke','kae','ke_kae');replace('ke','gae','ke_kae');replace('kae','ke','ke_kae');replace('gae','ke','ke_kae') end
      if enabled.long_i then replace('ke','kae','long_i');replace('ge','gae','long_i') end
      for _,pair in ipairs(custom or {}) do replace(pair[1],pair[2],'custom');replace(pair[2],pair[1],'custom') end
    end
  end
  local result={}
  for _,node in ipairs(queue) do if node.depth>=2 then result[#result+1]=node end end
  cache_key,cache_values=key,result
  return result
end
return M
