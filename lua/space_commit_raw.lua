-- space_commit_raw.lua
-- 空格键三种互斥行为：
--   reading_preview：只拦截空格，候选窗由 TSF 在按住期间显示日语读音
--   select_first：交还 Rime 默认处理，选择第一个候选
--   默认：上屏原始输入 + 自动添加空格

local function processor(key, env)
  local engine = env.engine
  local context = engine.context
  local composition = context.composition

  -- 只处理空格键
  if key:repr() ~= "space" then
    return 2  -- kNoop，不处理其他按键
  end

  local hot_mode = context:get_property("space_hot_mode")
  -- The settings panel publishes this property to existing sessions.  Only
  -- fall back to the compiled schema before the user has saved a hot mode.
  local reading_preview = hot_mode == "preview" or
      ((not hot_mode or hot_mode == "") and env.engine.schema.config:get_bool("space_commit_raw/reading_preview"))
  local select_first = hot_mode == "first" or
      ((not hot_mode or hot_mode == "") and env.engine.schema.config:get_bool("space_commit_raw/select_first"))
  -- 读音预览模式下，按下和松开空格都不能改变正在输入的内容。
  if reading_preview then
    return 1  -- kAccepted
  end

  -- 开启“空格选择首选”后不截获空格，让 selector / editor 正常处理。
  if select_first then
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
