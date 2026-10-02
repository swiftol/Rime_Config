local M = {}
local q_completion = require("chinese_q_prefix_completion")
local composition_learning = require("japanese_composition_learning")
local sequence = 0
local instance = tostring({}):gsub("[^%w]", "")
local mailbox = rime_api.get_user_data_dir() .. "/mozc_v2_mailbox"
local clean_conversion_cache = {}
local clean_conversion_order = {}
local CLEAN_CACHE_LIMIT = 256
local sentence_lexicon
local READING_OPEN = "[[RIME_JR:"
local READING_CLOSE = "]]"

-- Mozc returns converted candidate text, while its mailbox protocol currently
-- does not return the kana reading as a separate field.  Rebuild that reading
-- from the exact romaji sent to Mozc and carry it as hidden Weasel metadata so
-- Shift/Space/Alt preview also works for Mozc candidates.
local ROMAJI = {
  kya="きゃ",kyu="きゅ",kyo="きょ",gya="ぎゃ",gyu="ぎゅ",gyo="ぎょ",
  sha="しゃ",shu="しゅ",sho="しょ",sya="しゃ",syu="しゅ",syo="しょ",
  ja="じゃ",ji="じ",ju="じゅ",jo="じょ",jya="じゃ",jyu="じゅ",jyo="じょ",
  cha="ちゃ",chu="ちゅ",cho="ちょ",cya="ちゃ",cyu="ちゅ",cyo="ちょ",
  nya="にゃ",nyu="にゅ",nyo="にょ",hya="ひゃ",hyu="ひゅ",hyo="ひょ",
  bya="びゃ",byu="びゅ",byo="びょ",pya="ぴゃ",pyu="ぴゅ",pyo="ぴょ",
  mya="みゃ",myu="みゅ",myo="みょ",rya="りゃ",ryu="りゅ",ryo="りょ",
  tsa="つぁ",tsi="つぃ",tse="つぇ",tso="つぉ",she="しぇ",je="じぇ",che="ちぇ",
  thi="てぃ",dhi="でぃ",fa="ふぁ",fi="ふぃ",fe="ふぇ",fo="ふぉ",
  va="ゔぁ",vi="ゔぃ",vu="ゔ",ve="ゔぇ",vo="ゔぉ",
  shi="し",chi="ち",tsu="つ",dzu="づ",dji="ぢ",
  ka="か",ki="き",ku="く",ke="け",ko="こ",ga="が",gi="ぎ",gu="ぐ",ge="げ",go="ご",
  sa="さ",si="し",su="す",se="せ",so="そ",za="ざ",zi="じ",zu="ず",ze="ぜ",zo="ぞ",
  ta="た",ti="ち",tu="つ",te="て",to="と",da="だ",di="ぢ",du="づ",de="で",['do']="ど",
  na="な",ni="に",nu="ぬ",ne="ね",no="の",ha="は",hi="ひ",hu="ふ",fu="ふ",he="へ",ho="ほ",
  ba="ば",bi="び",bu="ぶ",be="べ",bo="ぼ",pa="ぱ",pi="ぴ",pu="ぷ",pe="ぺ",po="ぽ",
  ma="ま",mi="み",mu="む",me="め",mo="も",ya="や",yu="ゆ",yo="よ",
  ra="ら",ri="り",ru="る",re="れ",ro="ろ",wa="わ",wi="うぃ",we="うぇ",wo="を",
  a="あ",i="い",u="う",wu="う",e="え",o="お",n="ん",['-']="ー",
}

