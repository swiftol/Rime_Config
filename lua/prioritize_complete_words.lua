-- prioritize_complete_words.lua
-- 简拼时优先显示完整词组，特别优化重复简拼（如 ygyg）

local function filter(input, env)
  local context = env.engine.context
  local input_code = context.input or ""
  local input_len = #input_code
  
  -- 检测是否是 abab 重复模式（如 ygyg, nznz）
  local is_repeat_pattern = false
  if input_len == 4 then
    local a, b = input_code:sub(1, 1), input_code:sub(2, 2)
    local c, d = input_code:sub(3, 3), input_code:sub(4, 4)
    if a == c and b == d then
      is_repeat_pattern = true
    end
  end
  
  for cand in input:iter() do
    local text = cand.text
    local text_len = utf8.len(text)
    
    if text_len then
      -- 特殊处理：abab 模式（如 ygyg）优先匹配 4 字词
      if is_repeat_pattern and text_len == 4 then
        cand.quality = cand.quality * 10000  -- 大幅提升 4 字词
      
      -- 短输入（<=3 字母）优先显示 3 字词
      elseif input_len <= 3 and text_len == 3 then
        cand.quality = cand.quality * 5000
      
      -- 4-5 字母输入优先显示 4-5 字词
      elseif input_len >= 4 and input_len <= 5 and text_len >= 4 then
        cand.quality = cand.quality * 1000
      
      -- 一般规则：词组长度 >= 输入长度，稍微提升
      elseif text_len >= input_len then
        cand.quality = cand.quality * 10
      end
    end
    
    yield(cand)
  end
end

return filter
