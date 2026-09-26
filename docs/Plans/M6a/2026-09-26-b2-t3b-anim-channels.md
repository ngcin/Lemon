# M6a 批② T3b：动画生产三通道 + LoopMode + 面板 QoL（用户实测反馈微批）

Status: done（2026-09-26 完工；T3 最小版当日上午收口，本微批同日由用户实测反馈驱动开工并完工）

> 拆分自 [批② 总文件](./2026-09-25-b2-content-production.md) B 线 T3 之后的追加微批。
> 动因（2026-09-26 用户实测）：T3 最小版"基本功能有了，不满足实际开发"——
> 三种真实素材形态缺两条半通道；新建没有走图源流程；无关闭按钮；帧列表竖排
> 对多帧素材不可用。

## 1. 设计决策（2026-09-26 用户对齐，四项）

1. **切片归属 = Unity 式 + 面板内快捷入口**：网格切片写图片 `.meta` importer 段
   （资产级、全项目复用、Rescan 连号块——既有基础设施）；切片配置 UI 内嵌在
   "从精灵表加帧"流程（选图 → 调网格 → Apply 写 meta → 网格预览拖框选 → 追加帧），
   不另开独立 SpriteEditor 窗口。Godot 式（region 存 clip）不取。
2. **范围 = P0 全量**：三通道 + 新建向导 + 胶片带 + 全面板关闭钮 + LoopMode 三模式
   （Once/Loop/PingPong）。per-frame 时长仍缓（动"帧号=纯函数"确定性根基，单独立批）。
3. **资源目录 = 约定不强制**：引擎零硬编码目录；新建落点跟随当前浏览目录（Unity
   机制同款——.anim 保存对话框默认落 Project 当前目录）；项目向导可选生成推荐骨架
   （anims/sprites/prefabs）；按角色组织（monster/Walking/）与按类型组织均合法
   （guid 随 .meta 走）。AssetBrowser 加类型过滤器替代目录纪律。
4. **新建 .clip 默认落点 = AssetBrowser 当前浏览目录**（向导内可改路径）。

## 2. 现状缺口（2026-09-26 核对）

| 形态 | 用户素材 | 缺口 |
|---|---|---|
| A 文件夹多单图 | svr-test `Assets/monster/Walking/`（18 张零填充序号，meta 无 importer = 未切片） | **运行时不吃**：BuildPlayClipCache 要求 sheet 已切片（SliceSpriteId 对未切片恒 0 = 悬空） |
| B 单文件横条 | `dungeon_boss_1.png` 8×1 @32×48（meta 手写） | 切片数据模型已有，**无切片 UI** |
| C 大网格多段 | 8×8=64 格，1–16 idle / 17–32 run | 切片同 B + 需"从表框选区间生成帧"（Godot SpriteFrames 交互） |

## 3. 任务分解

- **T3b-1 引擎：整图引用放开**：`BuildPlayClipCache` 帧 {sheet, cell:0} 且 sheet
  未切片 → 解析为本体 spriteId（约 10 行）；AnimationPanel `FrameResolvable`/sheet
  下拉同步（未切片显示"（整图）"，cell 锁 0）；零重录（clip 解析在 EnterPlay 快照
  不入 StateHash，基准场 clip 不变）。
- **T3b-2 引擎：LoopMode 三模式**：`Animator2D.loop` 字节语义扩为 0=Once/1=Loop/
  2=PingPong（**布局冻结不破**——0/1 旧值逐位不变，`_pad` 不动）；AnimatorSystem
  pingpong 帧映射 = 纯函数（period=2(n−1)，pos<n?pos:period−pos），time 回绕同周期；
  Inspector `ED_BOOL8` → Enum 下拉；ClipEdit 档面 loopMode（schema 可选字段，仅
  PingPong 落盘加 `"loopMode": 2`，legacy `loop` bool 恒写——**旧档 no-edit 往返
  逐字节不变**）；语义：档面 = 创建默认，实体 Inspector = 运行时权威（现状权威
  位置不变）；C# SDK `Lemon.Anim` 加 LoopMode 枚举 + Play 重载。M2 无 clip 路径
  loop=2 同 1（truthy，金档锚不动）。
