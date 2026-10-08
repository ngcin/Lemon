# M7c 批① —— Fx 表现升级（TTF 烘焙器 / 贴图血条+延迟条 / 飘字动效）

Status: done（2026-10-07 机器面全过 + svr-test 实装；**真人走查 2026-10-07 用户过**——暴击中文 Pop 黄字 / 延迟白条正常；贴图血条为 64×10 程序占位图形态（`Assets/bar_bg|fg.png`），正式美术素材同名替换后复验观感即全闭环（零代码改动）；截图基线待前台会话补。落地明细与教训 = [DevLog 2026-10-07 批①](../../DevLog/2026-10-07-m7c-b1-fx-presentation-upgrade.md)）

> **S2 首查项定案**：方案 A（SpritePacket 尾加 UV 覆盖 + flags bit2）——侦察发现
> `SpriteInstance` 48B 实例块本就有逐实例 u0/v0/u1/v1（02 §3.3），覆盖只是 Bake
> 期 CPU 取值来源切换：**实例布局零改动、shader 零改动 → 免 ADR-017**（"若涉
> 实例布局变更"条件不成立）。BitmapFont 外部页字形产包同走此通道（sprite 表
> 零登记，资产号段结构性无冲突）。

> 来源：用户需求 2026-10-07（血条要做好看=贴图；飘字要暴击/闪避/调字体）+ 三引擎对照讨论结论——Unity/UE/Godot 均无引擎级战斗飘字通道（怪海规模下三家用户手搓的终点 = Lemon 现有 FxChannel 形态），Lemon 缺的是三家家有的**表现力地基**：TTF 字体资产管线（对标 TMP SDF 烘焙）与贴图比例填充原语（对标 uGUI Filled Image / UE ProgressBar）。
>
> 动工前必读：[02 §7](../../EngineDesign/02-Rendering-Vulkan.md)（位图文本——「HUD 数字高频路径绝不动态栅格化」红线）、[06 §8](../../EngineDesign/06-Asset-Pipeline-Out-of-Box.md)（Fx 恒定原则：血条/飘字恒走 sprite 管线不进 UI 框架）、[09 §9](../../EngineDesign/09-Testing.md)（回归与 bench 门禁口径）。

## 1. 现状事实（2026-10-07 代码核对）

| # | 事实 | 位置 |
|---|---|---|
| 1 | 飘字：16 字符文本 + 颜色；轨迹全编译期常量（kTextLife 0.8s / kTextRise 24px 匀升 / 末 30% 淡出）；池 256 环形覆写最老 | `Engine/ECS/FxChannel.h` FxText |
| 2 | 血条：仅 frac/color/width，白精灵双四边形染色（bg 整宽 + fg 比例宽缩放），高恒 4px；池 128 实体键控；受击 sticky 3s 自隐 | FxBar + `Engine/Renderer/GameFx.cpp` 层 251 |
| 3 | 位图字体：内置 5×7 像素字模，仅 ASCII 32..126 共 95 字形；**中文字形零覆盖**（「暴击」「闪避」显示不出）；注释预留「TTF→图集离线生成器（M5+）」未落地 | `Engine/Renderer/BitmapFont.h` |
| 4 | `SpritePacket` 无逐实例 UV；子矩形靠切片静态注册（.meta 真源）→ 贴图血条按 frac **缩放**会把图片横向压扁，需**裁剪** | `Engine/Renderer/Renderable.h:54` |
| 5 | 渲染双壳共用 `AppendGameFx`（编辑器 GameView + lemon-game），改一处两入口同生效 | `Engine/Renderer/GameFx.{h,cpp}` |
| 6 | C# 面：`Fx.Text(string/float, pos, color)` / `Fx.Bar(GameObject, frac, color, width)`；桥走 vtable（现 47 槽）；FxChannel = 表现层语义**不入 StateHash/回放**（已确立，M6a 批① 落） | `Lemon.SDK/Fx.cs` |
| 7 | FreeType VER-2-14-3 = CPM 三平台依赖（RmlUi 消费）；Noto Sans SC 8.3MB 已随引擎（`Engine/Ui/Fonts/` + OFL） | `cmake/Dependencies.cmake` |

## 2. 设计要点

### S1 TTF→位图图集离线烘焙器（地基，先做）

- **复用 FreeType**（已是依赖零新增；07 登记面只补用途行）。形态 = **编辑器导入期烘焙**（后台 worker，M6c 批① 后台烤音频同款先例）：输入 TTF + 字号 + 字符集 → 输出字形图集页 + 度量表（advance/bearing/宽高），产物缓存 `.lemon/`；**运行时零 FreeType 零栅格化**（02 §7 红线不破——与 RmlUi 运行时字体链并存，各管各的）。
- **字符集**：项目级配置文件（默认 ASCII 95；svr-test 给示例表 = 数字 + `暴击闪避格挡MISS` 等战斗词表）；字符集变更 = 增量重烘（哈希键缓存命中跳过）。
- **风格最小版**：字号（多档可烘多页）+ **描边**（烘焙期扩边像素，描边色参数）；SDF/渐变/发光/字距 kerning 登记不排。
- **BitmapFont 扩展**：支持外部页替换/并存（内置 5×7 页 = 兜底）；缺字形回退内置页 + 红字一次（不静默豆腐块）。
- **资产化形态开工定**（二选一）：A. 字体资产类型（.meta/GUID 进 AssetBrowser，.clip 族导入器先例）；B. 最小版 = 项目配置驱动 + 引擎工具函数（不进浏览器）。**建议 A**——与 06 §2 类型表一致的终局形态，B 是 A 的子集可先做后升格。

