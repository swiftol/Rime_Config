local RULES = {
  { marker = "[JF:SOKUON]", option = "japanese_fuzzy_sokuon" },
  { marker = "[JF:LONG_I]", option = "japanese_fuzzy_long_i" },
  { marker = "[JF:LONG_U]", option = "japanese_fuzzy_long_u" },
  { marker = "[JF:LONG_MARK]", option = "japanese_fuzzy_long_mark" },
  { marker = "[JF:CHI_JI]", option = "japanese_fuzzy_chi_ji" },
  { marker = "[JF:DAKUTEN]", option = "japanese_fuzzy_dakuten" },
  { marker = "[JF:KE_KAI]", option = "japanese_fuzzy_ke_kai" },
  { marker = "[JF:KE_KAE_GAE]", option = "japanese_fuzzy_ke_kae_gae" },
  { marker = "[JF:SEI_SAI]", option = "japanese_fuzzy_sei_sai" },
}

local fuzzy_learning = require("japanese_fuzzy_learning")
local continuation_context = require("japanese_continuation_context")

local COMBINED_MARKER = "[JF:COMBINED]"
local CUSTOM_MARKER = "[JF:CUSTOM]"
local PREFIX_MARKER = "[JP:PREFIX]"

local ROMAJI = {
  kya="きゃ", kyu="きゅ", kyo="きょ", gya="ぎゃ", gyu="ぎゅ", gyo="ぎょ",
  sha="しゃ", shu="しゅ", sho="しょ", sya="しゃ", syu="しゅ", syo="しょ",
  ja="じゃ", ji="じ", ju="じゅ", jo="じょ", jya="じゃ", jyu="じゅ", jyo="じょ",
  cha="ちゃ", chu="ちゅ", cho="ちょ", cya="ちゃ", cyu="ちゅ", cyo="ちょ",
  nya="にゃ", nyu="にゅ", nyo="にょ", hya="ひゃ", hyu="ひゅ", hyo="ひょ",
  bya="びゃ", byu="びゅ", byo="びょ", pya="ぴゃ", pyu="ぴゅ", pyo="ぴょ",
  mya="みゃ", myu="みゅ", myo="みょ", rya="りゃ", ryu="りゅ", ryo="りょ",
  tsa="つぁ", tsi="つぃ", tse="つぇ", tso="つぉ", she="しぇ", je="じぇ", che="ちぇ",
  thi="てぃ", dhi="でぃ", tya="てゃ", tyu="てゅ", tyo="てょ",
  fa="ふぁ", fi="ふぃ", fe="ふぇ", fo="ふぉ", va="ゔぁ", vi="ゔぃ", vu="ゔ", ve="ゔぇ", vo="ゔぉ",
  kwa="くぁ", kwi="くぃ", kwe="くぇ", kwo="くぉ", gwa="ぐぁ", swi="すぃ", zwi="ずぃ",
  shi="し", chi="ち", tsu="つ", dzu="づ", dji="ぢ",
  ka="か", ki="き", ku="く", ke="け", ko="こ", ga="が", gi="ぎ", gu="ぐ", ge="げ", go="ご",
  sa="さ", si="し", su="す", se="せ", so="そ", za="ざ", zi="じ", zu="ず", ze="ぜ", zo="ぞ",
  ta="た", ti="ち", tu="つ", te="て", to="と", da="だ", di="ぢ", du="づ", de="で", ['do']="ど",
  na="な", ni="に", nu="ぬ", ne="ね", no="の", ha="は", hi="ひ", hu="ふ", fu="ふ", he="へ", ho="ほ",
  ba="ば", bi="び", bu="ぶ", be="べ", bo="ぼ", pa="ぱ", pi="ぴ", pu="ぷ", pe="ぺ", po="ぽ",
  ma="ま", mi="み", mu="む", me="め", mo="も", ya="や", yu="ゆ", yo="よ",
  ra="ら", ri="り", ru="る", re="れ", ro="ろ", wa="わ", wi="うぃ", we="うぇ", wo="を",
  a="あ", i="い", u="う", e="え", o="お", n="ん", ['-']="ー",
}