- **T3b-3 core：`AssetDatabase::SetGridSlice`**：写 .meta importer 段（读改写原子），
  0 值 = 撤销切片；调用方 Rescan 生效（frames 增大烧号语义已备）；engine-tests。
- **T3b-4 编辑器：从精灵表加帧弹窗**：选图（下拉/全部 sprite）→ 未切片则网格配置
  （cols×rows 与 cell 尺寸双输入法）→ Apply（SetGridSlice + Rescan）→ 缩略图网格
  预览 + 拖框选（ImDrawList 网格线 + 鼠标矩形 → 行优先区间）→ "追加 N 帧 / 替换
  帧表"；AnimationPanel 底部 + 向导共用。
- **T3b-5/6 编辑器：文件夹→动画 + 新建向导三通道**：AssetBrowser 文件夹单元格
  右键"从此文件夹创建动画…"；向导三 Tab（从文件夹/从精灵表/空白）——文件夹页
  = 目录下拉 + 图列表（文件名序）计数 + name（默认文件夹名）/fps/loopMode + 落点
  （默认当前浏览目录可改）；精灵表页复用 T3b-4；空白页 = 现有空 clip。
- **T3b-7 编辑器：胶片带帧列表**：竖排行改横排 wrap 缩略图带（帧号角标/悬停高亮/
  拖拽重排（payload=索引）/Ctrl 多选 + 删除选中）；单击选中帧 → 底部单帧编辑行
  （sheet 下拉 + cell + 删除）；预览区照旧。
- **T3b-8 编辑器：全面板关闭按钮**：OnGui `Begin(name, &open)` → docked tab 出 ×；
  关闭走 `EditorApp::ClosePanel(name)` 同步 PanelRegistry（Window 菜单可重开）；
  核心 7 + Animation 全量。
- **T3b-9 编辑器：类型过滤器 + 向导骨架**：AssetBrowser 头部按钮组（全部/图/动画/
  prefab/表/script）；新建项目向导加"推荐目录骨架"勾选（blank 默认开：anims/
  sprites/prefabs）。
- **T3b-10 验证 + 记录**：真实素材验收（svr-test Walking 18 帧一键建、boss 表
  框选建段、64 格模拟、pingpong 帧序断言）+ 双 preset ctest + engine-tests 增量 +
  回归 14/14 + 本文件勾销 + AGENTS.md 状态行；05/06/09 文档注记归 T6 一并回写。

## 4. 风险与对策

| 风险 | 对策 |
|---|---|
| loop 语义扩值破旧档/C# bool 写路径 | 0/1 逐位不变；C# `Loop=(byte)1/0` 兼容；pingpong 仅经新 UI/枚举写入 |
| pingpong 帧映射破坏确定性 | 纯函数（无状态机）；engine-tests 新 pingpong 帧序断言；基准场无 loop=2 |
| 整图引用放开引入坏帧 | 判据收窄为 cell==0 且 spriteId≠0；面板/运行时同判据同文案 |
| 切片 Apply 后连号块烧号（frames 增大） | 既有 Rescan 语义（新块烧号红字）；弹窗 Apply 后重找 entry |
| 多单图 = 每张一纹理页（内存/绑定开销） | v1 正确性优先；图集打包归 M7 计划，此处注记不阻塞 |
| 拖框选与 ImGui 窗口拖拽冲突 | 框选仅限图像 rect 内（IsItemHovered 门控 + 鼠标按住时 CaptureMouseFromApp） |

## 5. 完工记录（2026-09-26）

**交付**（全部按 §3 分解落位）：

- **T3b-1 整图引用**：`BuildPlayClipCache` 未切片 sheet + cell 0 → 本体 spriteId；
  面板 `FrameResolvable`/`DrawCellImage`（全幅 UV）/sheet 下拉（未切片标"整图"、
  cell 锁 0）同判据；smoke-anim 增 anim-whole.clip 断言（裁决行 `whole=%s`）。
- **T3b-2 LoopMode**：`Animator2D.loop` 字节语义扩 0/1/2（布局冻结/C# bool 兼容）；
  AnimatorSystem pingpong 纯函数帧映射（period=2(n−1)）+ time 同周期有界；
  Inspector `ED_BOOL8`→Enum 下拉（kLoopModeNames）；ClipEdit `loopMode`（可选
  字段仅 PingPong 落盘加 `"loopMode": 2`——legacy `loop` bool 恒写，旧档 no-edit
  往返逐字节不变，golden 测试锁字段策略）；SDK `Lemon.Anim.LoopMode` 枚举 +
  Play 重载（bool 版转发，源码兼容）；预览三态同映射。
