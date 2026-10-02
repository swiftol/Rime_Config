local learning = require("emoji_learning")
local routing = require("mozc_v2_translator")
local M = {}

local function contains_emoji(text)
  for _, cp in utf8.codes(text or "") do
    if (cp >= 0x1f000 and cp <= 0x1faff) or
       (cp >= 0x2600 and cp <= 0x27bf) then
      return true
    end
  end
  return false
end

function M.init(env)
  learning.load()
  env.notifier = env.engine.context.commit_notifier:connect(function(context)
    local candidate = context:get_selected_candidate()
    if not candidate or not contains_emoji(candidate.text) then return end
    local input = (context.input or ""):lower():gsub("[%s']+", "")
    if input ~= "" then
      learning.increment(input, candidate.text)
      -- Emoji have no Han-character count, so the ordinary Chinese phrase
      -- learner cannot derive initials from them.  Reuse the full-pinyin
      -- parser to teach both forms after one explicit choice:
      -- haochi -> hc -> 😋, shengqi -> sq -> 😡.
      local initials = routing.pinyin_initials_auto(input)
      if initials and initials ~= input then
        learning.increment(initials, candidate.text)
      end
    end
  end)
end

function M.func(_, _)
  return 2 -- kNoop
end

function M.fini(env)
  if env.notifier then env.notifier:disconnect() end
end

return M
