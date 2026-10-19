# 日语输入按键表来源

## AZIK Google/Mozc

- 表维护者 pokkur 发布的 Google 日本語入力用 AZIK 表：
  https://gist.github.com/pokkur/7175577c77020ab468c03cd63fcc8f6d
  是社区维护的 Google/Mozc 表，不冒充木村清本人发布的官方 TXT，也不是 AZIT / Simplified-AZIK。
- 固定原文件 URL：
  https://gist.githubusercontent.com/pokkur/7175577c77020ab468c03cd63fcc8f6d/raw/d89d174d04138c84919158bdefb3e931896f967b/google-ime_azik.tsv
- 下载日期 2026-10-01；查询时 gist history revision de08b7aee572faef6a8d58e620a6d90406cf6b9c。
- 原件 `AZIK_google_pokkur_original.tsv` SHA256：81A983B9B166F986F417CA1EA31FEE45B8EB69A9A06B58409F318A18807E6AA9。
- `tools/build-azik-table.ps1` 保留全部 621 原始行，其中 pyu 重复两次且含义完全相同，620 唯一键。
  增加本项目的句号/逗号直接上屏两条，生成 `AZIK_Google.txt` 共 622 唯一规则。
  不加普通罗马字兜底，不修改任何上游读音。文件未声明许可，不自行补写许可声明。
- 转换后 SHA256：AC32A157F15BCFCEF1ECE06F67DDF32FB555E334784614BA07E1BBA536D5A887。
- 示例：kq→かい、kw→けい、kp→こう、kz/kn→かん、kd→けん、kk→きん、q/nn→ん、;→っ。
  kk 不是普通罗马字的促音；用 ka;ta→かった。kf→き，而现有 AZIT kf→かい，两表保留区别。
- 生产解析器逐条核对 622 条、21 个组合示例、待输入前缀及导入读回通过。
  实际配置库 ID e57312b3e6ec48ec9f61eed311656b84；当前 AZIT 的 TSV/JSON/state hash 不变。
- 独立真实 librime 会话验证候选、同一未提交 kf 在 AZIK/AZIT 间热切换、退格重输、
  日语专用和中日混输的 ds 加句号提交为「です。」；旧 AZIT/RGT/SZ 回归也通过。
  未操作鼠标，未进行前台 TSF 输入/截图验收，未重启用户后台或替换设置程序。

## Kagiroi SZ Romaji

- 来源：https://github.com/rimeinn/rime-kagiroi/blob/1951bab3742495f763b79809af253884c33358e9/kagiroi_szromaji.dict.yaml
- 上游 commit：1951bab3742495f763b79809af253884c33358e9，下载日期 2026-10-01（Asia/Tokyo）。
- 原始字典标注 draft v2.5、version 20250804，属实验布局；不是作者直接提供的 Google TXT。
- 原始 YAML SHA256：85139A33417F5381E3BBA79CC7CDA4A1362CEFE9CFD24788C6B44958A0FAFD70。
- 转换表 Kagiroi_SZ_Romaji.txt SHA256：47E0E50F6A320132FD75B16546D3757DCB79F81608A0896709203DB09DE31CD6。
- tools/build-sz-romaji-table.ps1 原样交换 text/code 两列，1106 条原映射全部保留；没有重码，不添加普通罗马字兜底。另加入 kagiroi.yaml 的 punct/3_dan/full_shape 的句号和逗号，共 1108 条。否则现有引擎的句号直接上屏会失败。
- 原始字典、schema、公共 preset 和仓库 LICENSE 均保留。字典头声明 MIT，而仓库 LICENSE 是 GPL-3.0，保留原声明，不自行推断或更改许可。
- 示例：sz→せい、zz→ぜい、kz→けい、kf→けん、ds→です、ms→ます、nn→なに；nn 不是普通罗马字的「ん」。
- 已使用生产解析器逐条验证 1108 条映射、组合输入、待输入前缀和保存读回，加入用户配置库 f8e1aeeb32b547a080fcecbc59cb523c；active TSV/JSON/state hash 未变，原 AZIT 保留。
- 独立真实 librime 会话覆盖 SZ 候选、同一未提交 nn 在 AZIT/SZ 间热切换、退格重输、ds 加句号上屏，退出 0；不等同于桌面 TSF 全项实测。本次不操作鼠标，不重启输入法后台，不替换面板二进制。

