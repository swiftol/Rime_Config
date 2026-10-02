-- EN/JA translation annotations are independently controlled by schema switches.
local RS = string.char(30)
local EN = RS .. "EN:"
local JA = RS .. "JA:"
local HIDDEN_CHINESE_COMMENT = "[[RIME_LANG:ZH]]"
local LANGUAGE_JA = "[[RIME_LANG:JA]]"
local LANGUAGE_CP = "[[RIME_LANG:CP]]"
-- Do not use an ASCII control character here: the Weasel IPC escaping layer
-- can discard it and expose a bare `JR:` suffix.  This textual envelope is
-- removed by Weasel before painting and survives every serialization path.
local JR_OPEN = "[[RIME_JR:"
local JR_CLOSE = "]]"
local japanese_reverse = nil
local reading_cache = {}
local annotation_shards = {}
local annotation_shard_order = {}
local annotation_index_file = nil
local annotation_data_file = nil
local annotation_store_checked = false
local reviewed_surface_readings = nil
local ANNOTATION_SHARD_COUNT = 65536
local MAX_ANNOTATION_SHARDS = 1024

local function annotation_shard_id(text)
  local value = 2166136261
  text = text or ""
  for index = 1, #text do
    value = ((value ~ text:byte(index)) * 16777619) & 0xffffffff
  end
  return value % ANNOTATION_SHARD_COUNT
end

