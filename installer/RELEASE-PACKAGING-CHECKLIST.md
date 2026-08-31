# 中日输入法安装包发布检查清单

> 本文件是每次制作 EXE 前必须逐项执行的发布门禁。任何一项失败，都不得生成正式安装包、不得上传 GitHub、不得让用户在其他电脑试错。

## 一、必须记住的 1.0.2 事故

1.0.2 曾经漏装以下运行依赖：

- `lua/japanese_fuzzy_learning.lua`
- `lua/japanese_fuzzy_learning_processor.lua`
- `lua/japanese_prefix_translator.lua`
- 安装目录下完整的 `data/`

方案文件仍然引用这些组件，导致真实 `WeaselServer` 能启动、词库也存在，但 Lua 候选管线报错，最终输入 `nihao` 没有任何候选。

事故能在开发电脑上被掩盖，是因为开发电脑的用户目录或旧安装中已经残留所需文件。只检查“安装成功”“进程启动”或“词库文件存在”都不够，必须在全新的隔离用户目录中做真实引擎候选测试。

## 二、发布原则

- 先构建本地候选安装包，再由用户在另一台测试电脑安装。
- 测试电脑验收通过后，才能上传 GitHub Release。
- 不允许先上传、再让用户测试，也不允许用相同文件名悄悄替换已经发布的错误文件。
- 开发版、安装载荷和安装后文件必须来自同一次提交、同一套源文件。
- 不以“开发机可以输入”作为发布依据。
- 打包和自动测试不得部署、清理或覆盖开发电脑当前正在使用的 Rime 用户目录。
- 旧版本很多的电脑必须能够直接覆盖升级；升级必须保留个人数据，并清理会干扰当前版本的旧运行库注册和残留路径。

## 三、安装载荷完整性门禁

### 3.1 必需 Lua 模块

至少检查以下文件全部存在且非空：

```text
lua/japanese_fuzzy_filter.lua
lua/japanese_fuzzy_learning.lua
lua/japanese_fuzzy_learning_processor.lua
lua/japanese_prefix_translator.lua
```

同时扫描 `rime.lua` 和 `*.schema.yaml` 中所有 `require(...)`、`lua_processor@...`、`lua_translator@...`、`lua_filter@...` 引用，确认对应文件及导出函数真实存在。禁止只维护一份手工文件名单。

### 3.2 runtime data

安装目录的 `data/` 至少必须包含：

```text
default.yaml
essay.txt
punctuation.yaml
opencc/
```

当前完整基准为 90 个文件、13,689,104 字节。构建门禁的最低阈值为 50 个文件、10 MB；低于阈值必须立即失败。

### 3.3 核心运行文件

确认安装载荷至少包含：

```text
WeaselServer.exe
WeaselDeployer.exe
WeaselSetup.exe
rime.dll
weasel.dll
weaselx64.dll
RimeCandidateSelfTest.exe
config/rime_ice_japanese.schema.yaml
```

检查 DLL/EXE 位数一致、直接导入依赖可解析，并记录安装包 SHA-256。

## 四、隐私排除门禁

安装包不得包含任何开发者或用户运行后产生的个人数据：

```text
custom_phrase.txt
lua/common_phrase_data.lua
*.userdb/
*.userdb.txt
user.yaml
installation.yaml
sync/
clipboard/
日志
缓存
本机备份
```

公共词库、公共日语词库、公共翻译注释、方案配置和程序资源必须保留，用户下载安装后应当可以直接使用。

程序引用可选个人文件时，必须使用安全降级逻辑。例如没有 `common_phrase_data.lua` 时使用空表，不能让整个 `rime.lua` 初始化失败。

## 五、构建前检查

- 确认版本号、文件名、安装器显示版本和日志版本完全一致。
- 确认 `payload/config` 来自当前开发版，而不是旧备份方案或旧发布目录。
- 确认 `payload/runtime` 来自当前验证通过的修改版小狼毫运行库。
- 确认没有从 `%APPDATA%\Rime\build` 复制开发机缓存来掩盖缺失源文件。
- 确认默认方案为 `rime_ice_japanese`，不能意外加载 V4、V5 等历史备份方案。
- 确认安装器统一当前用户目录为 `%APPDATA%\Rime`，同时正确写入需要的 32/64 位注册表视图。
- 验收工具必须从注册表读取实际运行目录，禁止把 `C:\Program Files\RimeChineseJapanese` 写死；用户允许安装到其他目录。
- 确认安装器能区分首次安装与增量升级；词库未变化时应复用有效缓存。

