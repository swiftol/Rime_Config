-- Bounded, offline evidence fusion. Scores are ranking evidence, NOT trained
-- probabilities. Raw Rime/Mozc quality values have incompatible scales.
local M = {}
local endings = {}
for word in ([[だけでなく だけではなく だけでなくて ばかりでなく
  だけです だけだ だけだった だけではない なくても なくては
  なければならない なければいけない なければ ないといけない
  なくてはいけない なくてはならない てもいい てもよい てはいけない
  てしまう てしまった ている ていた ています ていました
  ておく ておいた てください ないでください てほしい
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

function M.classify(code, reading, candidates, routing, recent)
  local has_ja = reading ~= ''
  local pinyin = routing.is_complete_pinyin(code)
  local zh_score, ja_score = pinyin and 5 or 0, has_ja and 1 or 0
  -- AZIK can mechanically interpret Mandarin initial-only abbreviations.
  -- Their consonant runs are Chinese evidence, not invalid Chinese input.
  local initials = #code >= 3 and code:match('^[bcdfghjklmnpqrstvwxyz]+$') ~= nil
  if initials then zh_score = zh_score + 5 end
  local grammar, lexical_ja, lexical_zh = false, false, false
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
        -- Mechanical kana spelling and fuzzy/partial fallback are not lexical
        -- proof. A meaningful full conversion is evidence, never certainty.
        local has_han = false
        for _, cp in utf8.codes(cand.text or '') do
          if cp >= 0x3400 and cp <= 0x9fff then has_han = true; break end
        end
        if has_han and not kind:find('fuzzy', 1, true) and
           (kind == 'japanese_input_table' or kind == 'mozc_v2' or
            kind == 'mozc_v2_lexical_sentence' or kind == 'japanese') then
          lexical_ja = true
        end
      elseif not comment:find('[[RIME_LANG:JA]]', 1, true) and
             not comment:find('[[RIME_LANG:CP]]', 1, true) then
        -- Dictionary/user phrases are stronger than arbitrary sentence
        -- assembly, cloud guesses, emoji or ASCII completions.
        if kind == 'table' or kind == 'user_table' or
           kind == 'chinese_abbreviation_learning' then lexical_zh = true end
      end
    end
  end
  if lexical_ja then ja_score = ja_score + 4 end
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
  return {priority=priority, decision=decision, zh=zh_score, ja=ja_score,
          grammar=grammar, lexical_ja=lexical_ja, lexical_zh=lexical_zh}
end
return M