- **T3b-3**：`AssetDatabase::SetGridSlice`（meta importer 读改写原子；全零 = 撤销
  切片；guid/type/hash 保原值；内存 entry 同步刷新）。
- **T3b-4 从精灵表弹窗**：选图（全部 sprite）→ 未切片就地配网格（格数/像素双
  输入法 + 图算 cell 预估 + Apply = SetGridSlice+Rescan 本帧早退 / 整图作 1 帧）→
  已切片 = 缩略图网格线 + 拖框选（clamp 格命中 + 高亮 + 计数）→ 追加/替换帧或
  向导"选定区间"。
- **T3b-5/6 向导**：三 Tab（文件夹/精灵表/空白）；文件夹页 = 目录下拉 + 文件名序
  图片计数 + 前 12 张预览 + 自动命名；AssetBrowser 文件夹右键"从此文件夹创建
  动画…"（`OpenAnimationCreateFromFolder`）；落点默认 = AssetBrowser 当前目录。
- **T3b-7 胶片带**：横排 wrap 缩略图带（帧号角标/选中描边/Ctrl 多选/拖拽重排/
  双击预览跳帧/删除选中）+ 选中帧编辑行（sheet 下拉含整图 + cell + 删除 + 拖
  Assets 精灵换 sheet）。
- **T3b-8 关闭按钮**：8 面板 `Begin(name, &open)` → tab ×；`EditorApp::ClosePanel`
  同步 PanelRegistry（Window 菜单可重开）。
- **T3b-9**：AssetBrowser 类型过滤按钮组（全部/图/动画/Prefab/表/脚本）；blank
  模板骨架 + `Assets/anims`、`Assets/sprites`（约定不强制）。
- **停靠位修订**：Animation 停靠中央区会抢 Scene 标签页（overlay 像素断言实测
  抓到）→ 改 **bottomId**（Unity 动画窗口同款；宽矮窗配横排胶片带）。

**验证记录（2026-09-26）**：

| 判据 | 结果 |
|---|---|
| ctest 双 preset | 3/3 ×2；engine-tests **33443**（+18：pingpong 帧序 3 + SetGridSlice 8 + loopMode 5 + 既有 golden 不变）；Debug 同数 |
| smoke-anim | `edit(rt=YES cache=YES whole=YES)` + byte-exact=YES + **面板本体无头覆盖**（编辑链后 `OpenAnimationEditor` 全程跑 OnGui——装载/胶片带/预览路径，ImGui 错误计数归零兜底） |
| 真素材验收（用户工程拷贝） | svr-test `monster/Walking/` 18 张单图 → Walking.clip（整图引用）→ Play 快照 **18 帧 @8fps**；boss 表框选段 4 帧 @6fps——通道 A/C 端到端实证 |
| editor-regression full | **14/14**（首跑即全绿） |
| 金回放零重录 | m5b2 三档（sim-mt/st + script）现录现放 **mismatches=0**——AnimatorSystem 改动 0/1 路径逐位等价 + 基准场零 Animator2D 实例（先例三第三次应用） |
| bench-survivor | 1000 帧窗 **PASS**：fps=73（≥45）、anim 10000/10000 切片命中、hazard 144823、fx 饱和。**口径注记**：700 帧窗 anim=9782<10000 FAIL 系**基线既有现象**（stash 对照逐位一致——怪数涨满需 ~1000 帧；非 T3b 回归）；1200 帧窗玩家磨死 hp<0 同理属帧窗敏感 |
| golden 格式 | hero-walk no-edit 往返逐字节不变（engine-tests 锁）；loopMode 仅 PingPong 落盘 |

**余项**：05 §3（Animation 停靠位/解冻注记更新）· 04（Lemon.Anim.LoopMode）·
09 §6.8（先例三第三次应用 + 面板无头覆盖手法）文档回写归 T6；per-frame 时长
仍为独立后续批（动帧映射纯函数根基）。
