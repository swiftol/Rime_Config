-- Bounded Japanese romanization prefix completion.
-- Rime's normal script completion only works at valid syllable boundaries;
-- an unfinished code such as "sud" therefore produced no Japanese results.
local M = {}

function M.init(env)
  env.memory = Memory(env.engine, Schema("japanese"))
end

function M.func(input, segment, env)
  local compact = (input or ""):lower():gsub("[%s']+", "")
  if #compact < 3 then return end

  local seen, emitted = {}, 0
  if env.memory:dict_lookup(compact, true, 16) then
    for entry in env.memory:iter_dict() do
      local decoded = env.memory:decode(entry.code)
      local spelling = decoded and table.concat(decoded, "") or ""
      if spelling ~= compact and spelling:sub(1, #compact) == compact and
         not seen[entry.text] then
        seen[entry.text] = true
        local phrase = Phrase(env.memory, "completion", segment.start, segment._end, entry)
        phrase.comment = ""
        yield(phrase:toCandidate())
        emitted = emitted + 1
        if emitted >= 8 then return end
      end
    end
  end
end

return M