local function load_annotation_shard(id)
  if annotation_shards[id] then return annotation_shards[id] end
  local shard = {}
  -- Keep the 46 MB annotation store outside the Rime user/config tree.
  -- The deployer recursively scans that tree and otherwise consumes gigabytes
  -- of memory reprocessing data that Lua alone owns.
  local local_app_data = os.getenv("LOCALAPPDATA")
  local directory = local_app_data and
      (local_app_data .. "/ZhongriInputMethod/translation_annotations/") or
      (rime_api.get_user_data_dir() .. "/translation_annotations/")
  if not annotation_store_checked then
    annotation_store_checked = true
    local directories = { directory }
    -- Fresh machines read the bundled offline dictionary in shared data.
    -- Open index and data as a pair, never mix different store versions.
    if rime_api.get_shared_data_dir then
      directories[#directories + 1] = rime_api.get_shared_data_dir() ..
          "/translation_annotations/"
    end
    for _, candidate_directory in ipairs(directories) do
      local index = io.open(candidate_directory .. "index-v4.bin", "rb")
      local data = io.open(candidate_directory .. "annotations-v4.tsv", "rb")
      if index and data then
        annotation_index_file, annotation_data_file = index, data
        break
      end
      if index then index:close() end
      if data then data:close() end
    end
  end
  local offset, length
  if annotation_index_file then
    annotation_index_file:seek("set", id * 12)
    local record = annotation_index_file:read(12)
    if record and #record == 12 then
      offset, length = 0, 0
      local multiplier = 1
      for index = 1, 8 do
        offset = offset + record:byte(index) * multiplier
        multiplier = multiplier * 256
      end
      multiplier = 1
      for index = 9, 12 do
        length = length + record:byte(index) * multiplier
        multiplier = multiplier * 256
      end
    end
  end
  if offset and length then
    if annotation_data_file then
      annotation_data_file:seek("set", offset)
      local payload = annotation_data_file:read(length) or ""
      for line in payload:gmatch("[^\n]+") do
      local text, en, ja, reading = line:match(
          "^([^\t]+)\t([^\t]*)\t([^\t]*)\t[^\t]*\t([^\t]*)")
      if text and text ~= "" and ((en and en ~= "") or (ja and ja ~= "")) then
        shard[text] = { en or "", ja or "", reading or "" }
      end
      end
    end
  end
  if #annotation_shard_order >= MAX_ANNOTATION_SHARDS then
    local expired = table.remove(annotation_shard_order, 1)
    annotation_shards[expired] = nil
  end
  annotation_shards[id] = shard
  annotation_shard_order[#annotation_shard_order + 1] = id
  return shard
end

local function complete_annotation_for(text)
  return load_annotation_shard(annotation_shard_id(text))[text]
end

local function reviewed_reading_for(japanese)
  if not japanese or japanese == "" then return nil end
  if not reviewed_surface_readings then
    local readings = {}
    local path = rime_api.get_user_data_dir() ..
        "/annotation_surface_reading_overrides.tsv"
    local file = io.open(path, "r")
    if file then
      for line in file:lines() do
        if line:sub(1, 1) ~= "#" then
          local surface, reading = line:match("^([^\t]+)\t([^\t]+)")
          if surface and reading then readings[surface] = reading end
        end
      end
      file:close()
    end
    reviewed_surface_readings = readings
  end
  return reviewed_surface_readings[japanese]
end

local function clean_upstream_comment(comment)
  -- Mixed-source ordering runs before this filter and appends a hidden
  -- language marker.  Remove transport metadata before parsing the visible
  -- Japanese translation; otherwise the marker becomes part of `ja` and the
  -- reading lookup tries to resolve text such as
  -- `それは違います[[RIME_LANG:ZH]]`.
  return (comment or "")
      :gsub("%[%[RIME_LANG:[A-Z]+%]%]", "")
      :gsub("%[%[RIME_JR:.-%]%]", "")
end

local function parse_annotations(text, raw_comment)
  local reading = (raw_comment or ""):match("%[%[RIME_JR:(.-)%]%]")
  local comment = clean_upstream_comment(raw_comment)
  local en, ja = comment:match("^" .. EN .. "(.-)\n" .. JA .. "(.*)$")
  if not en then en = comment:match("^" .. EN .. "(.*)$") end
  if not ja then ja = comment:match("^" .. JA .. "(.*)$") end
  if not en and not ja then
    local first, second = comment:match("^(.-)\n(.*)$")
    if first and second then en, ja = first, second end
  end
  -- A candidate can carry EN/JA without an embedded reading.  In that case
  -- prefer the packed *whole-word* reading for the same Japanese surface to
  -- reverse lookup's first alternate code (e.g. 化学: かがく, not ばけがく).
  if not en or en == "" or not ja or ja == "" or not reading or reading == "" then
    local extra = complete_annotation_for(text)
    if extra then
      if not en or en == "" then en = extra[1] end
      if not ja or ja == "" then ja = extra[2] end
      if (not reading or reading == "") and ja == extra[2] then
        reading = extra[3]
      end
    end
  end
  -- Reviewed whole-word corrections outrank old Rime dictionary comments.
  -- These 1,001 entries are small enough to cache once per Lua VM, avoiding
  -- a packed-dictionary seek on every ordinary keypress.
  reading = reviewed_reading_for(ja) or reading
  return en, ja, reading
end

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

local function build_comment(en, ja, reading_hint, show_en, show_ja)
  local lines = {}
  if show_en and en and en ~= "" then lines[#lines + 1] = en end
  if show_ja and ja and ja ~= "" then lines[#lines + 1] = ja end
  local result = table.concat(lines, "\n")
  if show_ja and ja and ja ~= "" then
    local reading = reading_hint
    if not reading or reading == "" then reading = japanese_reading(ja) end
    if reading and reading ~= "" then
      result = result .. JR_OPEN .. reading .. JR_CLOSE
    end
  end
  return result
end

local function allow_annotation_for_text(text, show_single_character)
  if show_single_character then return true end
  local ok, length = pcall(utf8.len, text or "")
  return not ok or length ~= 1
end

-- Hiding an annotation must not change the candidate's source language.
-- This matters for one-character Japanese words such as 枕: the colour is
-- driven by this non-visible marker even when its reading is not displayed.
local function preserved_hidden_comment(cand)
  local comment = cand.comment or ""
  -- Japanese fuzzy metadata such as [JF_READING] is consumed later by the UI
  -- and must survive together with the language marker.
  if comment:find(LANGUAGE_JA, 1, true) then return comment end
  if comment:find(LANGUAGE_CP, 1, true) then return comment end
  if comment:find(HIDDEN_CHINESE_COMMENT, 1, true) then
    return HIDDEN_CHINESE_COMMENT
  end
  return HIDDEN_CHINESE_COMMENT
end

local function visible_or_hidden_comment(text, en, ja, reading, show_en, show_ja,
                                         show_single_character)
  if not allow_annotation_for_text(text, show_single_character) then
    return HIDDEN_CHINESE_COMMENT
  end
  local comment = build_comment(en, ja, reading, show_en, show_ja)
  -- An empty ShadowCandidate comment inherits the source comment.  Keep a
  -- non-visible language marker so disabled annotations stay disabled.
  if comment == "" then return HIDDEN_CHINESE_COMMENT end
  -- Preserve Chinese-source classification after rebuilding the comment.
  return comment .. HIDDEN_CHINESE_COMMENT
end

local function annotate_candidate(cand, show_en, show_ja, show_single_character)
  -- A Japanese-source candidate already carries exactly the metadata needed
  -- by the Japanese UI (including fuzzy readings).  Translation annotations
  -- belong to Chinese input candidates; rebuilding this comment would both
  -- waste a packed lookup and incorrectly recolour Japanese Han text white.
  if (cand.comment or ""):find(LANGUAGE_JA, 1, true) then return cand end
  -- Do not pay for packed-dictionary lookup or Japanese reading generation
  -- when annotations cannot be painted.  Single-character annotations are
  -- hidden by default and dominate the expensive first-key candidate page.
  if (not show_en and not show_ja) or
      not allow_annotation_for_text(cand.text, show_single_character) then
    return ShadowCandidate(cand, cand.type, cand.text,
                           preserved_hidden_comment(cand))
  end
  local en, ja, reading = parse_annotations(cand.text, cand.comment)
  if not en and not ja then return cand end
  return ShadowCandidate(
      cand, cand.type, cand.text,
      visible_or_hidden_comment(cand.text, en, ja, reading, show_en, show_ja,
                                show_single_character))
end

-- common_phrase_data.lua is personal data and is intentionally excluded from
-- release installers.  A clean installation must still initialize every Lua
-- component; use an empty phrase map until the settings panel creates it.
local common_phrase_ok, common_phrase_data = pcall(require, "common_phrase_data")
local COMMON_PHRASES = common_phrase_ok and type(common_phrase_data) == "table"
    and common_phrase_data or {}

local function common_phrases_for(code)
  local ordered, lookup = COMMON_PHRASES[code] or {}, {}
  for _, text in ipairs(ordered) do lookup[text] = true end
  return ordered, lookup
end

local function translation_annotation_filter(input, env)
  local context = env.engine.context
  local show_en = context:get_option("show_english_annotation")
  local show_ja = context:get_option("show_japanese_annotation")
  local show_single_character = context:get_option("show_single_character_annotation")

  local phrase_order, phrase_lookup = common_phrases_for(context.input:lower():gsub("%s+", ""))

  -- Normal input has no configured common phrase.  Keep this path fully
  -- streaming: materialising the entire candidate stream made short inputs
  -- and repeated Backspace increasingly slow.
  if #phrase_order == 0 then
    for cand in input:iter() do
      yield(annotate_candidate(cand, show_en, show_ja, show_single_character))
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
    cand = annotate_candidate(cand, show_en, show_ja, show_single_character)

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
