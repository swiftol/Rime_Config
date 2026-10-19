-- Rank existing full conversions, never manufacture a surface or replace the
-- physical key table. Productive auxiliaries are normally written in kana;
-- noun homophones such as 解体 must not consume conversational ～たいです.
local M={}
-- Recognize only whole numeric-counter surfaces already returned by the
-- exact converter. Not a Han-script preference and never synthesize numbers.
local numerals={}
for _,cp in utf8.codes('〇零一二三四五六七八九十百千万億兆') do numerals[cp]=true end
local function numeric_prefix(text)
  if text=='' then return false end
  for _,cp in utf8.codes(text) do
    if not (numerals[cp] or cp>=0x30 and cp<=0x39 or cp>=0xff10 and cp<=0xff19) then return false end
  end
  return true
end
local counters={'人','円','本','枚','個','台','匹','歳','才','回','時','分','秒','日','年','時間','か月','ヶ月','カ月','キロ','メートル'}
function M.counter_surface(text)
  for _,counter in ipairs(counters) do
    if #text>#counter and text:sub(-#counter)==counter then
      if numeric_prefix(text:sub(1,#text-#counter)) then return true end
    end
  end
  return false
end
local counter_readings={['にん']='人',['えん']='円',['まい']='枚',['ほん']='本',
  ['ぼん']='本',['ぽん']='本',['こ']='個',['だい']='台',['びょう']='秒',['ねん']='年'}
function M.counter_variants(reading,values,predicted)
  local out,seen={},{}
  for _,text in ipairs(values or {}) do out[#out+1]=text;seen[text]=true end
  local added=0
  for _,text in ipairs(values or {}) do
    if not predicted or not predicted(text) then
      for kana,surface in pairs(counter_readings) do
        if #reading>#kana and reading:sub(-#kana)==kana then
          local katakana={};for _,cp in utf8.codes(kana) do katakana[#katakana+1]=utf8.char(cp+0x60) end
          for _,suffix in ipairs({kana,table.concat(katakana)}) do
            if #text>#suffix and text:sub(-#suffix)==suffix then
              local prefix=text:sub(1,#text-#suffix)
              local full=prefix..surface
              if numeric_prefix(prefix) and not seen[full] and added<8 then
                out[#out+1]=full;seen[full]=true;added=added+1
              end
            end
          end
        end
      end
    end
  end
  return out
end
-- Clock counters have irregular readings (しじ / くじ). The lightweight
-- converter may return only the lottery/place-name homophones. Generate a
-- clock spelling only in an explicit from/until time construction, never
-- replace the standalone homophones or infer time from arbitrary substrings.
local hours={['いちじ']='一時',['にじ']='二時',['さんじ']='三時',
  ['よじ']='四時',['ごじ']='五時',['ろくじ']='六時',['しちじ']='七時',
  ['はちじ']='八時',['くじ']='九時',['じゅうじ']='十時',
  ['じゅういちじ']='十一時',['じゅうにじ']='十二時'}
function M.clock_expression(reading)
  for _,ending in ipairs({'まで','から'}) do
    if reading:sub(-#ending)==ending then
      local base=reading:sub(1,#reading-#ending)
      local half=''
      if base:sub(-#'はん')=='はん' then base=base:sub(1,#base-#'はん');half='半' end
      if hours[base] then return hours[base]..half..ending end
    end
  end
end
local volitional={['おう']=true,['こう']=true,['ごう']=true,['そう']=true,['とう']=true,
  ['のう']=true,['ぼう']=true,['もう']=true,['ろう']=true,['よう']=true}
local endings={
  'なければならない','なければいけない','ないでください','ようもない','ようがない',
  'てください','たくなかった','たくない','たいです','なかった','ませんでした',
  'ていました','ています','ございます','ましょう','ません','ました','なさい','でしょう',
  'たい','ない','ます','おう','こう','ごう','そう','とう','のう','ぼう','もう','ろう','よう',
}
local function ends(text,suffix) return #text>=#suffix and text:sub(-#suffix)==suffix end
local function han(text)
  for _,cp in utf8.codes(text) do if cp>=0x3400 and cp<=0x9fff then return true end end
  return false
end
local function productive_ending(reading)
  -- Terminal conversational particles do not invalidate the verb phrase.
  -- Keep the entire ending for surface verification; never invent kanji.
  local base,particle=reading,''
  for _,suffix in ipairs({'よね','よ','ね'}) do
    if ends(reading,suffix) then base=reading:sub(1,#reading-#suffix);particle=suffix;break end
  end
  for _,ending in ipairs(endings) do
    if ends(base,ending) then return ending..particle,particle,ending end
  end
end
function M.supports_particle(reading,text)
  local ending,particle=productive_ending(reading)
  return ending~=nil and particle~='' and han(text) and ends(text,ending)
end
local function weather(text)
  -- Subject/verb collocation, not an exact sentence override. Preserve all
  -- alternatives, including 振る when the subject is not rain/snow.
  for _,subject in ipairs({'雨が','雪が','雨は','雪は'}) do
    if text:find(subject..'降',1,true) then return 3 end
  end
  return 0
end
function M.rank(reading,values,predicted,lexical)
  -- The model already recognized the whole numeric prefix. Resolve a literal
  -- counter tail (十二マイ -> 十二枚) by productive counter grammar, retaining
  -- every original candidate; never turn arbitrary name/word tails into units.
  values=M.counter_variants(reading,values,predicted)
  local ending,_,base_ending=productive_ending(reading)
  -- Keep the model's attested all-kana fixed expression (greetings, thanks,
  -- etc.) rather than favoring a rare kanji spelling only because it has Han.
  if lexical and values[1]==reading and (utf8.len(reading) or 0)>=5 then ending=nil end
  local ranked={}
  for index,text in ipairs(values or {}) do
    local score=0
    if not predicted or not predicted(text) then
      if ending and ends(text,ending) and (not volitional[base_ending] or han(text)) then
        score=2+((not ending:find('よう',1,true) and ending~='なさい' and ending~='ございます' and han(text)) and 1 or 0)
        -- A bare kana spelling must not turn an attested noun such as 招待
        -- into a fabricated ～たい verb. Actual kanji+auxiliary conversions
        -- remain eligible; sentence-length kana grammar is still useful.
        if lexical and not han(text) and not ending:find('よう',1,true) then score=0 end
      end
      score=score+weather(text)
      -- For an object-free closing request, the ordinary daily-action verb
      -- 閉める precedes the abstract 占める. Explicit objects/context keep
      -- their original model order; no candidate is synthesized or removed.
      if reading:sub(1,#'しめて')=='しめて' and ends(reading,'ください') and
         text:sub(1,#'閉めて')=='閉めて' then score=score+2 end
    end
    ranked[#ranked+1]={text=text,score=score,index=index}
  end
  table.sort(ranked,function(a,b) if a.score==b.score then return a.index<b.index end return a.score>b.score end)
  local out,preferred={},{}
  for _,item in ipairs(ranked) do
    out[#out+1]=item.text
  end
  if ranked[1] and ranked[1].score>0 then preferred[ranked[1].text]=true end
  return out,preferred
end
return M