## AKR 2021_07

- 作者仓库：https://github.com/kirameister/AKR_input_sequence
- 原文件：https://raw.githubusercontent.com/kirameister/AKR_input_sequence/main/AKR_table_2021_07.txt
- 下载日期：2026-10-01（Asia/Tokyo）
- 原文件 SHA256：CAD90732D9309DCBD0B8C10276BF8A7C2B1946D750BE97600AEE812E1B1A7FB0
- 原文件原样保留为 `AKR_table_2021_07.txt`；生产解析器解析为 579 条规则。
- 通过隔离目录及实际用户目录的生产解析、转换、保存和读回检查。
- 示例：v→ん、q→ー、kv→かん、kf→かい、kw→けい、bba→っば、kka→っか、kavji→かんじ、ds→です、sq→しゃ、;;→っ。
- 注意 `nj→ぬん` 是这张原表的规则；因此 `kanji` 并非普通罗马字的「かんじ」，用 `kavji`。不自行改写作者规则。
- 实际 profile ID：a7862110e01e46b9bc65ac1b6d63336b。
- 导入只新增 profile，不切换、不改 active TSV/JSON/state、也不发布配置代数；当前 AZIT 和开关保持原状。
- 这是按键映射测试，不是实际 TSF 候选/上屏测试。

## RGT

作者说明：https://qiita.com/dulunoj/items/95ebe5e40031183c5eea

未找到作者单独发布的 Google/Mozc TXT。找到作者 HPK 2.8 工具包及完整说明书，采用后期说明书的规则制作 **中日输入法适配版**，不是把自制文件冒充作者原表。

- 工具包来源：https://www.vector.co.jp/soft/winnt/util/se516450.html
- 下载：https://ftp.vector.co.jp/70/80/2331/Hpk2_8.zip
- 下载日期：2026-10-01；ZIP SHA256：32062AEF698A8D51E59AA04CC619BD5E0FB68C7340A7E5C1A73915D79F9071C0。
- 仅读取 ZIP 中说明书和说明文字，**没有运行 Hpk.exe**。规则以说明书第 21～26 页，尤其第 22 页键位表为准。
- 适配表：`RGT_HPK_2018.txt`，392 条规则；SHA256：02A02E98E332F43415F959CD15E2F234A9B65069DF0E11BEDF051032EE650531。
- 生成器：`tools/build-rgt-table.ps1`；普通罗马字沿用本项目已有 `romanji-hiragana.tsv`，移除会吞掉 RGT 单键的扩展、重复子音促音规则、tsu/kwa/gwa，再加入手册单键及 Q/W/P 二重母音。
- 该版本与 2017 年 Qiita 键位不同：f=す、j=て、z=き、b=の、y=と、n=な、q=ん、l=ー；kk=かか，促音用 x。
- 手册第 26 页 DS/MS 示例与第 22 页 S=し 键位冲突：按明确键位表实现，不暗改 S；“です”可用 df 或普通 desu。
- `# zhongri-mode: rgt-hpk-2018` 是本输入法的扩展声明，用于未完成前缀打消、空格分隔及 300 ms 停顿边界，不是 Google 的通用 TXT 功能。
- 暂不引入键盘工具的全局热键、长按功能或个人 VW/VP 句子宏；不修改用户快捷键。
- 生产解析器 24 个转换及 ky/sh/ch 待输入测试、导入读回和状态不变测试通过。独立 librime 会话的真实候选、300 ms 停顿、空格、退格、AZIT/RGT 未提交热切换通过。
- 已加入用户配置库：2f6ac1198f664982801912d788c7a824。新增配置时未改变用户当前表；实际窗口验收另见新版面板 TESTING.md，不将独立引擎结果冒充 TSF 窗口通过。
