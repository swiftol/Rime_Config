local learning = require("chinese_abbreviation_learning")
local cloud_learning = require("google_cloud_learning")
local routing = require("mozc_v2_translator")
local M = {}

local function han_count(text)
  local count = 0
  for _, cp in utf8.codes(text or "") do
    if (cp >= 0x3400 and cp <= 0x4dbf) or
       (cp >= 0x4e00 and cp <= 0x9fff) or
       (cp >= 0xf900 and cp <= 0xfaff) then
      count = count + 1
    else
      return nil
    end
  end
  return count
end

function M.init(env)
  learning.load()
  env.notifier = env.engine.context.commit_notifier:connect(function(context)
    local candidate = context:get_selected_candidate()
    if not candidate then env.last_single = nil; return end
    local comment = candidate.comment or ""
    local language = comment:find("[[RIME_LANG:JA]]", 1, true) and "ja" or
                     comment:find("[[RIME_LANG:ZH]]", 1, true) and "zh" or nil
    if language then
      context:set_property("cloud_recent_language", language)
      context:set_property("cloud_recent_language_time", tostring(os.time()))
    end
    if not comment:find("[[RIME_LANG:ZH]]", 1, true) or
       comment:find("[[RIME_LANG:JA]]", 1, true) then
      env.last_single = nil
      return
    end
    -- The selected candidate may be only the *last character* of a Chinese
    -- composition.  Learn what was actually committed, not that final fragment
    -- under the full code (e.g. yunzi must not teach only 子).
    local committed = context:get_commit_text() or candidate.text
    local count = han_count(committed)
    if not count or count < 1 or count > 12 then
      env.last_single = nil
      return
    end
    local input = (context.input or ""):lower():gsub("[%s']+", "")
    if count > 1 then
      local cloud_text, reading = cloud_learning.result(context.input or "", "zh", committed)
      if cloud_text == committed and
         cloud_learning.learn(input, committed, reading) then
        -- Also upgrades an older locally learned cloud choice when it is
        -- selected again.  Never mutate personal learning from a filter's
        -- lazy candidate iterator.
        env.last_single = nil
        return
      end
    end
    if count > 1 then
      env.last_single = nil
      if input:match("^[a-z]+$") then
        learning.increment(input, committed)
        local initials = routing.pinyin_initials(input, count)
        if initials and initials ~= input then
          learning.increment(initials, committed, true)
        end
      end
      return
    end
    if count == 1 then
      -- The mixed scheme's final sorter cannot rely on librime user-dictionary
      -- order alone because Chinese and Japanese arrive in separate streams.
      -- Learn an explicitly selected single Han character under its complete
      -- pinyin too (`shi` -> the user's preferred 是/事/时), then let the
      -- existing learned-candidate translator rank it on the next input.
      if input:match("^[a-z]+$") and
         (routing.is_single_pinyin_syllable(input) or #input == 1) then
        local previous = env.last_single
        local paired = false
        if previous and os.time() - previous.time <= 30 and
           routing.is_single_pinyin_syllable(input) then
          local code = previous.code .. input
          local initials = routing.pinyin_initials(code, 2)
          if initials then
            learning.observe_pair(code, previous.text .. committed, initials)
            paired = true
          end
        end
        -- Pair observations must not overlap: A B A B is two observations of
        -- AB, not an accidental third phrase BA between them.
        env.last_single = not paired and routing.is_single_pinyin_syllable(input) and
            { code = input, text = committed, time = os.time() } or nil
        learning.increment(input, committed)
        -- A one-Han complete syllable and its one-letter abbreviation refer
        -- to the same Chinese reading (`ba` and `b`).  Teach the initial too
        -- so choosing 把/吧 from either spelling cannot create two conflicting
        -- preference orders.  Multi-syllable words are deliberately excluded.
        if #input > 1 and routing.is_single_pinyin_syllable(input) then
          learning.increment(input:sub(1, 1), committed, true)
        end
      else
        env.last_single = nil
      end
      return
    end
  end)
end

function M.func(_, _) return 2 end

function M.fini(env)
  if env.notifier then env.notifier:disconnect() end
end

return M
