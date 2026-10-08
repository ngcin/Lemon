# M7c 实施计划 —— 引擎与编辑器功能段（2026-10-07 规划：M7b 发行侧后移，引擎优先）

Status: in-progress（**批⓪ ✅ 2026-10-07**——四件全落、回归 full 20/20，[DevLog](../../DevLog/2026-10-07-m7c-b0-engineering-hygiene.md)；**批① ✅ 2026-10-07 机器面**——Fx 表现升级五子项全落（TTF 烘焙器/贴图血条+延迟条/飘字动效/SDK vtable 49/svr-test 实装），单测 34,402、ctest 4/4，[批文件](./2026-10-07-b1-fx-presentation-upgrade.md)、[DevLog](../../DevLog/2026-10-07-m7c-b1-fx-presentation-upgrade.md)，真人走查 2026-10-07 过——飘字/延迟条正常，贴图血条为占位图形态、正式素材落地后复验观感；**批② ✅ 2026-10-07 机器面**——per-资产音频参数全链落地（ClipFx 覆写回退链/meta 三键双源解析/SetAudioImporter 写器/浏览器右键参数弹窗），单测 34,435、ctest 4/4、回归 full 20/20 首跑全绿、bench fps=85，[批文件](./2026-10-07-b2-per-asset-audio-fx.md)、[DevLog](../../DevLog/2026-10-07-m7c-b2-per-asset-audio-fx.md)，真人听感待用户；**批③ ✅ 2026-10-08 机器面**——编辑器 UI 国际化（tr 模块 + Strings JSON 601 key/语言 + ~400 调用点 + display 显示名映射 + 菜单即时切换），双语言 smoke PASS、截图视觉核验过，[批文件](./2026-10-08-b3-editor-i18n.md)、[DevLog](../../DevLog/2026-10-08-m7c-b3-editor-i18n.md)，真人走查待用户；批④ 起随 svr-test 需求滚动登记）

> 总览页惯例（M6b/M6c/M7a 同款）：每批一个文件（落 `Plans/M7c/`），开工前分解到文件/行级，完工后批文件内勾销；事件流水与实测数字记 [DevLog](../../DevLog/)。本页只做拆解与验收映射，设计定形物（如需）另落 ADR。
>
> 输入：用户拍板（2026-10-07，[DevLog](../../DevLog/2026-10-07-roadmap-reorder-m7b-deferred.md)）——①三件工程卫生事（AGENTS.md 瘦身+测试拆文件 / ccache / .clang-format）合成批⓪；②Fx 表现升级（贴图血条/延迟条/飘字动效/中文字体）合成批①；③M7b 发行侧（Steam/云档/成就）后移，引擎与编辑器功能优先。前置讨论 = FxChannel 现状核对（本页 §1 事实表）+ 三引擎对照（Unity/UE/Godot 均无战斗飘字通道，Lemon 的池化 sprite 通道形态领先，缺的是表现力地基：TTF 烘焙器与贴图比例填充）。

## 0. 段位定位与命名

- **语义**：引擎与编辑器功能批段，M6a「游戏驱动、引擎追着喂」模式的复刻——主消费者 = 用户幸存者项目（`demo/svr-test`），批② 起的题材随游戏侧卡点滚动登记。
- **命名**：承 M7 系列字母序（M7a ✅ → M7c 执行 → M7b 后移至 M9 之后），语义独立于「发布管线」；若用户偏好别的代号，批⓪ 开工前全局替换即可（成本 = 本页 + 08 两处 + DevLog 一处）。
- **与既有档位的关系**：不动 M8（光照与打磨）/M9（Tilemap+TD）定义；M7b 移交清单（[M7a 批⑧ 批文件](../M7a/2026-10-06-b8-closeout.md) §2，含图集压缩）随段后移，时点 = 真要上 Steam 时。

## 1. 现状盘点（2026-10-07 探测核实）

| # | 事实 | 对本段的含义 |
|---|---|---|
| 1 | `AGENTS.md` 全文 46 行，**行 3 单段 50,199 字节**（M5→M7a 各批完工流水全部内联） | 批⓪ T1 主体；每会话固定 token 烧伤源，且违反 docs/README.md 自家 30KB 拆分精神 |
| 2 | `tests/engine_tests.cpp` **7,939 行 / 139 个 Test 函数单 TU**；tests/ 仅 3 个源文件 | 批⓪ T2 主体；纯搬家可拆，ctest 入口名不变 |
| 3 | 全仓无 ccache/sccache（`CMAKE_*_COMPILER_LAUNCHER` 零命中）；preset ×6 = mac/mac-debug/win/win-debug/win-ci/win-share | 批⓪ T3；干净重建第三方 TU（SDL3/FreeType/RmlUi/miniaudio/stb）全量重编 |
| 4 | 无 `.clang-format`/`.clang-tidy`/`.editorconfig` | 批⓪ T4；多会话 AI 协作下风格漂移无机械防线 |
| 5 | Fx 现状（[FxChannel.h](../../../Engine/ECS/FxChannel.h)）：飘字 16 字符/轨迹全编译期常量（kTextLife 0.8s/kTextRise 24px）；血条仅 frac/color/width 白精灵染色；池 256/128 | 批① S2/S3 的尾加字段面 |
| 6 | 位图字体 = 内置 5×7 像素字模，仅 ASCII 32..126（[BitmapFont.h](../../../Engine/Renderer/BitmapFont.h)），注释预留「TTF→图集离线生成器（M5+）」至今未落 | 批① S1 的立项依据；中文字形（暴击/闪避）当前显示不了 |
| 7 | `SpritePacket`（Renderable.h:54）无逐实例 UV；子矩形靠切片静态注册 | 批① S2 开工首查项：贴图血条比例裁剪需渲染侧通道（方案 A/B 见批文件） |
| 8 | FreeType VER-2-14-3 已是 CPM 三平台依赖（RmlUi 消费）；Noto Sans SC 已随引擎（`Engine/Ui/Fonts/`） | 批① S1 地基现成——烘焙器只差工具本身 |
| 9 | 基线（M7a 批⑧ 出口）：回归 full 20 步 / ctest 4/4 / 单测 34,346 / script-tests ~1,780 / bench-survivor 门禁 fps≥76.5（基线 85×0.9）/ vtable 47 槽 / 组件 id 至 31 / 系统 20 | 全段零降级对表基线；批① 桥面尾加 vtable 47→N |
| 10 | 待用户尾巴（2026-10-07 二更）：批① 血条正式素材观感复验 / W5 真机 GPU（用户再后移，不设期）/ M4.8 走查其余 UX 优化待细化——M4.8 走查、svr-test V1–V3、批① 真人走查均已同日过 | 本段不新增用户验收债 |

