# 雾凇拼音·中日混输输入法 V9

基于小狼毫（Weasel）与雾凇拼音的 Windows 中日混输输入法。中文拼音和日语罗马字可以直接混输，无需日语前缀。

## 下载与安装

请从 [GitHub Releases](https://github.com/swiftol/Rime_Config/releases/latest) 下载 **V9 全用户一键安装 EXE**。

安装包包含完整的中文、日语及翻译注释词库，安装后即可使用，不需要另外配置词库。安装程序会为 Windows 的实际登录用户部署配置，不会只写入管理员账户。

如果电脑已安装小狼毫，安装器会先备份现有用户配置，再安装 V9。通常无需重启 Windows；安装结束后重新部署或重启算法服务即可。

## 主要功能

- 中文拼音与日语罗马字直接混输
- 中文候选附带英文、日文翻译注释，且可分别开关
- 日语促音、长音、浊音及多组罗马字模糊匹配
- 微信输入法风格的展开候选窗与逐行翻页
- 候选文字、底色、宽度及生僻字过滤设置
- 图形化“中日方案设置”面板
- 空格键行为可切换：原始字母加空格，或选择第一个候选词

## 图形化设置

安装后运行小狼毫程序目录 `tools` 文件夹内的 `中日方案设置.exe`。

设置面板可修改翻译注释、日语模糊匹配、中文模糊纠错、候选外观、生僻字过滤和空格键行为。修改后点击“应用设置”；需要重新编译词库的设置会自动重新部署。





## 源码目录

- Rime 配置和词库：仓库根目录
- 图形化设置面板：[`src/RimeSettings`](./src/RimeSettings)
- 一键安装器：[`installer`](./installer)
- 修改版小狼毫界面：[swiftol/weasel](https://github.com/swiftol/weasel)
- 修改版 librime：[swiftol/librime](https://github.com/swiftol/librime)

## 致谢与许可

本项目基于 [雾凇拼音](https://github.com/iDvel/rime-ice)、[rime-japanese](https://github.com/gkovacs/rime-japanese)， [Rime](https://rime.im/) 和 [小狼毫](https://github.com/rime/weasel) 开发。各部分遵循其目录中注明的原始许可证；二次修改部分按仓库许可证发布。
