-- Bounded, offline evidence fusion. Scores are ranking evidence, NOT trained
-- probabilities. Raw Rime/Mozc quality values have incompatible scales.
local M = {}
local endings = {}
-- Productive inflections need not be whole dictionary entries. In particular,
-- ni+ge+na+sai is also legal pinyin: omitting this imperative ending would
-- classify nigenasai as Chinese and suppress its decoded 逃げなさい path.
for word in ([[だけでなく だけではなく だけでなくて ばかりでなく
  だけです だけだ だけだった だけではない なくても なくては
  なければならない なければいけない なければ ないといけない
  なくてはいけない なくてはならない てもいい てもよい てはいけない
  てしまう てしまった ている ていた ています ていました
  ておく ておいた てください ないでください てほしい
  なさい なさいよ なさいね なさいませ
  られる させられる させる される された
  かもしれない かもしれません にちがいない わけではない
  ことができる ことがある ようになる ようにする
  について にかんする によって にたいして として
  でない ではない です ます でした ません ませんでした
  でしょう だろう たいです たかった たくない たくなかった
  ですか ますか でしたか ませんか
]]):gmatch('%S+') do endings[#endings + 1] = word end

local function ends_with(text, suffix)
  return #text >= #suffix and text:sub(-#suffix) == suffix
end

function M.classify(code, reading, candidates, routing, recent, dictionary_zh)
  local has_ja = reading ~= ''
  local pinyin = routing.is_complete_pinyin(code)
  local zh_score, ja_score = pinyin and 5 or 0, has_ja and 1 or 0
  -- AZIK can mechanically interpret Mandarin initial-only abbreviations.
  -- Their consonant runs are Chinese evidence, not invalid Chinese input.
  local initials = #code >= 3 and code:match('^[bcdfghjklmnpqrstvwxyz]+$') ~= nil
  if initials then zh_score = zh_score + 5 end
  -- Mixed Chinese spelling can be initial abbreviations followed by a full
  -- syllable (e.g. xzz + yang). It is not invalid Chinese merely because the
  -- complete string fails the full-pinyin parser. Three leading initials
  -- avoid confusing ordinary Japanese sh/ch/ky consonant clusters.
  if not pinyin and not initials then
    for split = 3, math.min(8, #code - 2) do
      if code:sub(1, split):match('^[bcdfghjklmnpqrstvwxyz]+$') and
         routing.is_complete_pinyin(code:sub(split + 1)) then
        zh_score = zh_score + 5
        break
      end
    end
  end
  local grammar, lexical_ja, lexical_zh, sentence_ja = false, false, dictionary_zh == true, false
  if has_ja then
    for _, ending in ipairs(endings) do
      if ends_with(reading, ending) then grammar = true; break end
    end
    -- Existing spelling evidence is only supplementary; no network lookup.
    if routing.should_prefer_japanese(code) then ja_score = ja_score + 3 end
    if grammar then ja_score = ja_score + 8 end
  end
  for _, cand in ipairs(candidates) do
    -- Prefix completions cannot decide the language of the entire input.
    if cand.start == 0 and cand._end == #code then
      local comment, kind = cand.comment or '', cand.type or ''
      if has_ja and comment:find('[[RIME_LANG:JA]]', 1, true) then
        if comment:find('[[RIME_JA_SENTENCE]]', 1, true) then sentence_ja = true end
        if comment:find('[[RIME_JA_GRAMMAR]]', 1, true) then grammar = true end
        -- Mechanical kana spelling and fuzzy/partial fallback are not lexical
        -- proof. A meaningful full conversion is evidence, never certainty.
        local has_han = false
        for _, cp in utf8.codes(cand.text or '') do
          if cp >= 0x3400 and cp <= 0x9fff then has_han = true; break end
        end
        if has_han and not comment:find('[[RIME_JA_ASSEMBLED]]', 1, true) and
           not kind:find('fuzzy', 1, true) and
           (kind == 'japanese_input_table' or kind == 'mozc_v2' or
            kind == 'mozc_v2_lexical_sentence' or kind == 'japanese') then
          lexical_ja = true
        end
      elseif not comment:find('[[RIME_LANG:JA]]', 1, true) and
             not comment:find('[[RIME_LANG:CP]]', 1, true) then
        -- Dictionary/user phrases are stronger than arbitrary sentence
        -- assembly, cloud guesses, emoji or ASCII completions.
        local spelling = (cand.preedit or ''):lower():gsub("[%s']+", '')
        local whole = spelling == code:lower():gsub("[%s']+", '')
        if whole and (kind == 'table' or kind == 'user_table' or kind=='phrase' or kind=='user_phrase' or
           kind == 'chinese_abbreviation_learning') then lexical_zh = true end
      end
    end
  end
  if grammar and ja_score < 9 then ja_score=ja_score+8 end
  if lexical_ja then ja_score = ja_score + 8 end
  if sentence_ja then ja_score = ja_score + 8 end
  if lexical_zh then zh_score = zh_score + 5 end
  local margin = ja_score - zh_score
  local priority = 'zh'
  if has_ja and margin >= 2 then priority = 'ja'
  elseif math.abs(margin) < 2 and recent == 'ja' and has_ja then priority = 'ja' end
  -- Short homophones must remain bilingual; only a larger margin or clear
  -- grammatical form may suppress the other route. Recency never suppresses.
  local decision = 'both'
  if not has_ja then decision = 'zh'
  elseif margin >= 4 and (#code >= 6 or grammar) then decision = 'ja'
  elseif margin <= -4 and #code >= 6 then decision = 'zh' end
  -- A genuine whole Chinese dictionary word remains distinct from a cloud
  -- phonetic guess. Keep bilingual access for a real lexical collision;
  -- productive Japanese grammar and recent committed Japanese can resolve it.
  if lexical_zh and (lexical_ja or sentence_ja) and not grammar and recent~='ja' then
    priority='zh';decision='both'
  end
  return {priority=priority, decision=decision, zh=zh_score, ja=ja_score,
          grammar=grammar, lexical_ja=lexical_ja, lexical_zh=lexical_zh}
end
return M