### S2 贴图血条 + 延迟条

- `FxBar` **尾加**：`bgSpriteGuid`/`fgSpriteGuid`（0 = 白精灵现状路径，向后兼容零迁移）+ `lagColor`（延迟条开关兼配色）+ `height`（退役 kBarHeight 恒 4 常量）。
- **fg 比例 = 横向 UV 裁剪（非缩放）**；延迟条 = Simulate 内 lagFrac 按追赶速率衰减向 frac 收敛（表现层语义，天然不入回放）。
- **开工首查项（本批唯一可能碰渲染内核的点）**：`SpritePacket` 尾加 UV 覆盖字段（0 = 整图）→ 核对实例块布局/顶点读取兼容（02 渲染分册 + RHI 实例上传路径）；若实例布局已冻结不宜动 → 退方案 B：Fx 专用产包小路径（GameFx 内直接产带 UV 的精灵描述，走现有 per-instance 通道的子矩形变体）。**A/B 开工首日定，ADR-017 落一条**（若涉实例布局变更）。
- 九宫格血条登记不排（横向 UV 裁剪已覆盖主流 2D 血条形态）。

### S3 飘字动效参数化

- `FxText` **尾加**：`scale`（默认 1）/ `lifeOverride`（0 = 默认 0.8s）/ `driftX`（水平漂移，暴击散布用）/ `curve` 枚举（Linear = 现状匀升 / **Pop** = 出生 1.4× 回落 1.0 + 更陡上浮）。
- 池语义不变（256 环形）；**提额登记不排**（若一局满屏跳字触顶再议——kMaxTexts 是常量，改字段包尺寸属破坏性变更需 ADR）。

### S4 C# SDK + 桥

- `Lemon.Fx`：`FxStyle`（scale/life/drift/curve）与 `FxBarSkin`（bgGuid/fgGuid/lagColor/height）结构 + `Text`/`Bar` 重载 + `Fx.Crit(text,pos)` / `Fx.Miss(pos)` 糖（默认黄字 Pop 大号 / 灰白小号——纯默认值糖，游戏侧可全自定义）。
- 桥：vtable 尾加（47→N，既有 Fx 通道族扩展先例）；C++/C# 字段表同步（LayoutTables 口径）。
- **确定性反例单测**（TestAudioSdk「音频调用后 state hash 不变」先例）：新参数全开的 Fx 调用前后 StateHash 逐位不变——锁死「表现层永不入回放」。

### S5 svr-test 接入 + 验收场景

- svr-test 实装：暴击「暴击 2333」黄字 Pop 弹跳 + 敌人贴图血条（bg/fg 各一张）+ 受击延迟白条；战斗词表字符集配置（S1 示例即它）。
- 模板 vs-survivor 是否同批跟进开工定（最小判据 = svr-test 实证即可，模板跟进降为顺手项）；截图基线落 `docs/Baselines/`。

## 3. 工作量与顺序

S1 烘焙器 1.5–2 天（FreeType 调用 + 装箱 + 缓存 + BitmapFont 扩展）→ S2 血条 1–1.5 天（UV 方案核对 + FxBar 扩展 + Simulate）→ S3+S4 飘字与 SDK 1 天 → S5 接入验收 0.5 天。**合计 4–5 天**（S1 与 S2 可并行开工不同 TU）。

## 4. 登记不排（砍单防线）

SDF 距离场缩放 / 九宫格血条 / 飘字池提额 / 渐变填充 / kerning 字距 / 图集内字形共享多字号 / CI 字体烘焙缓存。

## 5. 验收判据（出口）

- **机器面 ✅（2026-10-07）**：单测新增四件全落——烘焙器 golden（两次烘焙**字节一致** + 四种参数变更态 stale）/ Fx 数学（lag 单调收敛+回升贴平、UV 裁剪区间+白精灵现状路径回归、Pop 出生 1.4× 回落+陡升、寿命覆盖+过期隐形、池语义）/ hash 反例（TestFxSdk：新参数全开 StateHash 逐位不变）/ 多页寻址与缺字形回退（UV 覆盖产包+批键烘焙槽+混排回退）；checks 34,346→**34,402**；**ctest 4/4**；实跑链路（--play ani.scene 900 帧：装载 ×2 / play-roundtrip byte-exact / errors=0 / duel.winner 落档）+ `--validate` 两场 1080 帧零 VUID；回归 full 两轮（15/20→19/20，注入族首轮假红全数转绿）+ **bench 独立 ×3 = 85/87/87 全 PASS 且 `fx 256+128 饱和` 口径 ≥ 基线 85**（数字与 flake 判读 = [DevLog](../../DevLog/2026-10-07-m7c-b1-fx-presentation-upgrade.md)）。
- **真人面（用户走查 2026-10-07 过）**：暴击中文弹跳黄字 / 延迟白条正常；贴图血条 = 占位图形态已见（64×10 程序生成），正式素材落地后同名替换复验观感即全闭环；截图基线待前台会话补（本会话窗口回读黑 = 09 §9 后台现象，基线 smoke 截图对照复现确认非批① 缺陷）。走查轮④（暴击显示邻字形/血条悬空——烘焙器像素基址 bug + 帧边距锚定）落地 = [DevLog 轮④](../../DevLog/2026-10-07-m7c-b1-fx-presentation-upgrade.md)。
