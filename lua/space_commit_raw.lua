-- space_commit_raw.lua
-- 空格键行为由 space_select_first 开关控制：
--   false：上屏原始输入 + 自动添加空格
--   true：交还 Rime 默认处理，选择第一个候选

local function processor(key, env)
  local engine = env.engine
  local context = engine.context
  local composition = context.composition
  
  -- 只处理空格键
  if key:repr() ~= "space" then
    return 2  -- kNoop，不处理其他按键
  end

  -- 开启“空格选择首选”后不截获空格，让 selector / editor 正常处理。
  if env.engine.schema.config:get_bool("space_commit_raw/select_first") then
    return 2  -- kNoop
  end
  
  -- 如果正在输入
  if context:is_composing() then
    -- 获取原始输入
    local input = context.input
    
    -- 上屏原始输入
    engine:commit_text(input)
    
    -- 发送空格
    engine:commit_text(" ")
    
    -- 清空输入
    context:clear()
    
    return 1  -- kAccepted，已处理
  end
  
  return 2  -- kNoop，其他情况不处理
end

return processor
