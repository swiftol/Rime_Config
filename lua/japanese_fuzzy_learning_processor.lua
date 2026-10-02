local learning = require("japanese_fuzzy_learning")
local composition = require("japanese_composition_learning")
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

local function katakana_count(text)
  local count = 0
  for _, cp in utf8.codes(text or "") do
    if (cp >= 0x30a1 and cp <= 0x30fa) or cp == 0x30fc then
      count = count + 1
    else
      return nil
    end
  end
  return count
end

local function mixed_japanese_count(text)
  local count, has_han, has_kana = 0, false, false
  for _, cp in utf8.codes(text or "") do
    if (cp >= 0x3400 and cp <= 0x4dbf) or
       (cp >= 0x4e00 and cp <= 0x9fff) or
       (cp >= 0xf900 and cp <= 0xfaff) then
      has_han = true
    elseif (cp >= 0x3041 and cp <= 0x309f) or
           (cp >= 0x30a1 and cp <= 0x30fa) or cp == 0x30fc then
      has_kana = true
    else
      return nil
    end
    count = count + 1
  end
  return has_han and has_kana and count or nil
end

function M.init(env)
  learning.load()
  composition.load()
  -- The editor's select callback commits and clears the composition before
  -- later select_notifier listeners run. commit_notifier runs before Clear(),
  -- so the selected candidate and the original input are still available.
  env.notifier = env.engine.context.commit_notifier:connect(function(context)
    if context:get_option("japanese_input_table_enabled") then return end
    local candidate = context:get_selected_candidate()
    if not candidate then env.recent_han = {}; return end
    local comment = candidate.comment or ""
    local input = (context.input or ""):lower():gsub("[%s']+", "")
    if comment:sub(1, 12) == "[JF_READING]" then
      learning.increment(input, candidate.text)
    end
    if not comment:find("[[RIME_LANG:JA]]", 1, true) or
       not routing.is_valid_japanese_input(input) then
      env.recent_han = {}
      return
    end
    local committed = context:get_commit_text() or candidate.text
    local count = han_count(committed)
    local kana_count = katakana_count(committed)
    local mixed_count = mixed_japanese_count(committed)
    if mixed_count and mixed_count >= 2 and mixed_count <= 20 then
      composition.increment(input, committed)
      env.recent_han = {}
      return
    end
    if kana_count and kana_count >= 2 and kana_count <= 20 then
      composition.increment(input, committed)
      env.recent_han = {}
      return
    end
    if not count then env.recent_han = {}; return end
    if count >= 2 and count <= 6 then
      composition.increment(input, committed)
      env.recent_han = {}
      return
    end
    if count ~= 1 or #input > 8 then env.recent_han = {}; return end
    local recent = env.recent_han or {}
    local now = os.time()
    if #recent > 0 and now - recent[#recent].time > 30 then recent = {} end
    -- Repeating the same spelling starts a new observation of that name,
    -- rather than teaching a cross-boundary fragment such as 才李.
    if #recent >= 2 and recent[1].code == input and
       recent[1].text == committed then recent = {} end
    recent[#recent + 1] = { code = input, text = committed, time = now }
    if #recent > 4 then table.remove(recent, 1) end
    for length = 2, #recent do
      local codes, words = {}, {}
      for index = #recent - length + 1, #recent do
        codes[#codes + 1] = recent[index].code
        words[#words + 1] = recent[index].text
      end
      composition.observe(table.concat(codes), table.concat(words))
    end
    env.recent_han = recent
  end)
end

function M.func(_, _)
  return 2 -- kNoop
end

function M.fini(env)
  if env.notifier then env.notifier:disconnect() end
end

return M
