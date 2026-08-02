-- trilingual_mix_translator.lua
-- 智能中日英混输翻译器
-- 根据输入自动识别语言并返回候选

local function is_japanese_romaji(input)
    -- 日文特征：含有 tsu, shi, chi, fu, wa, wo, ya, yu, yo, n(单独的n)
    local jp_patterns = {
        "tsu", "shi", "chi", "fu", "wa", "wo", 
        "ya", "yu", "yo", "kya", "kyu", "kyo",
        "sha", "shu", "sho", "cha", "chu", "cho",
        "nya", "nyu", "nyo", "hya", "hyu", "hyo",
        "mya", "myu", "myo", "rya", "ryu", "ryo",
        "gya", "gyu", "gyo", "ja", "ju", "jo",
        "bya", "byu", "byo", "pya", "pyu", "pyo"
    }
    
    for _, pattern in ipairs(jp_patterns) do
        if string.find(input, pattern) then
            return true
        end
    end
    
    -- 常见日文单词
    local jp_words = {
        "konnichiwa", "arigatou", "sayonara", "ohayou",
        "konbanwa", "sumimasen", "gomen", "douzo",
        "itadakimasu", "gochisousama", "omedetou",
        "nihon", "tokyo", "osaka", "kyoto", "sushi",
        "ramen", "tempura", "sakura", "manga", "anime"
    }
    
    for _, word in ipairs(jp_words) do
        if input == word then
            return true
        end
    end
    
    return false
end

local function is_english(input)
    -- 全小写英文单词特征
    -- 如果包含 th, ph, ght, tion, ing 等英文特有组合
    local en_patterns = {
        "th", "ph", "ght", "tion", "ing", 
        "ness", "ment", "able", "ful", "less"
    }
    
    for _, pattern in ipairs(en_patterns) do
        if string.find(input, pattern) then
            return true
        end
    end
    
    return false
end

local function translator(input, seg)
    -- 如果是单个字母，不处理
    if #input <= 1 then
        return
    end
    
    local env = seg.env
    local context = env.engine.context
    local composition = context.composition
    
    -- 判断输入类型
    local is_jp = is_japanese_romaji(input)
    local is_en = is_english(input)
    
    -- 根据判断结果，调用对应的翻译器
    if is_jp then
        -- 日文输入，提高日文候选优先级
        -- 这里需要调用 RIME 的 japanese translator
        local jpTranslator = Component.Translator(env.engine, "", "script_translator@japanese")
        if jpTranslator then
            for cand in jpTranslator:query(input, seg) do
                cand.quality = 10  -- 提高优先级
                yield(cand)
            end
        end
    end
    
    if is_en then
        -- 英文输入
        local enTranslator = Component.Translator(env.engine, "", "table_translator@english")
        if enTranslator then
            for cand in enTranslator:query(input, seg) do
                cand.quality = 8
                yield(cand)
            end
        end
    end
    
    -- 总是提供中文候选（优先级较低）
    local cnTranslator = Component.Translator(env.engine, "", "script_translator@chinese")
    if cnTranslator then
        for cand in cnTranslator:query(input, seg) do
            cand.quality = is_jp and 5 or 7  -- 如果识别为日文，降低中文优先级
            yield(cand)
        end
    end
end

return translator
