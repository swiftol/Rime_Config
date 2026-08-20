local START_MARKER = "# --- RimeSettings common phrases begin ---"
local END_MARKER = "# --- RimeSettings common phrases end ---"

do
  local file = io.open(rime_api.get_user_data_dir() .. "/common_phrase_load.log", "a")
  if file then
    file:write(os.date("%Y-%m-%d %H:%M:%S"), " filter loaded\n")
    file:close()
  end
end

local function phrases_for(code)
  local ordered, lookup = {}, {}
  local file = io.open(rime_api.get_user_data_dir() .. "/custom_phrase.txt", "r")
  if not file then return ordered, lookup end

  local inside = false
  for line in file:lines() do
    line = line:gsub("\r$", "")
    if line == START_MARKER then
      inside = true
    elseif line == END_MARKER then
      inside = false
    elseif inside then
      local text, entry_code = line:match("^([^\t]+)\t([^\t]+)")
      if text and entry_code and entry_code:lower():gsub("%s+", "") == code and not lookup[text] then
        lookup[text] = true
        table.insert(ordered, text)
      end
    end
  end
  file:close()
  return ordered, lookup
end

local function debug_log(code, expected_count, input_count, matched_count)
  local file = io.open(rime_api.get_user_data_dir() .. "/common_phrase_debug.log", "a")
  if not file then return end
  file:write(os.date("%Y-%m-%d %H:%M:%S"), " code=", code,
    " expected=", expected_count, " input=", input_count,
    " matched=", matched_count, "\n")
  file:close()
end

local function common_phrase_filter(input, env)
  local code = env.engine.context.input:lower():gsub("%s+", "")
  local phrases, expected = phrases_for(code)

  -- Log at entry so configuration/loading failures can be distinguished from
  -- a phrase-file lookup miss.
  debug_log(code, #phrases, -1, -1)

  if #phrases == 0 then
    for cand in input:iter() do yield(cand) end
    return
  end

  local regular, matched = {}, {}
  local input_count, matched_count = 0, 0

  -- Use the iterator only through Lua's generic-for protocol.  Some bundled
  -- librime-lua versions do not allow calling the iterator object directly.
  for cand in input:iter() do
    input_count = input_count + 1
    if expected[cand.text] then
      if not matched[cand.text] then
        matched[cand.text] = cand
        matched_count = matched_count + 1
      end
    else
      table.insert(regular, cand)
    end
  end

  local anchor = regular[1]
  if not anchor then
    for _, text in ipairs(phrases) do
      if matched[text] then anchor = matched[text]; break end
    end
  end

  if #regular > 0 then yield(regular[1]) end
  if anchor then
    for _, text in ipairs(phrases) do
      local cand = matched[text]
      if not cand then
        cand = Candidate("common_phrase", anchor.start, anchor._end, text, "")
        cand.quality = 1000000000
      end
      yield(ShadowCandidate(cand, "common_phrase", text, ""))
    end
  end
  for i = 2, #regular do yield(regular[i]) end

  debug_log(code, #phrases, input_count, matched_count)
end

return common_phrase_filter
