# 1.0.2 已安装实例热修复说明

此包只用于另一台测试电脑验证已确认的 Lua/data 缺失问题，不是正式发布包。

## 操作边界

- 由另一台电脑上的 Codex 执行。
- 操作前备份将被替换的文件。
- 不删除整个用户目录，不删除个人词频、常用语、同步数据或用户数据库。
- 不卸载、不重装。

## 文件映射

- `config\lua\*.lua` 复制到 `%APPDATA%\Rime\lua\`
- `runtime\data\*` 复制到 `C:\Program Files\RimeChineseJapanese\data\`

复制后结束 `WeaselServer.exe` 和 `WeaselDeployer.exe`，使用
`C:\Program Files\RimeChineseJapanese\WeaselDeployer.exe /deploy`
重新部署，再启动 `WeaselServer.exe`。

## 验收

1. 最新日志中不再出现以下错误：
   - `module 'japanese_fuzzy_learning' not found`
   - `module 'japanese_fuzzy_learning_processor' not found`
   - `module 'japanese_prefix_translator' not found`
   - `attempt to call a string value`
   - `attempt to call a nil value`
2. 在真实小狼毫 TSF 会话输入 `nihao`，必须出现中文候选。
3. 再检查日语精确输入和日语模糊匹配，确保三个模块的导出类型正确。

