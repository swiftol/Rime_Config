local learning = require("japanese_fuzzy_learning")
local M = {}

function M.init(env)
  learning.load()
  -- The editor's select callback commits and clears the composition before
  -- later select_notifier listeners run. commit_notifier runs before Clear(),
  -- so the selected candidate and the original input are still available.
  env.notifier = env.engine.context.commit_notifier:connect(function(context)
    local candidate = context:get_selected_candidate()
    if not candidate then return end
    local comment = candidate.comment or ""
    local input = (context.input or ""):lower():gsub("[%s']+", "")
    if comment:sub(1, 12) ~= "[JF_READING]" then return end
    learning.increment(input, candidate.text)
  end)
end

function M.func(_, _)
  return 2 -- kNoop
end

function M.fini(env)
  if env.notifier then env.notifier:disconnect() end
end

return M
