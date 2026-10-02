# 隐私与发布数据边界

[English](#english) | [简体中文](#简体中文)

## 简体中文

Rime 中日直输以本地输入为原则。普通本地候选、个人词频与设置管理在用户电脑上完成，不要求登录账号。云服务是可选功能，不启用时不会为了生成本地候选而发送输入内容。

启用 Google 云候选后，正在输入的拼写会发送给 Google Input Tools 请求候选；界面提供中文、日语或双语匹配选项。用户主动使用在线句子翻译时，所选文本会发送到用户配置的小牛翻译 API。请查看并遵循对应服务的隐私政策和使用条款。云候选关闭时，输入和本地候选仍可离线工作。

项目公开仓库和正式安装包不得包含：

- 用户实际输入内容或选词学习记录；
- 个人常用语、自定义短语和剪贴板历史；
- Rime 用户数据库、同步目录与安装标识；
- 日志、诊断包、用户名、机器名和本机绝对路径；
- 含桌面、任务栏、账号、文件名或私人窗口的截图；
- 维护者个人数据的备份或中间文件。

云候选缓存和个人选词学习记录保存在本机，不应提交到公开仓库或随公共安装包发布。用户配置的翻译凭据也不得写入代码、日志或发布包。

公开演示图必须使用专门生成的示例数据，只展示软件界面，并在提交前检查画面内容和文件元数据。

## English

Rime Chinese–Japanese Direct Input is local-first. Ordinary local candidates, personal frequency data, and settings are handled on the user's computer and require no account. Cloud services are optional; they are not contacted to produce local candidates.

When Google cloud candidates are enabled, the current spelling is sent to Google Input Tools to request suggestions; the setting supports Chinese, Japanese, or bilingual matching. When the user explicitly requests online sentence translation, the selected text is sent to the NiuTrans API configured by the user. Please review the relevant service privacy policies and terms. Local input and candidates continue to work offline when cloud candidates are disabled.

The public repository and release packages must not contain:

- typed text or candidate-learning records;
- personal phrases, custom dictionaries, or clipboard history;
- Rime user databases, synchronization folders, or installation identifiers;
- logs, diagnostic archives, usernames, machine names, or local absolute paths;
- screenshots containing desktops, taskbars, accounts, filenames, or private windows;
- backups or intermediate files containing maintainer data.

Cloud-candidate caches and personal selection-learning data are stored locally and must not be committed or shipped in public installers. User-configured translation credentials must not be written into source code, logs, or release packages.

Public visuals must be generated from dedicated sample data, show only the product UI, and be checked for both visible information and embedded metadata before publication.
