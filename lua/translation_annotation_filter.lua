-- EN/JA translation annotations are independently controlled by schema switches.
local RS = string.char(30)
local EN = RS .. "EN:"
local JA = RS .. "JA:"
-- Do not use an ASCII control character here: the Weasel IPC escaping layer
-- can discard it and expose a bare `JR:` suffix.  This textual envelope is
-- removed by Weasel before painting and survives every serialization path.
local JR_OPEN = "[[RIME_JR:"
local JR_CLOSE = "]]"
local japanese_reverse = nil
local reading_cache = {}

-- Keep this conversion equivalent to the Japanese fuzzy filter.  Reverse
-- lookup stores dictionary codes in romaji, while the user-facing reading is
-- easier to scan in hiragana.
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
  a="あ",i="い",u="う",e="え",o="お",n="ん",['-']="ー",
}

local function romaji_to_hiragana(code)
  code = (code or ""):lower():gsub("[^a-z%-]", "")
  local out, i = {}, 1
  while i <= #code do
    local c, next_c = code:sub(i, i), code:sub(i + 1, i + 1)
    if c == next_c and c:match("[bcdfghjklmpqrstvwxyz]") and c ~= "n" then
      out[#out + 1], i = "っ", i + 1
    elseif c == "n" and
           (next_c == "" or next_c:match("[^aeiouy]") or next_c == "n") then
      out[#out + 1], i = "ん", i + (next_c == "n" and 2 or 1)
    else
      local found = false
      for length = 3, 1, -1 do
        local kana = ROMAJI[code:sub(i, i + length - 1)]
        if kana then
          out[#out + 1], i, found = kana, i + length, true
          break
        end
      end
      if not found then i = i + 1 end
    end
  end
  return table.concat(out)
end

local function direct_japanese_reading(text)
  if not japanese_reverse then
    local ok, reverse = pcall(ReverseLookup, "japanese")
    if ok then japanese_reverse = reverse else return nil end
  end
  local ok, codes = pcall(function() return japanese_reverse:lookup(text) end)
  if not ok or not codes or codes == "" then return nil end
  -- Reverse lookup can return several spellings separated by spaces.  The
  -- first is the dictionary's preferred reading and is enough for the UI.
  local reading = romaji_to_hiragana(codes:match("^%S+"))
  return reading
end

local function kana_to_hiragana(text)
  local out = {}
  for _, cp in utf8.codes(text) do
    if cp >= 0x30A1 and cp <= 0x30F6 then cp = cp - 0x60 end
    out[#out + 1] = utf8.char(cp)
  end
  return table.concat(out)
end

local function is_kana_character(ch)
  local cp = utf8.codepoint(ch)
  return (cp >= 0x3041 and cp <= 0x3096) or
         (cp >= 0x30A1 and cp <= 0x30FA) or cp == 0x30FC
end

local function japanese_reading(text)
  if not text or text == "" then return nil end
  if reading_cache[text] ~= nil then
    return reading_cache[text] ~= false and reading_cache[text] or nil
  end
  local reading = direct_japanese_reading(text)
  if reading and reading ~= "" then
    reading_cache[text] = reading
    return reading
  end

  -- Machine-translated annotations often form valid phrases which are not a
  -- single Japanese dictionary entry.  Split them by the longest readable
  -- prefix and concatenate the readings (e.g. で + 利用 + 可能).
  local chars = {}
  for _, cp in utf8.codes(text) do chars[#chars + 1] = utf8.char(cp) end
  local memo = {}
  local function solve(pos)
    if pos > #chars then return "" end
    if memo[pos] ~= nil then return memo[pos] ~= false and memo[pos] or nil end
    for last = #chars, pos, -1 do
      local piece = table.concat(chars, "", pos, last)
      local piece_reading
      if last == pos and is_kana_character(piece) then
        piece_reading = kana_to_hiragana(piece)
      else
        piece_reading = direct_japanese_reading(piece)
      end
      if piece_reading and piece_reading ~= "" then
        local rest = solve(last + 1)
        if rest ~= nil then
          memo[pos] = piece_reading .. rest
          return memo[pos]
        end
      end
    end
    memo[pos] = false
    return nil
  end
  reading = solve(1)
  reading_cache[text] = reading or false
  return reading
end

local function build_comment(en, ja, show_en, show_ja)
  local lines = {}
  if show_en and en and en ~= "" then lines[#lines + 1] = en end
  if show_ja and ja and ja ~= "" then lines[#lines + 1] = ja end
  local result = table.concat(lines, "\n")
  if show_ja and ja and ja ~= "" then
    local reading = japanese_reading(ja)
    if reading and reading ~= "" then
      result = result .. JR_OPEN .. reading .. JR_CLOSE
    end
  end
  return result
end

local COMMON_PHRASES = require("common_phrase_data")

local function common_phrases_for(code)
  local ordered, lookup = COMMON_PHRASES[code] or {}, {}
  for _, text in ipairs(ordered) do lookup[text] = true end
  return ordered, lookup
end

local function translation_annotation_filter(input, env)
  local context = env.engine.context
  local show_en = context:get_option("show_english_annotation")
  local show_ja = context:get_option("show_japanese_annotation")

  local phrase_order, phrase_lookup = common_phrases_for(context.input:lower():gsub("%s+", ""))

  -- Normal input has no configured common phrase.  Keep this path fully
  -- streaming: materialising the entire candidate stream made short inputs
  -- and repeated Backspace increasingly slow.
  if #phrase_order == 0 then
    for cand in input:iter() do
      local comment = cand.comment or ""
      local en, ja = comment:match("^" .. EN .. "(.-)\n" .. JA .. "(.*)$")
      if not en then en = comment:match("^" .. EN .. "(.*)$") end
      if not ja then ja = comment:match("^" .. JA .. "(.*)$") end
      if not en and not ja then
        local first, second = comment:match("^(.-)\n(.*)$")
        if first and second then en, ja = first, second end
      end
      if en or ja then
        cand = ShadowCandidate(cand, cand.type, cand.text,
                               build_comment(en, ja, show_en, show_ja))
      end
      yield(cand)
    end
    return
  end

  local regular, phrases = {}, {}
  local debug_enabled = #phrase_order > 0
  if debug_enabled then
    log.info("[COMMON_PHRASE] enter code=" .. context.input .. " configured=" .. table.concat(phrase_order, "|"))
  end

  for cand in input:iter() do
    if debug_enabled then
      log.info("[COMMON_PHRASE] input pos=" .. tostring(#regular + 1) .. " type=" .. tostring(cand.type) .. " text=" .. cand.text)
    end
    local comment = cand.comment or ""
    local en, ja = comment:match("^" .. EN .. "(.-)\n" .. JA .. "(.*)$")
    if not en then en = comment:match("^" .. EN .. "(.*)$") end
    if not ja then ja = comment:match("^" .. JA .. "(.*)$") end

    -- Some filters (notably OpenCC/simplifier) rebuild candidates and may
    -- discard the record-separator markers while preserving the two lines.
    -- Translation dictionaries always emit English first and Japanese second,
    -- so retain a robust fallback for that representation.
    if not en and not ja then
      local first, second = comment:match("^(.-)\n(.*)$")
      if first and second then
        en, ja = first, second
      end
    end

    if en or ja then
      cand = ShadowCandidate(cand, cand.type, cand.text,
                             build_comment(en, ja, show_en, show_ja))
    end

    if phrase_lookup[cand.text] then
      if debug_enabled then log.info("[COMMON_PHRASE] MATCH text=" .. cand.text) end
      if not phrases[cand.text] then phrases[cand.text] = cand end
    else
      table.insert(regular, cand)
    end
  end

  -- Keep the normal first candidate, then place configured common phrases in
  -- slots 2/3 like WeChat Input. Long phrases remain full text on commit; the
  -- UI is responsible for visual ellipsis.
  if #phrase_order > 0 and #regular > 0 then
    if debug_enabled then log.info("[COMMON_PHRASE] reorder regular=" .. tostring(#regular)) end
    yield(regular[1])
    for _, text in ipairs(phrase_order) do
      if phrases[text] then yield(phrases[text]) end
    end
    for i = 2, #regular do yield(regular[i]) end
  else
    for _, cand in ipairs(regular) do yield(cand) end
    for _, text in ipairs(phrase_order) do
      if phrases[text] then yield(phrases[text]) end
    end
  end
end

return translation_annotation_filter
