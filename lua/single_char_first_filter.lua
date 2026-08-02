-- single_char_first_filter.lua
-- 常用单字优先排序过滤器

local function single_char_first_filter(input, env)
  local high_freq_chars = {
    -- 常用单字列表（按使用频率排序）
    d = {"的", "得", "地", "都", "对", "大", "到", "道", "等", "但"},
    l = {"了", "来", "里", "离", "理", "力", "利", "立", "例", "列"},
    s = {"是", "说", "所", "上", "时", "什", "三", "谁", "十", "生"},
    z = {"在", "这", "中", "着", "之", "只", "自", "最", "则", "者"},
    w = {"我", "为", "问", "文", "五", "万", "外", "位", "完", "无"},
    y = {"一", "有", "要", "也", "用", "与", "以", "因", "于", "由"},
    b = {"不", "把", "被", "比", "本", "并", "表", "别", "部", "八"},
    g = {"个", "过", "给", "更", "国", "各", "高", "公", "关", "果"},
    t = {"他", "她", "它", "太", "同", "通", "听", "条", "天", "特"},
    n = {"你", "那", "能", "内", "年", "南", "难", "女", "农", "鸟"},
    h = {"和", "很", "还", "会", "或", "后", "好", "何", "话", "号"},
    j = {"就", "及", "见", "将", "进", "经", "己", "几", "家", "件"},
    k = {"可", "看", "开", "口", "快", "空", "科", "考", "况", "刻"},
    m = {"们", "么", "没", "每", "民", "名", "明", "面", "目", "马"},
    x = {"下", "些", "想", "小", "先", "新", "心", "形", "行", "相"},
    c = {"从", "出", "成", "次", "长", "才", "场", "产", "常", "此"},
    q = {"去", "前", "全", "起", "其", "期", "且", "情", "清", "区"},
    r = {"人", "如", "然", "让", "日", "任", "认", "容", "入", "若"},
    a = {"啊", "阿", "爱", "按", "案", "安", "岸", "暗", "鞍", "癌"},
    f = {"发", "法", "方", "分", "放", "反", "范", "非", "份", "风"},
    e = {"而", "二", "儿", "耳", "尔", "饿", "恶", "额", "鹅", "蛾"},
    p = {"品", "片", "平", "批", "票", "评", "破", "派", "盘", "配"},
    o = {"噢", "哦", "欧", "偶", "呕", "藕", "殴", "鸥", "讴", "瓯"},
    u = {"呜", "乌", "污", "屋", "无", "五", "午", "武", "舞", "雾"},
    v = {"绿", "律", "率", "虑", "旅", "履", "吕", "铝", "侣", "屡"}
  }
  
  local l = {}
  local single_chars = {}
  local phrases = {}
  
  -- 分类：单字和词组
  for cand in input:iter() do
    local len = utf8.len(cand.text)
    if len == 1 then
      table.insert(single_chars, cand)
    else
      table.insert(phrases, cand)
    end
  end
  
  -- 对单字按高频优先排序
  local function sort_single_chars(chars, input_code)
    if not input_code or #input_code == 0 then
      return chars
    end
    
    local first_letter = string.sub(input_code, 1, 1)
    local high_freq = high_freq_chars[first_letter]
    
    if not high_freq then
      return chars
    end
    
    -- 创建优先级映射
    local priority = {}
    for i, char in ipairs(high_freq) do
      priority[char] = 1000 - i  -- 越靠前优先级越高
    end
    
    -- 排序
    table.sort(chars, function(a, b)
      local pa = priority[a.text] or 0
      local pb = priority[b.text] or 0
      if pa ~= pb then
        return pa > pb
      end
      -- 相同优先级则保持原顺序（按词频）
      return false
    end)
    
    return chars
  end
  
  -- 获取输入码（用于判断首字母）
  local context = env.engine.context
  local input_code = context.input
  
  -- 排序单字
  single_chars = sort_single_chars(single_chars, input_code)
  
  -- 输出：单字在前，词组在后
  for _, cand in ipairs(single_chars) do
    yield(cand)
  end
  
  for _, cand in ipairs(phrases) do
    yield(cand)
  end
end

return single_char_first_filter