local function romaji_to_hiragana(code)
  code = (code or ""):lower():gsub("q", "-"):gsub("[^a-z%-]", "")
  local output, position = {}, 1
  while position <= #code do
    local current = code:sub(position, position)
    local following = code:sub(position + 1, position + 1)
    if current == following and
       current:match("[bcdfghjklmpqrstvwxyz]") and current ~= "n" then
      output[#output + 1] = "っ"
      position = position + 1
    elseif current == "n" and following == "n" then
      -- Consume only the first n.  The second remains available for ni/na...
      -- (`honnin` -> ほんにん, not the incorrect ほんいん).
      output[#output + 1] = "ん"
      position = position + 1
    elseif current == "n" and
           (following == "" or following:match("[^aeiouy]")) then
      output[#output + 1] = "ん"
      position = position + 1
    else
      local matched = false
      for length = 3, 1, -1 do
        local kana = ROMAJI[code:sub(position, position + length - 1)]
        if kana then
          output[#output + 1] = kana
          position = position + length
          matched = true
          break
        end
      end
      if not matched then position = position + 1 end
    end
  end
  return table.concat(output)
end

local function hiragana_to_katakana(reading)
  local output = {}
  for _, cp in utf8.codes(reading or "") do
    if cp >= 0x3041 and cp <= 0x3096 then cp = cp + 0x60 end
    output[#output + 1] = utf8.char(cp)
  end
  return table.concat(output)
end

local function is_short_kanji_word(value)
  local count = 0
  for _, cp in utf8.codes(value or "") do
    if not ((cp >= 0x3400 and cp <= 0x4dbf) or
            (cp >= 0x4e00 and cp <= 0x9fff) or
            (cp >= 0xf900 and cp <= 0xfaff)) then return false end
    count = count + 1
  end
  return count >= 2 and count <= 4
end

local function kana_surface_reading(text)
  local output, count = {}, 0
  for _, cp in utf8.codes(text or "") do
    if cp >= 0x30a1 and cp <= 0x30f6 then
      cp = cp - 0x60
    elseif not (cp >= 0x3041 and cp <= 0x3096) and cp ~= 0x30fc then
      return nil
    end
    output[#output + 1] = utf8.char(cp)
    count = count + 1
  end
  return count > 0 and table.concat(output) or nil
end

local function learned_fuzzy_reading(code, text, env)
  local typed_reading = romaji_to_hiragana(code)
  local surface = kana_surface_reading(text)
  if surface then
    return surface ~= typed_reading and surface or nil
  end
  -- Kanji spellings may have several dictionary readings.  Do not mark a
  -- user's custom-name reading as fuzzy merely because it is absent from the
  -- public lexicon; only a known, conflicting dictionary reading is evidence.
  if not env.japanese_reverse then
    local ok, reverse = pcall(ReverseLookup, "japanese")
    if ok then env.japanese_reverse = reverse else return nil end
  end
  local ok, codes = pcall(function()
    return env.japanese_reverse:lookup(text)
  end)
  if not ok or not codes or codes == "" then return nil end
  local first
  for spelling in codes:gmatch("%S+") do
    first = first or spelling
    if spelling:lower():gsub("q", "-") == code:gsub("q", "-") then
      return nil
    end
  end
  local reading = first and romaji_to_hiragana(first) or nil
  return reading ~= "" and reading or nil
end

local function is_valid_short_japanese_prefix(code)
  code = (code or ""):lower():gsub("q", "-")
  local position = 1
  while position <= #code do
    local current = code:sub(position, position)
    local following = code:sub(position + 1, position + 1)
    -- A final consonant is the unfinished part we intend to complete.
    if position == #code and current:match("[bcdfghjklmpqrstvwxyz]") and
       current ~= "n" then
      return true
    end
    if current == following and
       current:match("[bcdfghjklmpqrstvwxyz]") and current ~= "n" then
      position = position + 1
    elseif current == "n" and following == "n" then
      position = position + 1
    elseif current == "n" and
           (following == "" or following:match("[^aeiouy]")) then
      position = position + 1
    else
      local matched = false
      for length = 3, 1, -1 do
        if ROMAJI[code:sub(position, position + length - 1)] then
          position = position + length
          matched = true
          break
        end
      end
      if not matched then return false end
    end
  end
  return false
end

-- Accept only a fully parseable Japanese romaji stream before asking Mozc for
-- an ordinary conversion.  Mozc itself is deliberately permissive and may
-- silently discard unknown consonants; that turned Chinese abbreviations such
-- as `ywtia` into unrelated Japanese candidates ahead of `有问题啊`.
local function is_valid_complete_japanese_romaji(code)
  code = (code or ""):lower():gsub("q", "-")
  local position = 1
  while position <= #code do
    local current = code:sub(position, position)
    local following = code:sub(position + 1, position + 1)
    if current == following and
       current:match("[bcdfghjklmpqrstvwxyz]") and current ~= "n" then
      position = position + 1
    elseif current == "n" and following == "n" then
      position = position + 1
    elseif current == "n" and
           (following == "" or following:match("[^aeiouy]")) then
      position = position + 1
    else
      local matched = false
      for length = 3, 1, -1 do
        if ROMAJI[code:sub(position, position + length - 1)] then
          position = position + length
          matched = true
          break
        end
      end
      if not matched then return false end
    end
  end
  return code ~= ""
end

local function first_mora_length(code)
  code = (code or ""):lower():gsub("q", "-")
  if code == "" then return nil end
  local first = code:sub(1, 1)
  if first == code:sub(2, 2) and
     first:match("[bcdfghjklmpqrstvwxyz]") and first ~= "n" then
    return 1
  end
  if first == "n" then
    local following = code:sub(2, 2)
    if following == "n" then return 2 end
    if following == "" or following:match("[^aeiouy]") then return 1 end
  end
  for length = 3, 1, -1 do
    if ROMAJI[code:sub(1, length)] then return length end
  end
  return nil
end

-- Return the roman-code boundary that produces exactly the requested reading
-- prefix.  Trying prefixes is cheap (the active code is bounded by Rime) and
-- reuses the same parser as candidate annotations, avoiding a second romaji
-- table that could drift.  The first exact boundary is intentional: an
-- unfinished consonant from the following mora may be silently ignored by
-- romaji_to_hiragana and must not be consumed by the preceding candidate.
local function prefix_length_for_reading(code, reading_prefix)
  code = (code or ""):lower()
  if code == "" or not reading_prefix or reading_prefix == "" then return nil end
  for length = 1, #code do
    local reading = romaji_to_hiragana(code:sub(1, length))
    if reading == reading_prefix then return length end
  end
  return nil
end

local function japanese_comment(input, prefix, reading_override)
  local normalized = (input or ""):gsub("dewa", "deha")
  if normalized == "konnichiwa" then
    normalized = "konnichiha"
  elseif normalized == "konbanwa" then
    normalized = "konbanha"
  end
  local reading = reading_override or romaji_to_hiragana(normalized)
  local comment = (prefix or "") .. "[[RIME_LANG:JA]]"
  if reading ~= "" then
    comment = comment .. READING_OPEN .. reading .. READING_CLOSE
  end
  return comment
end

-- A complete-enough Mandarin syllable inventory for routing mixed input.
-- If a long code cannot be segmented entirely as pinyin, it is much more
-- likely to be a Japanese sentence.  In that case Mozc should outrank the
-- Chinese script translator's mechanically assembled, often nonsensical
-- sentence.  Valid long pinyin such as `xianzaishuru` remains Chinese-first.
local pinyin = {}
for syllable in ([=[
a ai an ang ao ba bai ban bang bao bei ben beng bi bian biao bie bin bing bo bu
ca cai can cang cao ce cen ceng cha chai chan chang chao che chen cheng chi chong chou chu chua chuai chuan chuang chui chun chuo ci cong cou cu cuan cui cun cuo
da dai dan dang dao de dei den deng di dia dian diao die ding diu dong dou du duan dui dun duo e ei en eng er
fa fan fang fei fen feng fo fou fu ga gai gan gang gao ge gei gen geng gong gou gu gua guai guan guang gui gun guo
ha hai han hang hao he hei hen heng hong hou hu hua huai huan huang hui hun huo
ji jia jian jiang jiao jie jin jing jiong jiu ju juan jue jun
ka kai kan kang kao ke ken keng kong kou ku kua kuai kuan kuang kui kun kuo
la lai lan lang lao le lei leng li lia lian liang liao lie lin ling liu long lou lu luan lun luo lv lve
ma mai man mang mao me mei men meng mi mian miao mie min ming miu mo mou mu
na nai nan nang nao ne nei nen neng ni nian niang niao nie nin ning niu nong nou nu nuan nuo nv nve
o ou pa pai pan pang pao pei pen peng pi pian piao pie pin ping po pou pu
qi qia qian qiang qiao qie qin qing qiong qiu qu quan que qun
ran rang rao re ren reng ri rong rou ru rua ruan rui run ruo
sa sai san sang sao se sen seng sha shai shan shang shao she shei shen sheng shi shou shu shua shuai shuan shuang shui shun shuo si song sou su suan sui sun suo
ta tai tan tang tao te teng ti tian tiao tie ting tong tou tu tuan tui tun tuo
wa wai wan wang wei wen weng wo wu
xi xia xian xiang xiao xie xin xing xiong xiu xu xuan xue xun
ya yan yang yao ye yi yin ying yo yong you yu yuan yue yun
za zai zan zang zao ze zei zen zeng zha zhai zhan zhang zhao zhe zhei zhen zheng zhi zhong zhou zhu zhua zhuai zhuan zhuang zhui zhun zhuo zi zong zou zu zuan zui zun zuo
]=]):gmatch("%a+") do
  pinyin[syllable] = true
end

local function is_complete_pinyin(input)
  local reachable = {[0] = true}
  for finish = 1, #input do
    for start = math.max(1, finish - 5), finish do
      if reachable[start - 1] and pinyin[input:sub(start, finish)] then
        reachable[finish] = true
        break
      end
    end
  end
  return reachable[#input] == true
end

-- Segment a full-pinyin code into exactly `syllable_count` syllables and
-- return their initials.  This lets locally learned Chinese phrases share
-- their usage across full spelling and abbreviation (`guiwuzhe` -> `gwz`).
-- Requiring the number of syllables to match the Han-character count avoids
-- manufacturing abbreviations from incomplete or Japanese input.
local function pinyin_initials(input, syllable_count)
  input = (input or ""):lower():gsub("[%s']+", "")
  if input == "" or syllable_count < 2 then return nil end
  local memo = {}
  local function walk(position, remaining)
    local key = position .. ":" .. remaining
    if memo[key] ~= nil then return memo[key] or nil end
    if remaining == 0 then
      local result = position > #input and "" or false
      memo[key] = result
      return result or nil
    end
    if position > #input then memo[key] = false; return nil end
    -- Prefer longer syllables so ambiguous streams use the ordinary pinyin
    -- split (for example gui|wu|zhe rather than gu|i|...).
    for length = math.min(6, #input - position + 1), 1, -1 do
      local syllable = input:sub(position, position + length - 1)
      if pinyin[syllable] then
        local suffix = walk(position + length, remaining - 1)
        if suffix ~= nil then
          local result = syllable:sub(1, 1) .. suffix
          memo[key] = result
          return result
        end
      end
    end
    memo[key] = false
    return nil
  end
  local result = walk(1, syllable_count)
  if result and #result < #input then return result end
  return nil
end

-- Derive initials when the selected result is not Han text (for example an
-- emoji generated from a Chinese phrase), so there is no character count to
-- pass to pinyin_initials().  Prefer longer valid syllables, matching normal
-- full-pinyin segmentation: haochi -> hao|chi -> hc.
local function pinyin_initials_auto(input)
  input = (input or ""):lower():gsub("[%s']+", "")
  if input == "" then return nil end
  local memo = {}
  local function walk(position)
    if position > #input then return { code = "", count = 0 } end
    if memo[position] ~= nil then return memo[position] or nil end
    for length = math.min(6, #input - position + 1), 1, -1 do
      local syllable = input:sub(position, position + length - 1)
      if pinyin[syllable] then
        local suffix = walk(position + length)
        if suffix then
          local result = {
            code = syllable:sub(1, 1) .. suffix.code,
            count = suffix.count + 1,
          }
          memo[position] = result
          return result
        end
      end
    end
    memo[position] = false
    return nil
  end
  local result = walk(1)
  if result and result.count >= 2 and #result.code < #input then
    return result.code
  end
  return nil
end

-- q is both this product's Japanese long-vowel key and a Mandarin initial.
-- If q starts qi/qu, or is the unfinished final initial after a complete
-- pinyin prefix, keep the stream on the Chinese side even when a later typo
-- makes the whole string incomplete (`niquedin` = ni + que + din[g],
-- `yiq` = yi + q...).
local function has_mandarin_q_boundary(input)
  -- Mixed full-pinyin + initials such as yiqd (yi qi de / yi qie dou)
  -- cannot be segmented by the ordinary pinyin parser.  The generated index
  -- is positive evidence that the complete key exists in the Chinese lexicon.
  if q_completion.has(input) then return true end
  local search_from = 1
  while true do
    local position = input:find("q", search_from, true)
    if not position then return false end
    local following = input:sub(position + 1, position + 1)
    local prefix = input:sub(1, position - 1)
    if (following == "" or following == "i" or following == "u") and
       (prefix == "" or is_complete_pinyin(prefix)) then
      return true
    end
    search_from = position + 1
  end
end

local function has_strong_japanese_ending(input)
  return input:find("desu", 1, true) or input:find("masu", 1, true) or
         input:find("mashita", 1, true) or input:find("deshita", 1, true) or
         input:find("janai", 1, true) or input:find("dewanai", 1, true) or
         input:find("dewanaku", 1, true) or input:find("kudasai", 1, true) or
         -- The productive verb te-form is Japanese evidence even when the
         -- same letters can be split mechanically as Mandarin shi + te.
         -- Require a preceding mora so the short Chinese code `shite` keeps
         -- its mixed-language behavior.
         (#input >= 7 and input:sub(-5) == "shite")
end

local japanese_function_words = {
  kara = true,
  moshi = true,
  tatoe = true,
  ohayou = true,
  konnichiwa = true,
  konbanwa = true,
}

local function has_strong_japanese_structure(input)
  if japanese_function_words[input] then return true end
  return input:find("mamanisezu", 1, true) or
         input:find("nisezu", 1, true) or
         input:find("waruku", 1, true) or
         input:find("nikui", 1, true) or
         input:find("baidemo", 1, true) or
         input:find("dehanaku", 1, true) or
         input:find("naiyouha", 1, true) or
         input:find("jibunha", 1, true) or
         input:find("houkoku", 1, true) or
         (#input >= 12 and input:find("wo", 1, true))
end

local function should_prefer_japanese(input)
  -- Grammar suffixes and input length are only evidence after the entire
  -- stream parses as Japanese.  Chinese initials ending in wa/wo must not
  -- suppress Chinese candidates (ywmwa previously matched the wa heuristic).
  local parseable = is_valid_complete_japanese_romaji(input) or
                    is_valid_short_japanese_prefix(input)
  -- Preserve the halfway point of ch/sh while typing a real Japanese word.
  if not parseable and (input:sub(-2) == "ch" or input:sub(-2) == "sh") then
    parseable = is_valid_short_japanese_prefix(input:sub(1, -2))
  end
  if not parseable then return false end
  -- Once a q is proven to start a Mandarin syllable (or a dictionary-backed
  -- Chinese abbreviation chain), later shorthand consonants must not let the
  -- generic long-input heuristic flip the entire stream back to Japanese.
  -- Example: yiqiandshi = yi qian d(e) shi -> 以前的事.
  if has_mandarin_q_boundary(input) then return false end
  -- A short code that is not a complete pinyin stream can still be a useful
  -- Chinese abbreviation (`zonggj` -> 总感觉).  Treating every such 6-letter
  -- code as Japanese made Mozc suppress genuine Chinese candidates.  Only
  -- unmistakable Japanese spelling/grammar is promoted at short lengths;
  -- the generic "not pinyin" heuristic starts at 10 letters.
  return japanese_function_words[input] == true or
         -- q is our explicit long-vowel mark only after a vowel (`koqhiq`
         -- -> コーヒー).  In Chinese abbreviations it can be an ordinary
         -- initial (`jtxqj` -> 今天星期几), so a bare q must not switch the
         -- whole input to Japanese.
         -- A vowel before q is not sufficient by itself: in Mandarin it can
         -- be the boundary before the q-initial syllable (`niqueding` =
         -- ni + que + ding).  A fully segmentable pinyin stream must retain
         -- Chinese routing; only use vowel+q as the long-vowel signal when
         -- the whole input is not valid pinyin.
         (input:find("[aeiou]q") ~= nil and
          -- An internal q is Japanese evidence only after the following
          -- syllable is complete.  At an unfinished short code such as `aqy`,
          -- treating q as ー made the hold path discard `qy` and show the old
          -- `a` candidates (あ/亜/有/在) instead of Chinese abbreviation
          -- results.  A final q itself is complete, and full forms such as
          -- `aqya` / `koqhiq` remain valid Japanese streams.
          is_valid_complete_japanese_romaji(input) and
          not is_complete_pinyin(input) and
          not has_mandarin_q_boundary(input)) or
         input:find("honnin", 1, true) == 1 or
         ((input:sub(-2) == "wa" or input:sub(-2) == "wo") and
          not is_complete_pinyin(input)) or
         has_strong_japanese_ending(input) or
         (has_strong_japanese_structure(input) and
          not is_complete_pinyin(input)) or
         (#input >= 10 and not is_complete_pinyin(input))
end

local function is_explicit_long_q(input)
  if not input:match("[aeiou]q$") then return false end
  -- A long, structurally Japanese sentence remains Japanese even when its
  -- internal letters can accidentally be segmented as Mandarin initials.
  return not has_mandarin_q_boundary(input) or
         (#input >= 10 and
          (has_strong_japanese_structure(input) or
           input:find("wa", 1, true)))
end

-- A terminal q can also be the next Mandarin initial after a complete
-- pinyin phrase (ta|ku|shi|q).  Trust it as ー only when the complete
-- q-to-minus spelling really exists in the Japanese dictionary.  This keeps
-- Chinese prefixes such as yiq/yiqd on their established route while making
-- dictionary words such as takushiq available to Mozc.
local function has_exact_japanese_terminal_q(input, memory)
  if not memory or not input:match("[aeiou]q$") or
     not is_valid_complete_japanese_romaji(input) then
    return false
  end
  local spelling = input:sub(1, -2) .. "-"
  if not memory:dict_lookup(spelling, false, 16) then return false end
  for entry in memory:iter_dict() do
    local decoded = memory:decode(entry.code)
    if decoded and table.concat(decoded, "") == spelling then return true end
  end
  return false
end

-- Mixed Chinese segmentation can split a complete romaji word into several
-- pinyin syllables (kaigaaru -> kai ga a ru).  The regular Japanese script
-- translator then sees only prefixes, even though the full spelling exists
-- in its dictionary.  Query that dictionary by the unsegmented input here.
local function exact_japanese_entries(input, memory)
  if not memory or not memory:dict_lookup(input, false, 64) then return {} end
  local result, seen = {}, {}
  for entry in memory:iter_dict() do
    local decoded = memory:decode(entry.code)
    if decoded and table.concat(decoded, "") == input and
       not seen[entry.text] then
      seen[entry.text] = true
      result[#result + 1] = entry.text
      if #result >= 24 then break end
    end
  end
  return result
end

-- The compiled Rime memory can also return mechanically assembled entries,
-- so it cannot by itself prove that a word exists in the source dictionary.
-- For short adjective sentences, use a compact index generated only from
-- weighted Mozc dictionary records.  Exact roman-code concatenation proves
-- that the composed candidate neither adds nor drops a kana; all other
-- sentences still use normal Mozc conversion.
local function lexical_sentence_candidate(input)
  if #input < 10 or #input > 28 or
     (not input:match("[ieo]$") and not input:match("desu$")) or
     not should_prefer_japanese(input) then
    return nil
  end
  if not sentence_lexicon then
    sentence_lexicon = require("japanese_sentence_lexicon")
  end
  local endings = {
    { "desune", "ですね" }, { "desuyo", "ですよ" },
    { "desu", "です" }, { "ne", "ね" }, { "yo", "よ" },
    { "", "" },
  }
  for _, ending in ipairs(endings) do
    local suffix_code, suffix_text = ending[1], ending[2]
    if suffix_code == "" or input:sub(-#suffix_code) == suffix_code then
      local body = suffix_code == "" and input or
                   input:sub(1, #input - #suffix_code)
      for split = 4, #body - 4 do
        local prefix = sentence_lexicon.prefix[body:sub(1, split)]
        local adjective = sentence_lexicon.adjective[body:sub(split + 1)]
        if prefix and adjective then
          return prefix .. adjective .. suffix_text
        end
      end
    end
  end
  return nil
end

M.is_complete_pinyin = is_complete_pinyin
M.is_single_pinyin_syllable = function(input)
  input = (input or ""):lower():gsub("[%s']+", "")
  return pinyin[input] == true
end
function M.pinyin_syllables_starting_with(prefix)
  local result = {}
  for syllable in pairs(pinyin) do
    if syllable:sub(1, #prefix) == prefix then
      result[#result + 1] = syllable
    end
  end
  table.sort(result)
  return result
end
M.pinyin_initials = pinyin_initials
M.pinyin_initials_auto = pinyin_initials_auto
M.romaji_to_hiragana = romaji_to_hiragana
M.is_complete_japanese = is_valid_complete_japanese_romaji
M.should_prefer_japanese = should_prefer_japanese
M.is_explicit_long_q = is_explicit_long_q
M.has_exact_japanese_terminal_q = has_exact_japanese_terminal_q
M.first_mora_length = first_mora_length
M.prefix_length_for_reading = prefix_length_for_reading
M.is_valid_japanese_input = function(input)
  return is_valid_complete_japanese_romaji(input) or
         is_valid_short_japanese_prefix(input)
end

function M.init(env)
  env.initial_quality = env.engine.schema.config:get_int("mozc_v2/initial_quality") or 1000000
  env.prefix_memory = Memory(env.engine, Schema("japanese"))
end

local function contains_ascii_letter(value)
  for _, cp in utf8.codes(value or "") do
    if (cp >= 0x0041 and cp <= 0x005a) or
       (cp >= 0x0061 and cp <= 0x007a) or
       (cp >= 0xff21 and cp <= 0xff3a) or
       (cp >= 0xff41 and cp <= 0xff5a) then
      return true
    end
  end
  return false
end

local function log(message)
  local file = io.open(rime_api.get_user_data_dir() .. "/mozc_v2_lua.log", "a")
  if file then file:write(os.date("%Y-%m-%d %H:%M:%S"), " ", message, "\n"); file:close() end
end

local function connect()
  local probe = io.open(mailbox .. "/bridge.ready", "r")
  if not probe then return false end
  probe:close()
  return true
end

local function query(input)
  if not connect() then
    log("bridge unavailable " .. input)
    return nil
  end
  sequence = sequence + 1
  local request_id = tostring(os.time()) .. "-" .. instance .. "-" .. tostring(sequence)
  local response_path = mailbox .. "/response-" .. request_id .. ".tsv"
  local request_path = mailbox .. "/request-" .. request_id .. ".tsv"
  local request_temp = mailbox .. "/request-" .. request_id .. ".tmp"
  local request = io.open(request_temp, "w")
  if not request then return nil end
  -- The bridge receives kana rather than raw key events, so a literal `wa`
  -- would otherwise become わ before Mozc can apply Japanese particle
  -- spelling.  In the copular grammar `dewa...`, the written form is always
  -- `では...` (for example ではなく / ではない).  Normalize this productive
  -- grammar pattern before romanization; keep the original input as the cache
  -- and routing key.
  local bridge_input = input:gsub("wu", "u"):gsub("dewa", "deha")
  if bridge_input == "konnichiwa" then
    bridge_input = "konnichiha"
  elseif bridge_input == "konbanwa" then
    bridge_input = "konbanha"
  end
  request:write(request_id, "\n", bridge_input, "\n")
  request:close()
  if not os.rename(request_temp, request_path) then
    log("request publish failed " .. input .. " path=" .. request_path)
    return nil
  end

  local deadline = os.clock() + 0.100
  local line
  local last_seen
  repeat
    local response = io.open(response_path, "r")
    if response then
      line = response:read("*l")
      response:close()
      last_seen = line
    end
    if line and line:sub(1, #request_id + 1) == request_id .. "\t" then break end
    line = nil
  until os.clock() >= deadline
  if not line then
    -- A timeout only means this prefix missed its deadline.  Removing the
    -- global ready marker disabled all following keystrokes for up to one
    -- second and made long sentences fall back to Chinese.  Cancel only this
    -- obsolete request; the bridge remains healthy for the next prefix.
    os.remove(request_path)
    os.remove(response_path)
    log("mailbox timeout " .. input .. " path=" .. response_path ..
        " seen=" .. tostring(last_seen))
    return nil
  end

  os.remove(response_path)
  local _, status, payload = line:match("^([^\t]+)\t([^\t]+)\t?(.*)$")
  if status == "STALE" then return nil end
  if status ~= "OK" then
    log("bridge returned " .. tostring(status) .. " input=" .. input ..
        " line=" .. tostring(line))
    return nil
  end
  local result = {}
  for text in payload:gmatch("[^\t]+") do result[#result + 1] = text end
  local clean, clean_seen = {}, {}
  for _, value in ipairs(result) do
    -- In this productive question grammar, kana なん is ordinarily written
    -- with the common kanji 何.  The mailbox bridge otherwise offers only the
    -- all-kana form even though the same standalone Mozc engine knows 何.
    if input == "korehanandesuka" then
      value = value:gsub("なんですか", "何ですか")
    end
    if not contains_ascii_letter(value) and not clean_seen[value] then
      clean_seen[value] = true
      clean[#clean + 1] = value
      -- Whole-sentence n-best paths and fair per-segment alternatives now
      -- arrive together. Do not reintroduce the old nine-item truncation.
      if #clean >= 48 then break end
    end
  end
  -- With no preceding context Mozc occasionally ranks 今生きます above the
  -- overwhelmingly more common motion phrase 今行きます.  Prefer the latter
  -- only for the explicit `ima + ikimasu` grammar; keep all other homophones.
  if input:match("imaikimasu$") then
    table.sort(clean, function(left, right)
      local left_score = left:find("今行きます", 1, true) and 1 or 0
      local right_score = right:find("今行きます", 1, true) and 1 or 0
      return left_score > right_score
    end)
  end
  if #clean > 0 then
    if clean_conversion_cache[input] == nil then
      clean_conversion_order[#clean_conversion_order + 1] = input
      if #clean_conversion_order > CLEAN_CACHE_LIMIT then
        local oldest = table.remove(clean_conversion_order, 1)
        clean_conversion_cache[oldest] = nil
      end
    end
    clean_conversion_cache[input] = clean
  end
  -- Never expose Mozc's raw/partially converted Latin fallback as a Japanese
  -- candidate.  The clean list was already computed and cached above; the
  -- old return value accidentally yielded the unfiltered bridge response.
  if #clean == 0 then return nil end
  return clean
end

function M.query_kana(reading)
  local code = "KANA:" .. reading
  return clean_conversion_cache[code] or query(code)
end

-- A bare n before a vowel or y is ambiguous across word boundaries:
-- haikan+oyobi must read はいかんおよび (not はいかのよび), and
-- haikan+you must read はいかんよう (not はいかにょう).  Find a real Japanese
-- word ending at n and a real following word; for one-kanji suffixes such
-- as 用, additionally require the combined word to exist in the lexicon.
local boundary_exact_cache, boundary_exact_order = {}, {}
local function boundary_exact_entries(code, memory)
  if boundary_exact_cache[code] then return boundary_exact_cache[code] end
  local entries = exact_japanese_entries(code, memory)
  boundary_exact_cache[code] = entries
  boundary_exact_order[#boundary_exact_order + 1] = code
  if #boundary_exact_order > 512 then
    boundary_exact_cache[table.remove(boundary_exact_order, 1)] = nil
  end
  return entries
end

local function n_boundary_options(input, memory)
  if #input < 10 or #input > 40 or
     not input:find("n[aeiouy]") then return {} end
  local options = {}
  for position = 1, #input - 4 do
    if input:sub(position, position) == "n" and
       input:sub(position + 1, position + 1):match("[aeiouy]") then
      local prefix_code, tail_code = input:sub(1, position),
                                     input:sub(position + 1)
      if #prefix_code >= 5 and
         is_valid_complete_japanese_romaji(prefix_code) and
         is_valid_complete_japanese_romaji(tail_code) then
        local pairs = {}
        for left_length = math.min(12, #prefix_code), 5, -1 do
          local left_code = prefix_code:sub(-left_length)
          local prefixes = boundary_exact_entries(left_code, memory)
          if #prefixes > 0 then
            for right_length = 2, math.min(10, #tail_code) do
              local right_code = tail_code:sub(1, right_length)
              if is_valid_complete_japanese_romaji(right_code) then
                local following = boundary_exact_entries(right_code, memory)
                local combined
                for first = 1, math.min(4, #prefixes) do
                  if utf8.len(prefixes[first]) >= 2 then
                    for second = 1, math.min(4, #following) do
                      local pair = prefixes[first] .. following[second]
                      if utf8.len(following[second]) >= 2 then
                        pairs[pair] = true
                      else
                        combined = combined or boundary_exact_entries(
                            left_code .. right_code, memory)
                        for _, word in ipairs(combined) do
                          if word == pair then pairs[pair] = true; break end
                        end
                      end
                    end
                  end
                end
              end
            end
          end
          if next(pairs) then break end
        end
        if next(pairs) then
          options[#options + 1] = {
            reading = romaji_to_hiragana(prefix_code) ..
                      romaji_to_hiragana(tail_code),
            pairs = pairs,
          }
          if #options >= 2 then break end
        end
      end
    end
  end
  return options
end

local function matches_n_boundary(text, option)
  for beginning in pairs(option.pairs) do
    if text:find(beginning, 1, true) then return true end
  end
  return false
end

-- A long technical term may be absent as a whole while all of its parts are
-- attested.  Offer an early, short-span kanji candidate only when the rest of
-- the spelling can also be covered by one or two complete dictionary words.
-- This lets the user choose 排水 -> 横 -> 枝管 without inventing a whole term.
local function decomposable_japanese_tail(code, memory, require_split)
  if not require_split and #boundary_exact_entries(code, memory) > 0 then
    return true
  end
  for split = 2, #code - 2 do
    if #boundary_exact_entries(code:sub(1, split), memory) > 0 and
       #boundary_exact_entries(code:sub(split + 1), memory) > 0 then
      return true
    end
  end
  return false
end

local function is_kanji_segment_word(text)
  local count = 0
  for _, cp in utf8.codes(text or "") do
    if not ((cp >= 0x3400 and cp <= 0x4dbf) or
            (cp >= 0x4e00 and cp <= 0x9fff) or
            (cp >= 0xf900 and cp <= 0xfaff)) then return false end
    count = count + 1
  end
  return count >= 1 and count <= 4
end

local function japanese_segment_heads(input, seg, memory)
  if #input < 10 or #input > 32 or
     (seg.start == 0 and not should_prefer_japanese(input)) or
     not is_valid_complete_japanese_romaji(input) or
     #boundary_exact_entries(input, memory) > 0 then return {} end
  local heads = {}
  for length = math.min(#input - 3, 12), 4, -1 do
    local head_code, tail_code = input:sub(1, length), input:sub(length + 1)
    if is_valid_complete_japanese_romaji(head_code) and
       is_valid_complete_japanese_romaji(tail_code) and
       decomposable_japanese_tail(tail_code, memory, seg.start == 0) then
      for _, word in ipairs(boundary_exact_entries(head_code, memory)) do
        if is_kanji_segment_word(word) and
           (seg.start > 0 or utf8.len(word) >= 2) then
          local head = Candidate("mozc_v2_segment_head", seg.start,
                                 seg.start + length, word,
                                 japanese_comment(head_code))
          head.preedit = head_code
          heads[#heads + 1] = head
          if #heads >= 4 then return heads end
        end
      end
    end
  end
  return heads
end

local function terminal_dictionary_variants(input, text, memory)
  for length = math.min(12, #input - 3), 6, -1 do
    local endings = exact_japanese_entries(input:sub(-length), memory)
    for _, ending in ipairs(endings) do
      if text:sub(-#ending) == ending then
        local variants = {}
        local stem = text:sub(1, #text - #ending)
        local last_offset = utf8.offset(stem, -1)
        local previous = last_offset and stem:sub(last_offset) or ""
        for _, alternative in ipairs(endings) do
          if alternative ~= ending and
             utf8.len(alternative) == utf8.len(ending) then
            -- Keep only another kanji spelling of the same-length ending.
            local has_han = false
            for _, cp in utf8.codes(alternative) do
              if (cp >= 0x3400 and cp <= 0x4dbf) or
                 (cp >= 0x4e00 and cp <= 0x9fff) or
                 (cp >= 0xf900 and cp <= 0xfaff) then
                has_han = true
                break
              end
            end
            if has_han then
              local next_offset = utf8.offset(alternative, 2)
              local first_char = alternative:sub(1, next_offset and
                                                  next_offset - 1 or nil)
              variants[#variants + 1] = {
                text = stem .. alternative,
                continuity = previous ~= "" and previous == first_char and
                             1 or 0,
                sequence = #variants + 1,
              }
            end
          end
        end
        table.sort(variants, function(a, b)
          if a.continuity ~= b.continuity then
            return a.continuity > b.continuity
          end
          return a.sequence < b.sequence
        end)
        return variants[1] and { variants[1].text } or {}
      end
    end
  end
  return {}
end

-- Accept the phonetic spelling `wa` for the topic particle は.  Mozc receives
-- romanized kana from our mailbox bridge, so `watashiwa` normally converts to
-- the orthographically wrong 私わ.  Query the standard spelling (`...ha`) as
-- a second branch and keep only results that actually end in は.  Prefer that
-- branch when Mozc's original best result exposes a literal final わ; ordinary
-- words such as denwa -> 電話 and kawa -> 川 therefore keep their established
-- order and are never mechanically rewritten.
local function add_final_wa_particle_alias(input, values)
  if input:sub(-2) ~= "wa" then return values end

  local corrected_input = input:sub(1, -3) .. "ha"
  if corrected_input == input then return values end
  local corrected = query(corrected_input)
  if not corrected then return values end

  local particle, seen = {}, {}
  for _, value in ipairs(values or {}) do seen[value] = true end
  for _, value in ipairs(corrected) do
    if value:sub(-#"は") == "は" and not seen[value] then
      particle[#particle + 1] = value
      seen[value] = true
    end
  end
  if #particle == 0 then return values end

  local prefer_particle = input == "wa" or
      (values and values[1] and values[1]:sub(-#"わ") == "わ")
  if not prefer_particle then return values end
  local merged = {}
  for _, value in ipairs(particle) do merged[#merged + 1] = value end
  for _, value in ipairs(values or {}) do merged[#merged + 1] = value end
  return merged
end

-- The spoken topic particle `wa` can occur inside a complete sentence, not
-- only at its end.  Try each occurrence from right to left as written `ha`
-- and accept that branch only when Mozc actually returns は.  Requiring the
-- original conversion to expose a literal わ protects lexical wa in words
-- such as denwa -> 電話 and kawa -> 川.
local function add_contextual_wa_particle_alias(input, values, allow_initial)
  if not values or not values[1] or #input < 8 or
     not input:find("wa", 1, true) then
    return values
  end

  local positions = {}
  local start = 1
  while true do
    local position = input:find("wa", start, true)
    if not position then break end
    -- Final wa is handled by the stricter final-particle branch below.
    if (position > 1 or allow_initial) and position + 1 < #input then
      positions[#positions + 1] = position
    end
    start = position + 2
  end

  local corrected_values, seen = {}, {}
  for _, value in ipairs(values) do seen[value] = true end
  for index = #positions, 1, -1 do
    local position = positions[index]
    local corrected_input = input:sub(1, position - 1) .. "ha" ..
                            input:sub(position + 2)
    local corrected = query(corrected_input)
    for _, value in ipairs(corrected or {}) do
      if value:find("は", 1, true) and not seen[value] then
        corrected_values[#corrected_values + 1] = value
        seen[value] = true
      end
    end
  end
  if #corrected_values == 0 then return values end

  local merged = {}
  for _, value in ipairs(corrected_values) do merged[#merged + 1] = value end
  for _, value in ipairs(values) do merged[#merged + 1] = value end
  return merged
end

-- Prefix association reuses the same mailbox protocol and Latin-residue
-- filtering.  Keep the raw bridge function private so every caller receives
-- only clean Japanese candidates.
M.query_clean = query

-- When association/prediction is disabled, an unfinished consonant at the
-- end of a Japanese sentence must not make the whole candidate window blank.
-- Query the nearest completed romaji prefix again instead of relying on
-- translator state: Rime may recreate/lazily enumerate a translation between
-- keystrokes, so a cached "previous frame" is not dependable.
local function query_nearest_completed_prefix(input)
  -- First prefer a response already produced by an earlier frame.
  for trim = 1, math.min(3, #input - 1) do
    local prefix = input:sub(1, #input - trim)
    local cached = clean_conversion_cache[prefix]
    if cached and #cached > 0 then
      log("hold cache input=" .. input .. " prefix=" .. prefix ..
          " count=" .. tostring(#cached) .. " first=" .. cached[1])
      return cached, prefix
    end
  end

  -- Query only the nearest prefix that is itself a complete romaji stream.
  -- The old loop queried every trim independently; an unfinished digraph such
  -- as sh therefore waited for up to three 100 ms mailbox deadlines.  Fuzzy
  -- completions keep their own preceding-frame cache, so one clean fallback
  -- request is sufficient here.
  local prefix
  for trim = 1, math.min(3, #input - 1) do
    local candidate = input:sub(1, #input - trim)
    if is_valid_complete_japanese_romaji(candidate) then
      prefix = candidate
      break
    end
  end
  if not prefix then return nil end
  local values = query(prefix)
  if values then
    local clean = {}
    for _, value in ipairs(values) do
      if not contains_ascii_letter(value) then
        clean[#clean + 1] = value
        if #clean >= 9 then break end
      end
    end
    if #clean > 0 then
      log("hold fallback input=" .. input .. " prefix=" .. prefix ..
          " count=" .. tostring(#clean) .. " first=" .. clean[1])
      return clean, prefix
    end
    log("hold fallback dirty input=" .. input .. " prefix=" .. prefix ..
        " count=" .. tostring(#values))
  end
  return nil
end

function M.func(input, seg, env)
  if seg:has_tag("japanese_input_table") then return end
  if input == "" or not input:match("^[a-zq%-]+$") then return end
  local context = env.engine.context
  local n_boundaries = n_boundary_options(input, env.prefix_memory)
  -- The user can teach an absent Japanese name by repeatedly selecting its
  -- individual kanji.  Learned spellings are exact-code user history, not a
  -- guessed variant of an unrelated dictionary word.
  for index, item in ipairs(composition_learning.ranked(input)) do
    local corrected_reading
    for _, option in ipairs(n_boundaries) do
      if matches_n_boundary(item.text, option) then
        corrected_reading = option.reading
        break
      end
    end
    local fuzzy_reading = not corrected_reading and
                          learned_fuzzy_reading(input, item.text, env) or nil
    if not fuzzy_reading or context:get_option("japanese_fuzzy_match") then
      local comment = fuzzy_reading and
          ("[JF_READING]" .. fuzzy_reading .. "[[RIME_LANG:JA]]") or
          japanese_comment(input, nil, corrected_reading)
      local candidate = Candidate("mozc_v2_preferred_correction", seg.start,
                                  seg._end, item.text, comment)
      candidate.preedit = input
      candidate.quality = 2000 + item.score - index / 1000
      yield(candidate)
    end
    if index >= 8 then break end
  end
  local association_disabled =
      context:get_option("japanese_prefix_completion_disabled")
  -- Dictionary evidence is stronger than the strict Mozc parser.  Look up
  -- the *whole* unsegmented code before any q/romaji early return so a valid
  -- lexicon spelling cannot disappear merely because a parser rejects it.
  local dictionary_exact = #input >= 3 and
      exact_japanese_entries(input:gsub("q", "-"), env.prefix_memory) or {}
  for index, value in ipairs(dictionary_exact) do
    local exact = Candidate("mozc_v2_dictionary_exact", seg.start,
                            seg._end, value, japanese_comment(input))
    exact.preedit = input
    exact.quality = math.max(env.initial_quality, 350) + 100 - index
    yield(exact)
  end
  local exact_terminal_long_q =
      (input:match("[aeiou]q$") and #dictionary_exact > 0) or
      has_exact_japanese_terminal_q(input, env.prefix_memory)
  if context:get_option("japanese_particle_wa_ha") and
     input:find("wh", 1, true) then
    local corrected_input = input:gsub("wh", "ha")
    local corrected = query(corrected_input)
    local emitted = 0
    for _, value in ipairs(corrected or {}) do
      if value:find("は", 1, true) then
        emitted = emitted + 1
        local candidate = Candidate("mozc_v2_particle_alias", seg.start,
                                    seg._end, value,
                                    japanese_comment(corrected_input))
        candidate.preedit = input
        candidate.quality = 1000 - emitted
        yield(candidate)
      end
    end
    if emitted > 0 then return end
  end
  -- q only represents ー after a vowel.  Otherwise it is commonly a Chinese
  -- abbreviation initial (`jtxqj` -> 今天星期几).  Sending that form to Mozc
  -- merely produces punctuation variants such as jtx-j, which must not crowd
  -- genuine Chinese candidates.
  if input:find("q", 1, true) and
     (not input:find("[aeiou]q") or
      (has_mandarin_q_boundary(input) and not is_explicit_long_q(input) and
       not exact_terminal_long_q)) then
    return
  end

  -- Never let Mozc repair an invalid completed stream by dropping letters.
  -- An unfinished final consonant is handled separately below so genuine
  -- Japanese remains visible while the user is between syllables.
  local explicit_long_q = is_explicit_long_q(input)
  local has_unfinished_final =
      input:match("[bcdfghjklmprstvwxyz]$") ~= nil and not explicit_long_q
  if not has_unfinished_final and
     not is_valid_complete_japanese_romaji(input) then
    return
  end
  if has_unfinished_final and #input <= 8 and
     not is_valid_short_japanese_prefix(input) then
    return
  end

  -- A complete one-mora spelling is the exact gojuon form, not an
  -- association.  Emit it synchronously so large Chinese homophone sets
  -- cannot push `shi -> し`, `chi -> ち`, etc. beyond the filter buffer.
  -- This is derived by the romaji parser and therefore covers the whole
  -- table without a per-syllable output list.
  if #input >= 2 then
    local gojuon = romaji_to_hiragana(input)
    if gojuon ~= "" and utf8.len(gojuon) == 1 then
      local exact = Candidate("mozc_v2", seg.start, seg._end, gojuon,
                              japanese_comment(input))
      exact.preedit = input
      exact.quality = 999
      yield(exact)
    end
  end

  -- Emit the four standalone polite grammar forms synchronously.  Their
  -- exact form must remain first even when the fuzzy dictionary is busy
  -- producing alternatives such as desu -> tesuu / 手数.  The later Mozc
  -- response is still yielded in full and the uniquifier removes duplicates.
  local standalone_polite = ({
    desu = "です", masu = "ます",
    desuka = "ですか", masuka = "ますか",
  })[input]
  if standalone_polite then
    local exact = Candidate("mozc_v2", seg.start, seg._end,
                            standalone_polite, japanese_comment(input))
    exact.preedit = input
    exact.quality = 1000
    yield(exact)
  end

  -- Short, valid Japanese prefixes ending in an unfinished consonant need
  -- real associations rather than Mozc's `いっp`-style Latin residue.  Use
  -- the compiled Japanese prefix dictionary for this: synchronously probing
  -- five vowels (and eight suffixes for `ipp`) blocked the Rime key handler
  -- for several mailbox round trips, so later physical keys visibly queued.
  -- The strict parser prevents Chinese abbreviations such as `wjdmwt` from
  -- entering this branch.  Longer sentences keep the established hold logic.
  if not association_disabled and #input <= 8 and
     is_valid_short_japanese_prefix(input) then
    local seen, emitted = {}, 0
    -- Reuse the established Japanese dictionary's frequency order.  The
    -- dictionary stores the same Hepburn spelling (`sapporo`, `ippan`), so
    -- query the user's prefix directly.
    local legacy_prefix = input
    local prefix_entries = {}
    local diversify_doubled = #input == 3 and
                              input:sub(-1) == input:sub(-2, -2)
    local lookup_limit = diversify_doubled and 512 or 32
    if #input >= 3 and env.prefix_memory and
       env.prefix_memory:dict_lookup(legacy_prefix, true, lookup_limit) then
      for entry in env.prefix_memory:iter_dict() do
        local decoded = env.prefix_memory:decode(entry.code)
        local spelling = decoded and table.concat(decoded, "") or ""
        if spelling:sub(1, #legacy_prefix) == legacy_prefix and
           not seen[entry.text] then
          seen[entry.text] = true
          prefix_entries[#prefix_entries + 1] = {
            text = entry.text,
            spelling = spelling,
          }
          if not diversify_doubled and #prefix_entries >= 8 then break end
        end
      end
    end

    local ordered_entries = {}
    -- For a three-letter doubled-consonant boundary (`ipp`), raw dictionary
    -- order is dominated by one continuation such as `ippa...`.  Diversify
    -- the first row by the same common vowel continuations that the old code
    -- queried through eight blocking mailbox calls, but do it entirely from
    -- the already-loaded dictionary.
    if diversify_doubled then
      local used = {}
      for _, suffix in ipairs({"an", "ai", "ou", "a", "i", "u", "e", "o"}) do
        for index, item in ipairs(prefix_entries) do
          if not used[index] and
             item.spelling:sub(#legacy_prefix + 1,
                               #legacy_prefix + #suffix) == suffix then
            ordered_entries[#ordered_entries + 1] = item
            used[index] = true
            break
          end
        end
      end
      for index, item in ipairs(prefix_entries) do
        if not used[index] then ordered_entries[#ordered_entries + 1] = item end
      end
    else
      ordered_entries = prefix_entries
    end

    for _, item in ipairs(ordered_entries) do
      emitted = emitted + 1
      local candidate = Candidate(
        "completion", seg.start, seg._end, item.text,
        japanese_comment(item.spelling, "[JP:PREFIX]"))
      candidate.quality = math.max(env.initial_quality, 350) - emitted
      yield(candidate)
      if emitted >= 8 then break end
    end
    if emitted > 0 then return end
  end
  -- Mozc represents an unfinished trailing consonant by appending an ASCII or
  -- full-width Latin letter to the converted text.  Downstream filters remove
  -- those dirty candidates, which used to blank the real TSF candidate window
  -- at every syllable boundary.  Always hold the nearest completed conversion
  -- for a clearly Japanese stream.  The association switch only controls the
  -- separate prefix/prediction translator; it must not control continuity of
  -- the core conversion.  Final n is a complete Japanese mora, while q is this
  -- product's explicit spelling for ー.
  if has_unfinished_final then
    if should_prefer_japanese(input) then
      local held, held_input = query_nearest_completed_prefix(input)
      if held then
        log("hold yield input=" .. input .. " seg=" .. tostring(seg.start) ..
            "-" .. tostring(seg._end) .. " input_len=" .. tostring(#input))
        for index, value in ipairs(held) do
          -- Use the established prefix transport marker.  The fuzzy filter
          -- passes this stream through immediately; ordinary Mozc candidates
          -- are buffered for ranking and a held conversion could otherwise
          -- disappear before the real Weasel UI receives it.
          -- Use the established prefix transport marker.  The fuzzy filter
          -- passes this stream through immediately; ordinary Mozc candidates
          -- are buffered for ranking and a held conversion could otherwise
          -- disappear before the real Weasel UI receives it.
          local candidate = Candidate(
            "completion", seg.start, seg._end, value,
            japanese_comment(held_input or input,
                             "[JP:PREFIX]"))
          candidate.quality = math.max(env.initial_quality, 350) - index
          yield(candidate)
        end
      end
      return
    end
  end
  local lexical_sentence = lexical_sentence_candidate(input)
  if lexical_sentence then
    local candidate = Candidate("mozc_v2_lexical_sentence", seg.start,
                                seg._end, lexical_sentence,
                                japanese_comment(input))
    candidate.preedit = input
    candidate.quality = math.max(env.initial_quality, 350) + 50
    yield(candidate)
  end
  local candidates = query(input)
  -- Katakana names are productive phonetic spellings.  Mozc may return only
  -- mixed kanji/kana assemblies, so provide the whole phonetic form even if
  -- no pre-existing dictionary entry contains this particular name.
  if candidates and context:get_option("japanese_particle_wa_ha") then
    candidates = add_contextual_wa_particle_alias(input, candidates,
                                                   seg.start > 0)
    candidates = add_final_wa_particle_alias(input, candidates)
  end
  -- A dictionary-confirmed boundary gets a real Mozc conversion.  Require
  -- its surface to contain the two attested words together, so a speculative
  -- homophone cannot outrank the ordinary conversion merely because it was
  -- produced from an alternative reading.
  local corrected_emitted = false
  for _, option in ipairs(n_boundaries) do
    for _, value in ipairs(query(option.reading) or {}) do
      if matches_n_boundary(value, option) then
        local corrected = Candidate("mozc_v2_preferred_correction", seg.start,
                                    seg._end, value,
                                    japanese_comment(input, nil,
                                                     option.reading))
        corrected.preedit = input
        corrected.quality = 1500
        yield(corrected)
        for index, alternative in ipairs(terminal_dictionary_variants(
            input, value, env.prefix_memory)) do
          local variant = Candidate("mozc_v2_preferred_correction", seg.start,
                                    seg._end, alternative,
                                    japanese_comment(input, nil,
                                                     option.reading))
          variant.preedit = input
          variant.quality = 1500 - index
          yield(variant)
        end
        -- Mozc's later alternatives often vary the first noun (拝観/廃刊)
        -- while keeping the corrected boundary.  One verified whole phrase
        -- is enough; leave those speculative homophones out of the top row.
        corrected_emitted = true
        break
      end
    end
    if corrected_emitted then break end
  end
  local quality = env.initial_quality
  if should_prefer_japanese(input) then
    quality = math.max(quality, 350)
  elseif #input >= 10 then
    if is_complete_pinyin(input) then
      -- A long, fully valid pinyin stream is overwhelmingly likely to be
      -- Chinese.  Keep Mozc available, but below assembled Chinese phrases.
      quality = math.min(quality, 0)
    end
  end
  if #input >= 4 and is_valid_complete_japanese_romaji(input) then
    local reading = romaji_to_hiragana(input)
    if utf8.len(reading) >= 2 then
      local katakana = Candidate("mozc_v2_katakana", seg.start, seg._end,
                                  hiragana_to_katakana(reading),
                                  japanese_comment(input))
      katakana.preedit = input
      -- First real conversion stays ahead; phonetic spelling follows it,
      -- before a long tail of mechanically assembled kanji variants.
      katakana.quality = quality - 1.5
      yield(katakana)
    end
  end
  for _, head in ipairs(japanese_segment_heads(input, seg,
                                                env.prefix_memory)) do
    yield(head)
  end
  if not candidates then return end
  for index, value in ipairs(candidates) do
    local candidate = Candidate("mozc_v2", seg.start, seg._end, value,
                                japanese_comment(input))
    candidate.preedit = input
    candidate.quality = quality - index
    yield(candidate)
  end
  -- Full-code conversion is emitted first, preserving Mozc's ordinary order.
  -- Then offer single-kanji candidates for the first complete reading span.
  -- Their short candidate span leaves the suffix active after selection.
  local name_shaped = false
  for index = 1, math.min(3, #(candidates or {})) do
    if is_short_kanji_word(candidates[index]) then
      name_shaped = true
      break
    end
  end
  if #input >= 6 and is_valid_complete_japanese_romaji(input) and
     (seg.start > 0 or (#dictionary_exact == 0 and name_shaped)) then
    local emitted, seen = 0, {}
    for length = math.min(#input - 2, 8), 2, -1 do
      local head_code = input:sub(1, length)
      local tail_code = input:sub(length + 1)
      if is_valid_complete_japanese_romaji(head_code) and
         is_valid_complete_japanese_romaji(tail_code) then
        for _, value in ipairs(exact_japanese_entries(head_code,
                                                      env.prefix_memory)) do
          local ok, size = pcall(utf8.len, value)
          if ok and size == 1 and not seen[value] then
            local cp = utf8.codepoint(value)
            if cp and ((cp >= 0x3400 and cp <= 0x4dbf) or
                       (cp >= 0x4e00 and cp <= 0x9fff) or
                       (cp >= 0xf900 and cp <= 0xfaff)) then
              seen[value] = true
              emitted = emitted + 1
              local head = Candidate("mozc_v2_character_head", seg.start,
                                     seg.start + length, value,
                                     japanese_comment(head_code))
              head.preedit = head_code
              head.quality = env.initial_quality - 2 - emitted / 100
              yield(head)
              if emitted >= 10 then break end
            end
          end
        end
      end
      if emitted >= 10 then break end
    end
  end
end

return M