local function romaji_to_hiragana(code)
  code = (code or ""):lower():gsub("[^a-z%-]", "")
  if code == "" then return "" end
  local out, i = {}, 1
  while i <= #code do
    local c = code:sub(i, i)
    local next_c = code:sub(i + 1, i + 1)
    if c == next_c and c:match("[bcdfghjklmpqrstvwxyz]") and c ~= "n" then
      out[#out + 1] = "っ"
      i = i + 1
    elseif c == "n" and (next_c == "" or next_c:match("[^aeiouy]") or next_c == "n") then
      out[#out + 1] = "ん"
      i = i + (next_c == "n" and 2 or 1)
    else
      local found = false
      for length = 3, 1, -1 do
        local part = code:sub(i, i + length - 1)
        if ROMAJI[part] then
          out[#out + 1] = ROMAJI[part]
          i = i + length
          found = true
          break
        end
      end
      if not found then i = i + 1 end
    end
  end
  return table.concat(out)
end

-- In the mixed schema q is the user's explicit spelling for the Japanese
-- long-vowel mark (ー), while the Japanese dictionary stores that mark as
-- "-".  Treat those two spellings as identical for exact ranking.  Without
-- this normalization a fresh session with the fuzzy master switch enabled
-- demotes koqhiq -> ko-hi- as a fuzzy result; switching to the standalone
-- Japanese schema merely hides the bug by resetting the session option.
local function canonical_exact_spelling(code)
  return (code or ""):lower():gsub("[%s']+", ""):gsub("q", "-"):gsub("ー", "-")
end

-- Return true only when every byte of the spelling can be consumed as
-- Japanese romaji.  This separates real composed Japanese such as
-- `warui desu` from Chinese pinyin that the Japanese translator happened to
-- segment into a sentence, for example `xi anzai shuru` (`xi` is not a
-- Japanese syllable).
local function is_valid_japanese_romaji(code)
  code = (code or ""):lower():gsub("[%s']+", "")
  if code == "" then return false end
  local i = 1
  while i <= #code do
    local c = code:sub(i, i)
    local next_c = code:sub(i + 1, i + 1)
    if c == next_c and c:match("[bcdfghjklmpqrstvwxyz]") and c ~= "n" then
      i = i + 1
    elseif c == "n" and
           (next_c == "" or next_c:match("[^aeiouy]") or next_c == "n") then
      i = i + (next_c == "n" and 2 or 1)
    else
      local found = false
      for length = 3, 1, -1 do
        local part = code:sub(i, i + length - 1)
        if ROMAJI[part] then
          i = i + length
          found = true
          break
        end
      end
      if not found then return false end
    end
  end
  return true
end

local function fuzzy_spelling_covers_input(marker, spelling, typed)
  spelling = (spelling or ""):lower():gsub("[%s']+", "")
  if canonical_exact_spelling(spelling) == canonical_exact_spelling(typed) then
    return false
  end
  local variants = { spelling }
  local function add_optional_replacements(from, to)
    -- Each long vowel can be omitted independently.  A global gsub loses
    -- valid mixed spellings such as daitouryou -> daitouryo, where only the
    -- second "ou" is omitted.  Expand all practical subsets, with a small
    -- cap to keep pathological dictionary entries cheap.
    local seen = { [spelling] = true }
    local queue = { spelling }
    local cursor = 1
    while cursor <= #queue and #queue < 32 do
      local current = queue[cursor]
      cursor = cursor + 1
      local start = 1
      while #queue < 32 do
        local first, last = current:find(from, start, true)
        if not first then break end
        local changed = current:sub(1, first - 1) .. to .. current:sub(last + 1)
        if not seen[changed] then
          seen[changed] = true
          queue[#queue + 1] = changed
          variants[#variants + 1] = changed
        end
        start = first + 1
      end
    end
  end
  if marker == "[JF:LONG_U]" then
    add_optional_replacements("ou", "o")
    add_optional_replacements("uu", "u")
  elseif marker == "[JF:LONG_I]" then
    variants[#variants + 1] = spelling:gsub("ii", "i")
    variants[#variants + 1] = spelling:gsub("ei", "e")
    variants[#variants + 1] = spelling:gsub("ei", "e"):gsub("ii", "i")
    variants[#variants + 1] = spelling:gsub("kae", "ke")
    variants[#variants + 1] = spelling:gsub("gae", "ge")
  elseif marker == "[JF:LONG_MARK]" then
    variants[#variants + 1] = spelling:gsub("%-", "")
  elseif marker == "[JF:SOKUON]" then
    variants[#variants + 1] = spelling:gsub("([bcdfghjklmpqrstvwxyz])%1", "%1")
  elseif marker == "[JF:CHI_JI]" then
    local swaps = {
      {"cha", "ja"}, {"chu", "ju"}, {"cho", "jo"},
      {"cho", "chu"},
      {"sho", "shu"},
      {"chi", "ji"}, {"tya", "dya"}, {"tyu", "dyu"},
      {"tyo", "dyo"}, {"ti", "di"},
    }
    for _, pair in ipairs(swaps) do
      variants[#variants + 1] = spelling:gsub(pair[1], pair[2])
      variants[#variants + 1] = spelling:gsub(pair[2], pair[1])
    end
  elseif marker == "[JF:DAKUTEN]" then
    local swaps = {
      {"t", "d"}, {"k", "g"}, {"s", "z"},
      {"p", "b"}, {"p", "h"},
    }
    local function add_single_swaps(from, to)
      local start = 1
      while true do
        local first, last = spelling:find(from, start, true)
        if not first then break end
        variants[#variants + 1] = spelling:sub(1, first - 1) .. to .. spelling:sub(last + 1)
        start = first + 1
      end
    end
    for _, pair in ipairs(swaps) do
      add_single_swaps(pair[1], pair[2])
      add_single_swaps(pair[2], pair[1])
    end
  elseif marker == "[JF:KE_KAI]" then
    variants[#variants + 1] = spelling:gsub("kai", "ke")
  elseif marker == "[JF:KE_KAE_GAE]" then
    variants[#variants + 1] = spelling:gsub("kae", "ke")
    variants[#variants + 1] = spelling:gsub("gae", "ke")
  elseif marker == "[JF:SEI_SAI]" then
    variants[#variants + 1] = spelling:gsub("sei", "sai")
    variants[#variants + 1] = spelling:gsub("sai", "sei")
  end
  for _, variant in ipairs(variants) do
    if variant == typed then return true end
  end
  return false
end

local function combined_rule_for(spelling, typed, context)
  spelling = (spelling or ""):lower():gsub("[%s']+", "")
  if canonical_exact_spelling(spelling) == canonical_exact_spelling(typed) then
    return nil
  end
  -- Accept the translator's bounded leading-vowel correction.  The master
  -- Japanese fuzzy switch is the control for this rule.
  if context:get_option("japanese_fuzzy_match") and #typed >= 5 and
     typed:sub(1, 1):match("[aeiou]") and spelling == typed:sub(2) then
    return { marker = "[JF:INITIAL_VOWEL]", option = "japanese_fuzzy_match" }
  end
  for _, rule in ipairs(RULES) do
    if context:get_option(rule.option) and
       fuzzy_spelling_covers_input(rule.marker, spelling, typed) then
      return rule
    end
  end
  -- Resolve two independently omitted long-vowel letters in a complete
  -- dictionary spelling, without accepting arbitrary edit-distance matches.
  if #typed >= 6 and #typed <= 24 then
    local contractions = {}
    if context:get_option("japanese_fuzzy_long_i") then
      contractions[#contractions + 1] = { "ei", "e" }
      contractions[#contractions + 1] = { "ii", "i" }
    end
    if context:get_option("japanese_fuzzy_long_u") then
      contractions[#contractions + 1] = { "ou", "o" }
      contractions[#contractions + 1] = { "uu", "u" }
    end
    local once, seen = {}, {}
    for _, pair in ipairs(contractions) do
      local start = 1
      while true do
        local first, last = spelling:find(pair[1], start, true)
        if not first then break end
        local variant = spelling:sub(1, first - 1) .. pair[2] .. spelling:sub(last + 1)
        if not seen[variant] then
          seen[variant] = true
          once[#once + 1] = variant
        end
        start = first + 1
      end
    end
    for _, first_variant in ipairs(once) do
      for _, pair in ipairs(contractions) do
        local start = 1
        while true do
          local first, last = first_variant:find(pair[1], start, true)
          if not first then break end
          local variant = first_variant:sub(1, first - 1) .. pair[2] ..
                          first_variant:sub(last + 1)
          if variant == typed then
            return { marker = "[JF:DOUBLE_VOWEL]", option = "japanese_fuzzy_match" }
          end
          start = first + 1
        end
      end
    end
  end
  -- Long-vowel omission and a voiced/unvoiced consonant mistake may occur in
  -- the same word even when no small-tsu correction is involved.  Compare a
  -- contracted spelling after changing one consonant in either direction.
  if context:get_option("japanese_fuzzy_long_u") and
     context:get_option("japanese_fuzzy_dakuten") then
    local contracted = spelling:gsub("ou", "o"):gsub("uu", "u")
    local swaps = { {"p", "b"}, {"p", "h"}, {"t", "d"}, {"k", "g"}, {"s", "z"} }
    for _, pair in ipairs(swaps) do
      for _, direction in ipairs({ pair, { pair[2], pair[1] } }) do
        local start = 1
        while true do
          local first, last = contracted:find(direction[1], start, true)
          if not first then break end
          local variant = contracted:sub(1, first - 1) .. direction[2] .. contracted:sub(last + 1)
          if variant == typed then
            return { marker = "[JF:COMBINED_DAKUTEN_LONG_U]", option = "japanese_fuzzy_dakuten" }
          end
          start = first + 1
        end
      end
    end
  end
  -- Some user spellings combine an omitted small-tsu consonant with a
  -- voiced/semi-voiced substitution, e.g. shuppatsu -> shupatsu -> shubatsu.
  if context:get_option("japanese_fuzzy_sokuon") and
     context:get_option("japanese_fuzzy_dakuten") then
    local degeminated = spelling:gsub("([bcdfghjklmpqrstvwxyz])%1", "%1")
    local consonant_variants = {
      degeminated:gsub("p", "b"), degeminated:gsub("b", "p"),
      degeminated:gsub("p", "h"), degeminated:gsub("h", "p"),
      degeminated:gsub("t", "d"), degeminated:gsub("d", "t"),
      degeminated:gsub("k", "g"), degeminated:gsub("g", "k"),
      degeminated:gsub("s", "z"), (degeminated:gsub("z", "s")),
    }
    for _, variant in ipairs(consonant_variants) do
      if variant == typed then
        return { marker = "[JF:COMBINED_CONSONANT]", option = "japanese_fuzzy_dakuten" }
      end
      -- A correction may simultaneously omit the long -u after -o:
      -- happyou -> hapyou -> habyou -> habyo.
      if context:get_option("japanese_fuzzy_long_u") then
        local without_long_u = variant:gsub("ou", "o"):gsub("uu", "u")
        if without_long_u == typed then
          return { marker = "[JF:COMBINED_SOKUON_DAKUTEN_LONG_U]", option = "japanese_fuzzy_dakuten" }
        end
      end
    end
  end
  -- Vowel contraction plus dakuten confusion, e.g.
  -- kigaeru -> kigeru -> kikeru.  Replace only one consonant so the
  -- initial correct k is not changed as well.
  if context:get_option("japanese_fuzzy_long_i") and
     context:get_option("japanese_fuzzy_dakuten") then
    local contracted = spelling:gsub("kae", "ke"):gsub("gae", "ge")
    local swaps = { {"k", "g"}, {"s", "z"}, {"t", "d"}, {"p", "b"}, {"p", "h"} }
    for _, pair in ipairs(swaps) do
      for _, direction in ipairs({pair, {pair[2], pair[1]}}) do
        local start = 1
        while true do
          local first, last = contracted:find(direction[1], start, true)
          if not first then break end
          local variant = contracted:sub(1, first - 1) .. direction[2] .. contracted:sub(last + 1)
          if variant == typed then
            return { marker = "[JF:COMBINED_VOWEL_DAKUTEN]", option = "japanese_fuzzy_dakuten" }
          end
          start = first + 1
        end
      end
    end
  end
  if context:get_option("japanese_fuzzy_hu_fu") then
    if spelling:gsub("fu", "hu") == typed or spelling:gsub("hu", "fu") == typed then
      return { marker = "[JF:FU_HU]", option = "japanese_fuzzy_hu_fu" }
    end
  end
  if context:get_option("japanese_fuzzy_shu_sho") then
    if spelling:gsub("shu", "sho") == typed or spelling:gsub("sho", "shu") == typed then
      return { marker = "[JF:SHU_SHO]", option = "japanese_fuzzy_shu_sho" }
    end
  end
  if context:get_option("japanese_fuzzy_ke_kai") then
    if spelling:gsub("kai", "ke") == typed then
      return { marker = "[JF:KE_KAI]", option = "japanese_fuzzy_ke_kai" }
    end
  end
  if context:get_option("japanese_fuzzy_ke_kae_gae") then
    if spelling:gsub("kae", "ke") == typed or
       spelling:gsub("gae", "ke") == typed then
      return { marker = "[JF:KE_KAE_GAE]", option = "japanese_fuzzy_ke_kae_gae" }
    end
  end
  if context:get_option("japanese_fuzzy_sei_sai") then
    if spelling:gsub("sei", "sai") == typed or
       spelling:gsub("sai", "sei") == typed then
      return { marker = "[JF:SEI_SAI]", option = "japanese_fuzzy_sei_sai" }
    end
  end
  return nil
end

local LANGUAGE_JA = "[[RIME_LANG:JA]]"
local LANGUAGE_ZH = "[[RIME_LANG:ZH]]"
local tag_candidate_language

-- Frequently typed Japanese particles should be immediately visible in the
-- mixed candidate list.  Keep the first exact Chinese candidate, then place
-- the matching particle, followed by the remaining exact candidates.
local PRIORITY_PARTICLES = {
  ha="は", wa="は", ka="か", ga="が", no="の", wo="を", o="を",
  ni="に", he="へ", e="へ", to="と", de="で", mo="も", ya="や",
  kara="から", yori="より", koso="こそ", sae="さえ", shika="しか",
  dake="だけ", made="まで", hodo="ほど", kurai="くらい", gurai="ぐらい",
  nado="など", demo="でも", tte="って", node="ので", noni="のに",
  kedo="けど", temo="ても", nagara="ながら", ne="ね", yo="よ",
  na="な", zo="ぞ", ze="ぜ", sa="さ", kana="かな", kashira="かしら",
}

local function is_priority_particle(cand, typed)
  local wanted = PRIORITY_PARTICLES[typed]
  if not wanted or cand.text ~= wanted then return false end
  local preedit = (cand.preedit or ""):lower():gsub("[%s']+", "")
  local quality = tonumber(cand.quality) or 0
  return preedit == typed and quality >= 50 and quality < 299
end

local function yield_exact_with_particle_second(items, typed)
  if #items == 0 then return end
  -- Exact Japanese candidates come from two independent translators.  The
  -- compact dictionary can arrive first (kana/katakana, quality 200), while
  -- Mozc arrives slightly later with the normal conversion order.  Enabling
  -- an extra fuzzy lookup changes that timing, so move each Mozc candidate
  -- left across only those earlier kana dictionary candidates.  A Han-only
  -- Chinese candidate is an intentional barrier: never reorder across it.
  for i = 2, #items do
    local current = items[i]
    local current_quality = tonumber(current.quality) or 0
    -- ShadowCandidate preserves relative score but Rime may normalize both the
    -- type name and the absolute quality scale.  Only a higher-scored exact
    -- candidate may cross the low-priority kana fallback immediately before it.
    if current_quality > 0 then
      local j = i - 1
      while j >= 1 do
        local previous = items[j]
        local previous_text = previous.text or ""
        local previous_quality = tonumber(previous.quality) or 0
        local previous_has_kana = false
        for _, cp in utf8.codes(previous_text) do
          if cp >= 0x3040 and cp <= 0x30ff then
            previous_has_kana = true
            break
          end
        end
        if not previous_has_kana or previous_quality >= current_quality then
          break
        end
        items[j + 1] = previous
        j = j - 1
      end
      items[j + 1] = current
    end
  end
  -- Shared Han spellings can be emitted once by the Chinese translator and
  -- once by the Japanese translator.  The final uniquifier keeps the first
  -- copy, so promote that copy to Japanese whenever an exact Japanese source
  -- for the same text exists in this head (makura -> 枕 is the typical case).
  local exact_japanese_text = {}
  for _, cand in ipairs(items) do
    local preedit = (cand.preedit or ""):lower():gsub("[%s']+", "")
    local quality = tonumber(cand.quality) or 0
    local comment = cand.comment or ""
    local japanese_source = (cand.type or ""):match("^mozc_v2") or
      comment:find(LANGUAGE_JA, 1, true) or
      (quality >= 50 and quality < 299)
    if preedit == typed and japanese_source then
      exact_japanese_text[cand.text] = true
    end
  end
  local function tagged(cand)
    if exact_japanese_text[cand.text] then
      local comment = (cand.comment or "")
        :gsub("%[%[RIME_LANG:JA%]%]", "")
        :gsub("%[%[RIME_LANG:ZH%]%]", "")
      return ShadowCandidate(cand, cand.type, cand.text, comment .. LANGUAGE_JA)
    end
    return tag_candidate_language(cand)
  end
  local preferred = {}
  local ordinary = {}
  for _, cand in ipairs(items) do
    if is_priority_particle(cand, typed) then
      preferred[#preferred + 1] = cand
    else
      ordinary[#ordinary + 1] = cand
    end
  end
  if #ordinary > 0 then
    yield(tagged(ordinary[1]))
    for _, cand in ipairs(preferred) do yield(tagged(cand)) end
    for i = 2, #ordinary do yield(tagged(ordinary[i])) end
  else
    for _, cand in ipairs(preferred) do yield(tagged(cand)) end
  end
end

local function strip_language_metadata(value)
  return (value or ""):gsub("%[%[RIME_LANG:JA%]%]", "")
                       :gsub("%[%[RIME_LANG:ZH%]%]", "")
end

local function has_script(text, first, last)
  for _, cp in utf8.codes(text or "") do
    if cp >= first and cp <= last then return true end
  end
  return false
end

tag_candidate_language = function(cand)
  local comment = cand.comment or ""
  if comment:find("[[RIME_LANG:", 1, true) then return cand end
  local quality = tonumber(cand.quality) or 0
  local has_kana = has_script(cand.text, 0x3040, 0x30ff)
  local has_han = has_script(cand.text, 0x3400, 0x9fff) or
                  has_script(cand.text, 0xf900, 0xfaff)
  local fuzzy_marked = comment:find("[JF", 1, true) == 1 or
                         comment:find(COMBINED_MARKER, 1, true) == 1
  -- Main Japanese streams use qualities around 100/200.  Chinese exact is
  -- around 300 and the assembled Chinese stream around 1.2.  This preserves
  -- the language of shared Han spellings such as 社会, where glyph inspection
  -- alone cannot distinguish Japanese from Chinese.
  local is_japanese = (cand.type or ""):match("^mozc_v2") or
                      has_kana or fuzzy_marked or
                      (quality >= 50 and quality < 299)
  local marker = is_japanese and LANGUAGE_JA or (has_han and LANGUAGE_ZH or "")
  if marker == "" then return cand end
  return ShadowCandidate(cand, cand.type, cand.text, comment .. marker)
end

local function japanese_fuzzy_filter(input, env)
  local context = env.engine.context
  if context:get_option("japanese_input_table_enabled") then
    for cand in input:iter() do yield(cand) end
    return
  end
  local master_enabled = context:get_option("japanese_fuzzy_match")
  -- After a partial candidate has been selected context.input still contains
  -- the whole original spelling.  Ranking, however, must compare candidates
  -- with the active (unselected) segment only.
  local raw_input = context.input or ""
  local active = context.composition and context.composition:back() or nil
  local active_input = raw_input
  if active and active.start and active.start > 0 then
    active_input = raw_input:sub(active.start + 1, active._end or #raw_input)
  end
  local typed = active_input:lower():gsub("[%s']+", "")
  local protected_suffix = nil
  for _, suffix in ipairs({ "desuka", "masuka", "desu", "masu" }) do
    if typed:sub(-#suffix) == suffix then
      protected_suffix = suffix
      break
    end
  end
  local protected_kana_suffix = protected_suffix and ({
    desuka = "ですか", masuka = "ますか",
    desu = "です", masu = "ます",
  })[protected_suffix] or nil
  local continuation_ja = continuation_context.selected_japanese_prefix(context)
    ~= nil

  -- librime treats an unfinished `sho` as a prefix of Chinese `shou` even
  -- when completion, strict spelling and every correction algebra are off.
  -- Translator quality identifies the two Chinese streams unambiguously:
  -- exact Chinese ~=300, assembled Chinese ~=1.2, exact Japanese ~=200.
  local function is_hidden_chinese_completion(cand)
    if typed:sub(-3) ~= "sho" then return false end
    local quality = tonumber(cand.quality) or 0
    return quality >= 299 or (quality >= 1.19 and quality < 2)
  end

  -- With fuzzy matching disabled, do not build ranking buckets at all.
  -- Drop the combined translator's marked candidates lazily and pass every
  -- normal candidate through immediately.
  if not master_enabled then
    local head = {}
    local HEAD_LIMIT = 48
    local function yield_clean_head(items)
      local full_chinese = false
      if #typed >= 8 then
        for _, item in ipairs(items) do
          local text = item.text or ""
          local preedit = (item.preedit or ""):lower():gsub("[%s']+", "")
          local has_han = has_script(text, 0x3400, 0x9fff) or
                          has_script(text, 0xf900, 0xfaff)
          local has_kana = has_script(text, 0x3040, 0x30ff)
          if preedit == typed and has_han and not has_kana then
            full_chinese = true
            break
          end
        end
      end
      local cleaned = {}
      for _, item in ipairs(items) do
        local is_japanese_sentence = item.type == "sentence" and
          has_script(item.text or "", 0x3040, 0x30ff)
        local bogus_japanese_sentence = is_japanese_sentence and
          not is_valid_japanese_romaji(typed)
        if not (full_chinese and bogus_japanese_sentence) then
          cleaned[#cleaned + 1] = item
        end
      end
      yield_exact_with_particle_second(cleaned, typed)
    end
    for cand in input:iter() do
      local comment = cand.comment or ""
      if comment:sub(1, #PREFIX_MARKER) == PREFIX_MARKER then
        local visible_comment = comment:sub(#PREFIX_MARKER + 1)
        local output_type = cand.type == "mozc_v2_polite_completion" and
                            cand.type or "completion"
        yield(Candidate(output_type, cand.start, cand._end, cand.text,
          visible_comment .. LANGUAGE_JA))
      elseif comment:sub(1, #CUSTOM_MARKER) == CUSTOM_MARKER then
        if #head > 0 then
          yield_clean_head(head)
          head = {}
        end
        local payload = comment:sub(#CUSTOM_MARKER + 1)
        local spelling = payload:match("^(.-)%[%[JF_TYPED:.-%]%]$") or payload
        spelling = strip_language_metadata(spelling)
        local kana = romaji_to_hiragana(spelling)
        local typed_kana = romaji_to_hiragana(typed)
        local ui_comment = kana ~= "" and ("[JF_READING]" .. kana) or ""
        if ui_comment ~= "" and typed_kana ~= "" then
          ui_comment = ui_comment .. "[[RIME_FUZZY_TYPED:" .. typed_kana .. "]]"
        end
        local converted = ShadowCandidate(cand, cand.type, cand.text, ui_comment)
        yield(tag_candidate_language(converted))
      else
        local is_fuzzy = comment:sub(1, #COMBINED_MARKER) == COMBINED_MARKER
        if not is_fuzzy then
          for _, rule in ipairs(RULES) do
            if comment:sub(1, #rule.marker) == rule.marker then
              is_fuzzy = true
              break
            end
          end
        end
        local quality = tonumber(cand.quality) or 0
        local comment = cand.comment or ""
        local japanese_source = (cand.type or ""):match("^mozc_v2") or
          comment:find(LANGUAGE_JA, 1, true) or
          has_script(cand.text, 0x3040, 0x30ff) or
          (quality >= 50 and quality < 299)
        local chinese_source = comment:find(LANGUAGE_ZH, 1, true) or
          (not japanese_source and
           (quality >= 299 or (quality >= 1.19 and quality < 2) or
            has_script(cand.text, 0x3400, 0x9fff) or
            has_script(cand.text, 0xf900, 0xfaff)))
        if not is_fuzzy and not is_hidden_chinese_completion(cand) and
           not (continuation_ja and chinese_source) then
          if #head < HEAD_LIMIT then
            head[#head + 1] = cand
          else
            if #head > 0 then
              yield_clean_head(head)
              head = {}
            end
            yield(tag_candidate_language(cand))
          end
        end
      end
    end
    if #head > 0 then yield_clean_head(head) end
    return
  end

  local exact, fuzzy_kanji, fuzzy_other, fuzzy_lexical = {}, {}, {}, {}
  local assembled, assembled_japanese, japanese_sentence = {}, {}, {}
  local completion, completion_japanese = {}, {}
  -- A long Chinese spelling can also be segmented by the Japanese translator
  -- into a bogus mixed-script sentence (for example xianzaishuru ->
  -- ぃ安西手る).  Remember that a genuine whole-input Chinese candidate
  -- exists so those broad Japanese corrections can be hidden at flush time.
  local has_full_exact_chinese = false
  local buffered = 0
  local lexical_selected = false
  -- The deepest validated whole-word correction currently starts at #59.
  -- 96 preserves enough headroom for ranking without scanning hundreds or
  -- thousands of candidates on every key and Backspace.
  -- Longer romanizations can have more than 100 exact Chinese/Japanese
  -- candidates ahead of a valid fuzzy whole-word match (shajou -> 社長 was
  -- #150).  Scan deeper only for sufficiently specific input; keeping the
  -- old limit for short input avoids bringing back Backspace latency.
  local BUFFER_LIMIT = (#typed >= 5) and 192 or 96
  -- Full polite expressions can be emitted after many Chinese syllable and
  -- short Japanese prefix candidates.  Keep enough headroom to rank the
  -- complete exact sentence (悪いですか) before those prefixes.
  if protected_suffix then BUFFER_LIMIT = math.max(BUFFER_LIMIT, 320) end
  -- hu/fu corrections can sit behind a large number of short Chinese
  -- candidates (huan -> 不安 was raw #255).  Deepen only this requested
  -- consonant pair instead of making every short input expensive.
  if typed:find("hu", 1, true) or typed:find("fu", 1, true) then
    BUFFER_LIMIT = 320
  end
  -- ke -> kai whole-word corrections can also sit behind many short Chinese
  -- candidates (kegi -> kaigi / 会議).  Keep them inside the ranked fuzzy
  -- bucket instead of yielding them late after the first buffer flush.
  if context:get_option("japanese_fuzzy_ke_kai") and
     typed:find("ke", 1, true) then
    BUFFER_LIMIT = math.max(BUFFER_LIMIT, 320)
  end
  -- A final Japanese -ou is often typed without u.  Whole-word matches such
  -- as danjo -> danjou/壇上 may start just beyond the normal 192 window.
  if context:get_option("japanese_fuzzy_long_u") and typed:sub(-1) == "o" then
    BUFFER_LIMIT = math.max(BUFFER_LIMIT, 256)
  end

  local function has_kanji(text)
    for _, cp in utf8.codes(text or "") do
      if (cp >= 0x3400 and cp <= 0x9fff) or (cp >= 0xf900 and cp <= 0xfaff) then
        return true
      end
    end
    return false
  end

  local function flush()
    -- A verified whole-word entry for the unmodified spelling outranks
    -- speculative long-vowel insertions.  Otherwise hanasu -> hanasuu / 花数
    -- can replace the real verb 話す even though the user made no typo.
    for _, item in ipairs(exact) do
      if item.type == "mozc_v2_dictionary_exact" then
        fuzzy_lexical = {}
        break
      end
    end
    -- Mozc can synthesize many implausible homophones after it has already
    -- returned a whole-word dictionary entry.  For a long, complete Japanese
    -- spelling, show its exact lexical alternatives instead of those machine
    -- assembled fragments.  Short ambiguous readings retain the full menu.
    local lexical_han, lexical_kana = false, false
    for _, item in ipairs(exact) do
      if item.type == "mozc_v2_dictionary_exact" then
        local text = item.text or ""
        local has_kana = has_script(text, 0x3040, 0x30ff)
        if has_kanji(text) and not has_kana then lexical_han = true end
        if has_kana and not has_kanji(text) then lexical_kana = true end
      end
    end
    if #typed >= 8 and not protected_suffix and lexical_han and lexical_kana and
       is_valid_japanese_romaji(typed) and exact[1] and
       exact[1].type == "mozc_v2_dictionary_exact" then
      lexical_selected = true
      local emitted = {}
      for _, item in ipairs(exact) do
        if item.type == "mozc_v2_dictionary_exact" and not emitted[item.text] then
          emitted[item.text] = true
          yield(tag_candidate_language(item))
        end
      end
      exact, fuzzy_kanji, fuzzy_other, fuzzy_lexical = {}, {}, {}, {}
      assembled, assembled_japanese, japanese_sentence = {}, {}, {}
      completion, completion_japanese = {}, {}
      return
    end
    -- Full-word long-vowel corrections (one or two omissions) outrank raw
    -- sentence segmentation such as せ + 少年 or 背 + 初年.  Show only exact
    -- dictionary words, not the broad generated fragments, for this frame.
    if #fuzzy_lexical > 0 and #typed >= 6 and not protected_suffix and
       not (typed:find("ke", 1, true) and
            (context:get_option("japanese_fuzzy_ke_kai") or
             context:get_option("japanese_fuzzy_ke_kae_gae"))) then
      lexical_selected = true
      local emitted = {}
      for pass = 1, 2 do
        for _, item in ipairs(fuzzy_lexical) do
          if has_kanji(item.text) == (pass == 1) and not emitted[item.text] then
            emitted[item.text] = true
            yield(tag_candidate_language(item))
          end
        end
      end
      exact, fuzzy_kanji, fuzzy_other, fuzzy_lexical = {}, {}, {}, {}
      assembled, assembled_japanese, japanese_sentence = {}, {}, {}
      completion, completion_japanese = {}, {}
      return
    end
    -- Short codes and dedicated ke→kai/kae rules retain their established
    -- ordering.  Their exact restored entries still belong in normal fuzzy
    -- buckets; otherwise a valid word such as regi→礼儀 disappears.
    for _, item in ipairs(fuzzy_lexical) do
      local bucket = has_kanji(item.text) and fuzzy_kanji or fuzzy_other
      bucket[#bucket + 1] = item
    end
    if protected_suffix and #exact > 1 then
      local kana_suffix = ({
        desuka = "ですか", masuka = "ますか",
        desu = "です", masu = "ます",
      })[protected_suffix]
      local reordered, emitted = {}, {}
      -- Preserve the contract: first exact Chinese candidate, then the
      -- natural full Japanese sentence, then the remaining exact results.
      for i, item in ipairs(exact) do
        local text = item.text or ""
        local has_han_text = has_script(text, 0x3400, 0x9fff) or
                             has_script(text, 0xf900, 0xfaff)
        local has_kana_text = has_script(text, 0x3040, 0x30ff)
        if has_han_text and not has_kana_text then
          reordered[#reordered + 1] = item
          emitted[i] = true
          break
        end
      end
      for i = 1, #exact do
        local text = exact[i].text or ""
        if not emitted[i] and kana_suffix and text:sub(-#kana_suffix) == kana_suffix then
          reordered[#reordered + 1] = exact[i]
          emitted[i] = true
        end
      end
      for i = 1, #exact do
        if not emitted[i] then
          reordered[#reordered + 1] = exact[i]
        end
      end
      yield_exact_with_particle_second(reordered, typed)
    else
      yield_exact_with_particle_second(exact, typed)
    end
    table.sort(fuzzy_kanji, function(a, b)
      local learned_a = fuzzy_learning.score(typed, a.text)
      local learned_b = fuzzy_learning.score(typed, b.text)
      if learned_a ~= learned_b then return learned_a > learned_b end
      local function distance(c)
        local reading = (c.comment or ""):match("%[JF_READING%]([^%[]*)") or ""
        return math.max(0, utf8.len(reading) - utf8.len(typed))
      end
      local distance_a, distance_b = distance(a), distance(b)
      if distance_a ~= distance_b then return distance_a < distance_b end
      return false
    end)
    -- Keep the standard kana form beside the first kanji form with the same
    -- reading.  Otherwise rare spellings can fill the whole visible row
    -- before a common form such as さすが appears.
    -- Once a sufficiently long input already has an exact Chinese word or
    -- phrase, broad Japanese fuzzy output is almost always segmentation
    -- noise rather than a useful alternative.  Exact Japanese dictionary
    -- words remain in `exact`; only fuzzy corrections are suppressed.
    if not (has_full_exact_chinese and #typed >= 8 and
            not is_valid_japanese_romaji(typed)) then
      local emitted_other = {}
      local reading_seen = {}
      for _, cand in ipairs(fuzzy_kanji) do
        yield(tag_candidate_language(cand))
        local reading = (cand.comment or ""):match("%[JF_READING%]([^%[]*)") or ""
        if reading ~= "" and not reading_seen[reading] then
          reading_seen[reading] = true
          for index, other in ipairs(fuzzy_other) do
            local other_reading = (other.comment or ""):match("%[JF_READING%]([^%[]*)") or ""
            if other_reading == reading then
              yield(tag_candidate_language(other))
              emitted_other[index] = true
            end
          end
        end
      end
      for index, cand in ipairs(fuzzy_other) do
        if not emitted_other[index] then yield(tag_candidate_language(cand)) end
      end
    end
    -- For a completely valid Japanese spelling, useful shorter Japanese
    -- words (warui under waruidesu) must stay beside the whole expression,
    -- rather than being buried behind Chinese syllable fallbacks.
    local valid_japanese_input = is_valid_japanese_romaji(typed)
    if valid_japanese_input then
      for _, cand in ipairs(assembled_japanese) do
        yield(tag_candidate_language(cand))
      end
      assembled_japanese = {}
    end
    for _, cand in ipairs(assembled) do yield(tag_candidate_language(cand)) end
    if not (has_full_exact_chinese and #typed >= 8 and
            not is_valid_japanese_romaji(typed)) then
      for _, cand in ipairs(assembled_japanese) do yield(tag_candidate_language(cand)) end
      for _, cand in ipairs(japanese_sentence) do yield(tag_candidate_language(cand)) end
      for _, cand in ipairs(completion_japanese) do yield(tag_candidate_language(cand)) end
    end
    for _, cand in ipairs(completion) do yield(tag_candidate_language(cand)) end
    exact, fuzzy_kanji, fuzzy_other, fuzzy_lexical = {}, {}, {}, {}
    assembled, assembled_japanese, japanese_sentence = {}, {}, {}
    completion, completion_japanese = {}, {}
  end

  for cand in input:iter() do
    local comment = cand.comment or ""
    local learned_emoji = cand.type == "emoji_learning" or
                          cand.type == "chinese_abbreviation_learning"
    local is_prefix_completion = comment:sub(1, #PREFIX_MARKER) == PREFIX_MARKER
    local tagged_comment = comment
    local matched_rule = nil
    local is_custom = false
    local spelling = nil
    if learned_emoji then
      exact[#exact + 1] = cand
      goto continue
    elseif is_prefix_completion then
      local visible_comment = comment:sub(#PREFIX_MARKER + 1)
      local output_type = cand.type == "mozc_v2_polite_completion" and
                          cand.type or "completion"
      yield(Candidate(output_type, cand.start, cand._end, cand.text,
        visible_comment .. LANGUAGE_JA))
      goto continue
    end
    -- A candidate may pass through this filter again after a UI/context
    -- refresh.  Remove transport tags from the previous pass before parsing
    -- the fuzzy reading and appending exactly one fresh language tag.
    comment = strip_language_metadata(comment)
    for _, rule in ipairs(RULES) do
      if comment:sub(1, #rule.marker) == rule.marker then
        matched_rule = rule
        break
      end
    end
    if comment:sub(1, #CUSTOM_MARKER) == CUSTOM_MARKER then
      is_custom = true
      local payload = comment:sub(#CUSTOM_MARKER + 1)
      spelling = payload:match("^(.-)%[%[JF_TYPED:.-%]%]$") or payload
      spelling = strip_language_metadata(spelling)
    elseif comment:sub(1, #COMBINED_MARKER) == COMBINED_MARKER then
      local payload = comment:sub(#COMBINED_MARKER + 1)
      local segment_typed
      spelling, segment_typed = payload:match("^(.-)%[%[JF_TYPED:(.-)%]%]$")
      if not spelling then
        spelling, segment_typed = payload:match("^(.-)" .. string.char(30) .. "(.*)$")
      end
      if not spelling then spelling = payload end
      spelling = strip_language_metadata(spelling)
      segment_typed = strip_language_metadata(segment_typed)
      -- The record-separator payload can be stripped by an intermediate
      -- candidate rebuild, leaving an empty string.  Empty strings are truthy
      -- in Lua, so `segment_typed or typed` would not fall back and every
      -- fuzzy rule would compare against "".  Treat empty as missing.
      if not segment_typed or segment_typed == "" then
        segment_typed = (cand.preedit or ""):lower():gsub("[%s']+", "")
      end
      if segment_typed == "" then segment_typed = typed end
      if master_enabled then
        matched_rule = combined_rule_for(spelling, segment_typed, context)
      end
    end
    if is_custom or (matched_rule and master_enabled and context:get_option(matched_rule.option)) then
      spelling = spelling or comment:sub(#matched_rule.marker + 1)
      -- A correct polite ending is grammar, not a fuzzy target.  Keep broad
      -- prism matches that repair the stem, but discard any match that turns
      -- desuka into tesuka/dezuka/desuga (and the corresponding desu/masu
      -- endings).  Custom user rules remain literal and are not constrained.
      local suffix_ok = is_custom or not protected_suffix or
                        typed == protected_suffix or
                        (spelling:sub(-#protected_suffix) == protected_suffix and
                         protected_kana_suffix and
                         (cand.text or ""):sub(-#protected_kana_suffix) == protected_kana_suffix)
      if suffix_ok then
        local kana = romaji_to_hiragana(spelling)
        local ui_comment = kana ~= "" and ("[JF_READING]" .. kana) or ""
        local typed_kana = romaji_to_hiragana(typed)
        if ui_comment ~= "" and typed_kana ~= "" then
          ui_comment = ui_comment .. "[[RIME_FUZZY_TYPED:" .. typed_kana .. "]]"
        end
        -- A direct whole-word correction for an omitted long-u or ke->kai is
        -- more useful than mechanical exact segmentation (kensetsugyo ->
        -- 建設魚 versus 建設業; kegi -> 毛木 versus 会議).  Give only these
        -- two unambiguous correction families a transport type that the final
        -- mixed sorter can place ahead of Japanese mechanical assemblies.
        local output_type = cand.type
        local gyo_long_u = matched_rule and
          matched_rule.option == "japanese_fuzzy_long_u" and
          spelling == typed:gsub("gyo", "gyou")
        if matched_rule and
           (gyo_long_u or matched_rule.option == "japanese_fuzzy_ke_kai") then
          output_type = "mozc_v2_preferred_correction"
        end
        local converted = ShadowCandidate(cand, output_type, cand.text, ui_comment)
        if buffered < BUFFER_LIMIT then
          local vowel_lexeme = comment:sub(1, #COMBINED_MARKER) == COMBINED_MARKER and
            cand.type ~= "sentence" and matched_rule and
            (matched_rule.marker == "[JF:DOUBLE_VOWEL]" or
             matched_rule.marker == "[JF:LONG_I]" or
             matched_rule.marker == "[JF:LONG_U]")
          local bucket = vowel_lexeme and fuzzy_lexical or
                         (has_kanji(cand.text) and fuzzy_kanji or fuzzy_other)
          bucket[#bucket + 1] = converted
        else
          yield(tag_candidate_language(converted))
        end
      end
    elseif not matched_rule and not is_custom and comment:sub(1, #COMBINED_MARKER) ~= COMBINED_MARKER
           and not is_hidden_chinese_completion(cand) then
      local quality = tonumber(cand.quality) or 0
      local candidate_has_kana = has_script(cand.text, 0x3040, 0x30ff)
      local candidate_has_han = has_script(cand.text, 0x3400, 0x9fff) or
                                has_script(cand.text, 0xf900, 0xfaff)
      -- The exact Mozc dictionary can score above the Chinese quality range
      -- (for example 枝管/edakan).  Honor its explicit source before using the
      -- quality heuristic, especially after selecting a Japanese prefix.
      local candidate_is_japanese =
        (cand.type or ""):match("^mozc_v2") or
        tagged_comment:find(LANGUAGE_JA, 1, true) ~= nil or
        candidate_has_kana or is_prefix_completion or
        comment:find("[JF", 1, true) == 1 or
        (quality >= 50 and quality < 299)
      local is_chinese = not candidate_is_japanese and
                         (tagged_comment:find(LANGUAGE_ZH, 1, true) ~= nil or
                          quality >= 299 or
                          (quality >= 1.19 and quality < 2) or
                          candidate_has_han)
      if continuation_ja and is_chinese then
        -- The preceding selected segment is Japanese.  Keep the remainder in
        -- the same language instead of restarting Chinese mixed input.
        goto continue
      end
      -- When the active input is a complete polite expression, a Japanese
      -- candidate must also cover that complete grammar.  Prefix candidates
      -- such as 悪い and malformed assemblies such as 悪出すか must not
      -- occupy the visible row for waruidesuka.
      if protected_kana_suffix and typed ~= protected_suffix and
         candidate_is_japanese and
         (cand.text or ""):sub(-#protected_kana_suffix) ~= protected_kana_suffix then
        goto continue
      end
      if buffered < BUFFER_LIMIT then
        local preedit = (cand.preedit or ""):lower():gsub("[%s']+", "")
        -- A dictionary spelling containing "-" is reported by librime as a
        -- completion when the user typed its explicit q alias.  Exact
        -- equivalence must win before the generic completion bucket, or the
        -- correct コーヒー is buried behind a page of Chinese candidates.
        if canonical_exact_spelling(preedit) == canonical_exact_spelling(typed) and
               (cand.type ~= "sentence" or is_chinese or
                (candidate_is_japanese and
                 (protected_suffix or is_valid_japanese_romaji(typed)))) and
               not ((cand.text or ""):find("[A-Za-z]") and
                    (candidate_has_kana or candidate_has_han)) and
               not (cand.text or ""):find("[A-Za-z]") then
          exact[#exact + 1] = cand
          if is_chinese then has_full_exact_chinese = true end
        elseif cand.type == "completion" then
          local bucket = candidate_is_japanese and completion_japanese or completion
          bucket[#bucket + 1] = cand
        -- Only a candidate whose complete spelling equals the active input
        -- is exact.  Translator quality alone is insufficient: prefix
        -- candidates such as sasu under sasuka also carry Japanese quality
        -- 200 and must stay behind the whole-word fuzzy match さすが.
        elseif cand.type == "sentence" and candidate_is_japanese then
          japanese_sentence[#japanese_sentence + 1] = cand
        else
          local bucket = candidate_is_japanese and assembled_japanese or assembled
          bucket[#bucket + 1] = cand
        end
      else
        yield(tag_candidate_language(cand))
      end
    end
    ::continue::
    buffered = buffered + 1
    if buffered == BUFFER_LIMIT then
      flush()
      if lexical_selected then break end
    end
  end
  if buffered < BUFFER_LIMIT then flush() end
end

return japanese_fuzzy_filter