## 六、构建时强制测试

运行：

```powershell
cd installer
.\build-release.ps1 -RuntimeRoot <已验证的运行库目录>
```

构建脚本必须执行以下步骤，任何一步失败都停止：

1. 重新编译 `RimeCandidateSelfTest.exe`，禁止沿用不明来源的旧测试程序。
2. 生成本次安装包的精确 `payload`。
3. 检查必需 Lua、runtime data、核心 EXE/DLL 和隐私排除项。
4. 创建全新的隔离用户目录，只复制本次 `payload/config`。
5. 使用本次 `payload/runtime` 完整部署隔离目录。
6. 真实选择 `rime_ice_japanese` 并输入 `nihao`。
7. 必须得到 `CANDIDATE_COUNT > 0`，且第一候选为 `你好`。
8. 只有以上步骤全部通过，才调用 Inno Setup 生成 EXE。

预期关键输出：

```ini
DEPLOY_FINISH=1
SCHEMA=rime_ice_japanese
CANDIDATE_COUNT=36
CANDIDATE_1=你好
```

候选数量可以随词库变化，但不得为 0；第一候选必须保持为“你好”。

## 七、其他电脑安装验收

### 7.1 测试顺序

1. 在安装过多个旧版本的测试电脑上直接覆盖安装，不要求用户手工删除旧文件。
2. 等安装器明确完成部署；进度中 CPU 持续增加表示仍在编译，并非卡死。
3. 解压并运行最新的 `Rime-CNJP-*-Post-Install-Test-v*.zip`。
4. 验收脚本必须是只读测试，不能卸载、重装、清理用户数据或接管鼠标。
5. 自动验收全部通过后，再进行真实 TSF 输入：记事本中输入 `nihao`，确认出现“你好”。

### 7.2 验收必须通过的项目

```text
REG_RUNTIME
REG_USERDIR
RUNTIME_FILES
PRODUCT_LUA
RUNTIME_DATA
CLEAN_RUNTIME
OLD_RUNTIME
SERVER
CANDIDATES
PE_IMPORTS
```

出现任何 `FAIL` 都不得发布。必须读取真实 `WeaselServer` 日志定位原因，不能反复猜测、不能要求用户盲试。

## 八、发布前最终核对

- 用户明确回复测试通过。
- 安装包大小、修改时间和 SHA-256 已记录。
- 本地测试过的文件与准备上传的文件 SHA-256 完全一致。
- Release 中只放当前可用安装包；已知不可用的小版本应删除或明确撤回，不能继续误导下载。
- Release 说明只写用户需要知道的功能、升级方式和校验值，不写个人数据内容或无关内部对话。
- 保留历史 Release 的正确安装包；如果历史包已知不可用，应标记为不可用或移除其下载资产。
- 上传完成后重新下载一次，对下载文件再次计算 SHA-256，并与本地值比较。

## 九、发生故障时的纪律

- 先运行只读诊断，再修改安装包。
- 先确认真实运行进程、实际加载路径、用户目录、Lua 日志和候选管线。
- 不根据独立 `LoadLibrary` 的单一结果直接下结论，要以真实 `WeaselServer` 行为交叉验证。
- 不把单词或单条配置硬编码成“修复”；必须解决通用机制或打包清单问题。
- 每次修复后重新走“全新隔离部署 → 真实候选 → 另一台电脑验收”的完整流程。
- 将新发现的事故原因和防复发门禁补充到本文件，不能只修代码、不修流程。

## 十、当前 1.0.2 已验证基准

```text
安装包：Rime-Chinese-Japanese-1.0.2-Setup.exe
SHA-256：D6EF75F4ABBB6BDFFDD4B40618851E0986AE71BEF825D61957E39847D7C766AF
Lua：4/4
runtime data：90/90
隔离部署：成功
nihao 候选数：36
第一候选：你好
```

该哈希仅对应 2026-08-24 重新构建的候选测试包。在另一台电脑完成最终验收前，不得上传 GitHub。
