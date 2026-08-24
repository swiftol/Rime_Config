# 安装包构建与隐私边界

安装包包含公开的中文词库、日语词库、翻译注释、Rime 配置、Lua 扩展、设置面板和修改版小狼毫运行库，安装后可以直接使用。

安装包不得包含任何运行后产生的个人数据：

- `*.userdb/`、`*.userdb.txt`、`user.yaml`、`installation.yaml`
- `custom_phrase.txt` 与生成的 `lua/common_phrase_data.lua`
- `clipboard/`、`sync/`、日志、缓存和本机备份

`RimeUserBootstrap.exe` 会在原 Windows 用户身份下同步两个注册表视图、备份原 Rime 用户目录、保留个人数据并重新部署。全局程序文件由管理员安装；当前用户立即配置，其他 Windows 用户通过 Active Setup 在首次登录时获得同一套公共配置。
