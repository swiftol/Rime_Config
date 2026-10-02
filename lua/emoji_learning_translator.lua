local learning = require("emoji_learning")
local MARKER = "[[RIME_LANG:ZH]]"

local function translator(input, seg, _)
  local normalized = (input or ""):lower():gsub("[%s']+", "")
  for index, item in ipairs(learning.ranked(normalized)) do
    local candidate = Candidate("emoji_learning", seg.start, seg._end,
                                item.text, MARKER)
    candidate.quality = 1000000000 + item.score - index / 1000
    yield(candidate)
  end
end

return translator
