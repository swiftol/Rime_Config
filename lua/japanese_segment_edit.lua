-- Explicit, session-local editing of genuine Mozc segments. Normal typing
-- never starts an edit request. No phrase exceptions or invented path scores.
local M={}
local states,order,serial={},{},0
local function hex(text) return (text:gsub('.',function(c)return string.format('%02x',c:byte())end)) end
local function unhex(text)
  if #text%2~=0 or text:find('[^0-9a-f]') then return nil end
  return (text:gsub('..',function(c)return string.char(tonumber(c,16))end))
end
local function kana(text)
  if not text or text=='' then return false end
  local n=0
  for _,cp in utf8.codes(text) do
    if not ((cp>=0x3041 and cp<=0x3096) or (cp>=0x30a1 and cp<=0x30fa) or cp==0x30fc) then return false end
    n=n+1
  end
  return n<=128
end
function M.cancel(context,refresh)
  local token=context:get_property('japanese_segment_token') or ''
  states[token]=nil
  context:set_property('japanese_segment_token','')
  context:set_property('japanese_segment_active','')
  context:set_property('japanese_segment_pick','')
  context:set_property('japanese_segment_focus_reading','')
  context:set_property('japanese_segment_reading','')
  if refresh then context:refresh_non_confirmed_composition() end
end
local function refresh(context,state)
  context:set_property('japanese_segment_reading',state.reading)
  context:set_property('japanese_segment_focus_reading',state.segments[state.focus].key)
  context:refresh_non_confirmed_composition()
end
function M.current(context)
  local token=context:get_property('japanese_segment_token') or ''
  local state=states[token]
  if state and state.input==context.input and state.generation==(context:get_property('japanese_input_table_generation') or '') and not context:get_option('ascii_mode') and context:get_option('japanese_input_table_enabled') then return state end
  if token~='' then M.cancel(context,false) end
end
local function request(state,sizes,choices,adjust)
  local payload=hex(state.reading)..'|'..table.concat(sizes or {},',')..'|'..table.concat(choices or {},',')..'|'..(adjust or '')
  local rows=require('mozc_v2_translator').query_segment_edit(payload)
  if not rows then return false end
  local segments,joined={},''
  for i,row in ipairs(rows) do
    local index,key,selected,words=row:match('^EDITSEG:(%d+):([0-9a-f]+):([0-9a-f]+):([0-9a-f,]+)$')
    if tonumber(index)~=i-1 then return false end
    key,selected=unhex(key),unhex(selected)
    if not key or not selected or not kana(key) then return false end
    local alternatives,found={},false
    for word in words:gmatch('[^,]+') do
      local decoded=unhex(word)
      if not decoded or decoded=='' or #alternatives>=9 then return false end
      alternatives[#alternatives+1]=decoded;if decoded==selected then found=true end
    end
    if not found then return false end
    segments[i]={key=key,selected=selected,words=alternatives};joined=joined..key
  end
  if joined~=state.reading or #segments==0 then return false end
  state.segments=segments;state.focus=math.min(state.focus,#segments)
  return true
end
local function replay(state,until_segment)
  local sizes,choices={},{}
  for i,segment in ipairs(state.segments) do
    sizes[i]=utf8.len(segment.key)
    choices[i]=(not until_segment or i<until_segment) and hex(segment.selected) or ''
  end
  return sizes,choices
end
function M.surface(state,focus_word)
  local text={}
  for i,segment in ipairs(state.segments) do text[i]=i==state.focus and (focus_word or segment.selected) or segment.selected end
  return table.concat(text)
end
function M.emit(context)
  local state=M.current(context);if not state then return end
  local segment=state.segments[state.focus]
  local words={segment.selected}
  for _,word in ipairs(segment.words) do if word~=segment.selected then words[#words+1]=word end end
  for i,word in ipairs(words) do
    local candidate=Candidate('japanese_segment_edit',0,#state.input,M.surface(state,word),
        '[[RIME_LANG:JA]][[RIME_JR:'..state.reading..']]')
    local preview={}
    for n,s in ipairs(state.segments) do preview[n]=n==state.focus and ('〔'..word..'〕') or s.selected end
    candidate.preedit=table.concat(preview,'｜')
    candidate.comment=candidate.comment..string.format('分段 %d/%d · %s · ←→换段 Shift+←→调边界 数字选词 Ctrl+Enter上屏 Esc退出',state.focus,#state.segments,segment.key)
    candidate.quality=100000-i
    yield(candidate)
  end
end
function M.processor(key,env,read)
  local context=env.engine.context
  if key:release() then return nil end
  local code=key.keycode
  local state=M.current(context)
  local trigger=code==32 and key:ctrl() and not key:alt() and not key:shift() and not key:super()
  if trigger then
    if state then M.cancel(context,true);return 1 end
    if context:get_option('ascii_mode') or not context:get_option('japanese_input_table_enabled') or context.input=='' then return nil end
    local tail=context.composition:back()
    if not tail or tail.start~=0 or tail._end~=#context.input or context.caret_pos~=#context.input then return nil end
    local reading=read(context.input,context)
    if not kana(reading) then return nil end
    local draft={input=context.input,reading=reading,focus=1,generation=context:get_property('japanese_input_table_generation') or ''}
    if not request(draft) then return 1 end -- unavailable bridge: preserve original input/menu
    serial=serial+1;local token=tostring(serial)
    states[token]=draft;order[#order+1]=token
    if #order>64 then states[table.remove(order,1)]=nil end
    context:set_property('japanese_segment_token',token)
    context:set_property('japanese_segment_active','1')
    refresh(context,draft);return 1
  end
  if not state then return nil end
  -- Existing TSF clients request a VK_SELECT refresh after a real popup
  -- click. The backend already applied that local pick; do not exit/commit.
  if code==0xff60 then return 1 end
  if code==0xff1b or code==0xff08 then M.cancel(context,true);return 1 end
  if code==0xff0d and key:ctrl() and not key:shift() and not key:alt() and not key:super() then
    require('japanese_learning_commit').capture(context)
    context:commit();M.cancel(context,false);return 1
  end
  if key:ctrl() or key:alt() or key:super() then return nil end
  local changed=false
  if code==0xff51 or code==0xff53 then
    local delta=code==0xff51 and -1 or 1
    if key:shift() then
      local sizes,choices=replay(state,state.focus)
      if request(state,sizes,choices,(state.focus-1)..','..delta) then changed=true end
    else state.focus=math.max(1,math.min(#state.segments,state.focus+delta));changed=true end
  elseif code>=49 and code<=57 or code==0xffca then
    local requested=code==0xffca and tonumber(context:get_property('japanese_segment_pick')) or code-49
    context:set_property('japanese_segment_pick','')
    local segment=state.segments[state.focus];local words={segment.selected}
    for _,word in ipairs(segment.words) do if word~=segment.selected then words[#words+1]=word end end
    local chosen=requested and words[requested+1]
    if chosen then
      local sizes,choices=replay(state);choices[state.focus]=hex(chosen)
      if request(state,sizes,choices) then changed=true end
    end
  elseif code==32 then
    local selected=context:get_selected_candidate()
    local segment=state.segments[state.focus]
    for _,word in ipairs(segment.words) do
      if selected and M.surface(state,word)==selected.text then
        local sizes,choices=replay(state);choices[state.focus]=hex(word)
        changed=request(state,sizes,choices);break
      end
    end
  elseif code==0xff52 or code==0xff54 or code==0xff0d then return nil
  else M.cancel(context,false);return nil end
  if changed then refresh(context,state) end
  return 1
end
return M