## 2. 批次表

| 批 | 文件 | 主题 | 预估 | 出口判据 |
|---|---|---|---|---|
| ⓪ ✅ | [2026-10-07-b0-engineering-hygiene.md](./2026-10-07-b0-engineering-hygiene.md) | 工程卫生四件：AGENTS.md 瘦身（50KB 巨段 → 指针化）/ 测试拆文件（139 函数按域拆 TU）/ ccache+sccache 接入六 preset / .clang-format 最小集 | 0.5–1 天（实际 1 日） | AGENTS.md <150 行且待用户清单保留；checks 34,346 不变；回归 full 20/20；干净重建时间对比数字入 DevLog |
| ① ✅ | [2026-10-07-b1-fx-presentation-upgrade.md](./2026-10-07-b1-fx-presentation-upgrade.md) | Fx 表现升级：TTF→位图图集离线烘焙器（中文字形/描边）+ 贴图血条+延迟条（UV 裁剪）+ 飘字动效参数化（scale/life/漂移/曲线）+ `Lemon.Fx` 重载 | 3.5–5 天（实际 1 日） | svr-test 暴击中文弹跳黄字/贴图血条/延迟条真人走查过；hash 反例单测；bench-survivor 门禁不降 |
| ② ✅ | [2026-10-07-b2-per-asset-audio-fx.md](./2026-10-07-b2-per-asset-audio-fx.md) | per-资产音频参数（M6c 缓议登记项启用）：听感覆写三件（重触发节流窗/同 clip 并发上限/音高微扰幅度）meta 覆写 + 引擎回退链 + 浏览器右键参数弹窗 | 1 日（实际当日） | 单测覆写回退链 + meta roundtrip 全绿（**34,435**）；ctest 4/4；回归 full **20/20 首跑**；bench 门禁 fps=85；svr-test 真人听感对比过（Mixer 全局面不变）——**机器面 ✅，真人听感待用户** |
| ③ ✅ | [2026-10-08-b3-editor-i18n.md](./2026-10-08-b3-editor-i18n.md) | 编辑器 UI 国际化（用户 2026-10-08 提出）：tr 模块 + Strings JSON（12 模块 601 key/语言）+ ~400 调用点迁移 + 反射显示名映射（display.*）+ Window 菜单即时切换（editor-settings.json 持久化）；术语对齐 Unity/Godot 官方简中 | 1 日（实际当日） | 对齐校验 12/12；zh/en 双语言 `--smoke` PASS + 截图视觉核验；单测 34,435、ctest 4/4、构建零警告——**机器面 ✅，真人走查待用户** |
| ④+ | （滚动登记，开工落文件） | 候选池：动画 auto-slice（T3-UX2 遗留）/ LoadScene 档2 / 手柄输入（随 Steam 目标确认，与 M7b 联动）/ 飘字池提额（若一局满屏跳字触顶）/ **编辑器资产引用拖放**（M4.8 走查反馈 2026-10-07：下拉选在资产多时难寻 → Unity 式拖放置入 + 其余走查优化待细化补登；用户拍板后置，功能优先） | — | 每批开工前本表登记一行 + 批文件落位 |

## 3. 段内排序与全局位次

- **批内顺序**：批⓪ → 批①（批⓪ 的测试拆文件先落，批① 新增 Fx 单测直接进新 TU 结构，避免拆两次）。
- **全局新序**：M7c（滚动段，至少批⓪①）→ M8 光照与打磨 → M9 Tilemap+TD → M7b 发行侧。理由：M8 的 soak 长稳与光照直接服务「把引擎做好」；M9 是第二品类模板（TD），无近期消费者时不抢前。
- **可调点**（不重排文档，开工时口头定）：若 TD 模板需求提前（比如 svr-test 之后想做 TD），M8/M9 可对调；M7b 时点 = Steam 发行决策日。

## 4. 验收判据映射（08 总览表行）

批⓪：AGENTS.md <150 行 + 全链接可解析；`lemon-tests` checks 数字不变（34,346）；ctest 4/4；回归 full 20/20；ccache 预热后干净重建时间实测下降（数字记 DevLog）。
批①：单测新增四件（烘焙 golden / Fx 数学 / hash 反例 / 多页寻址）+ ctest 4/4 + 回归 full 20/20 + bench-survivor ≥76.5 + svr-test 真人走查（暴击中文 Pop 弹跳、贴图血条、延迟白条）。
