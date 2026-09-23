# M5 批③：表现层 —— 计划与完工记录

Status: done（2026-09-23 勾销）

> 拆分自 M5 总计划（2026-09-23 收口），章节编号沿用原文件；同批事件与实测数字见 [DevLog 2026-09-23 条目](../../DevLog/2026-09-23-m5-b3-presentation.md)。

## 16. 批③：表现层 —— 现状盘点与设计决策（代码逐行核对，2026-09-23）

### 16.1 现状：管线断点在哪

| 环节 | 现状 | 锚点 |
|---|---|---|
| AnimatorSystem #13 | **仅 time 推进**：占位周期 1.0 回绕；curFrame 帧映射注释自认"接 clip 资产后补，M5" | `Systems.cpp:750` / `Systems.h:163` |
| Animator2D 组件 | 16B 布局已冻结，六字段齐备（clipId/time/speed/loop/playOnStart/curFrame），C# 镜像+探针已同步——**clipId 现语义"动画页基 spriteId"从未实现**（Inspector tip 自认"M5 clip 资产化"） | `RenderComponents.h:23` / `Components.cs:72` / `ComponentCatalog.cpp:62` |
| clip 数据资产通道 | 无。06 §2.2 clip2d=JSON 帧动画（AnimationEditor M6 产出）落空；批② D1 已把"独立数据资产通道"留给本批 | `06:56` / 本页 §11.2 D1 |
| 图集/切片 | M4 最小集：**每 PNG 一页一全幅 sprite**；`.meta` 只存 guid/type/hash（06 §2.1 的 `importer` 配置段未实现）；`AddSpriteAt` 已支持任意号+空洞（resize 哨兵） | `AssetGpuCache.cpp ImportSprite` / `AssetDatabase.cpp:145 SyncMeta` / `Atlas.cpp AddSpriteAt` |
| 素材包 | 06 §7 定 yami MIT 底包"直接采用"，至今**未引入一张**；来源在位：`~/GameProjects/yami-rpg-editor/Project/Templates/arpg-ts-chinese/Assets/`（Dungeon 表 hero 144×32 / monster 128×32 / boss 256×48；yami `.anim` 自带 hframes——hero 9 / monster 8，即 16px 格） | `06:131` |
| 编辑器 Play 桥先例 | SpawnFn（清障②）：EnterPlay 建 prefab 缓存、uint32 = GUID 低 32 位、错绑去重告警——**clip 表同款机制可直接复制** | `EditorContext.cpp:393/:542` |

**管线零重编号 + 零新组件 + 零布局改动**：Animator #13 占位即正式槽位（批②同款无插队成本）；
本批**不新增 ECS 组件、不改任何组件布局/注册表行数/字段序**——批②勘误的教训前置规避
（ComputeStateHash 对组件名无条件入哈希 → 新组件必致金档重录；本批机制上保证零重录，见 §18）。

### 16.2 设计决策（六条，实施时写进代码注释与文档）

**D1 clipId 语义 = clip 资产 GUID 低 32 位；引擎侧 ClipTable（纯 id 数据，零 GPU 依赖）**：
新增 `Engine/ECS/ClipTable.{h,cpp}`——`clipId → {fps, loop, frames[](spriteId)}`，
`World` 持有（`Clips()` 访问）。帧号 = 纯函数 `min(n-1, (u16)(time*fps))`（无逐帧累加
状态机 → 回放确定性）；表只 `Find` 不遍历（unordered_map 迭代序不进任何确定路径）。
映射约定与 prefabId 同款（03 schema uint32 恒定；M7 dense id 表同语义替换）。编辑器
EnterPlay 一次性建表（同 BuildPlayPrefabCache 快照语义：Play 世界 = 进 Play 时刻资产态）；
引擎测试直接填表。**clipId=0 或未命中 → 走 M2 旧路径逐位不变**（time 推进 + 周期 1.0
回绕）——既有场景零漂移 = 金回放零重录的机制保证。

**D2 clip `.clip` JSON 格式（06 §2.2 clip2d 落地；文本可 diff）**：

```json
{ "schemaVersion": 1, "name": "hero-walk", "fps": 8, "loop": true,
  "frames": [ {"sheet": "5bd31a7c10e9f2c8", "cell": 0}, {"sheet": "...", "cell": 1} ] }
```

帧引用 = **精灵表资产 GUID（hex）+ 切片序号**（行优先），不直接存 spriteId——切片身份
=(sheet guid, cell)，manifest 重排不断链（06"切片 GUID 稳定匹配"同精神）。EnterPlay
解析为 spriteId 数组入 ClipTable；sheet 缺失/越界/JSON 坏 → 红字跳过该 clip（实体回退
M2 路径不炸）。`AssetType::Clip`（扩展名 `.clip`）入资产库类型表（编辑器域，零哈希影响）。
`Animator2D.loop` 为**权威**（实体上热调参）；clip.loop 仅档面默认值。per-frame 时长/
PingPong/Random/Queue/帧事件打点归 M6 AnimationEditor（05 §7）。

**D3 最小图集/切片 = 单页网格切片（不打包、不 MaxRects）**：`.meta` 增 `importer` 段
（06 §2.1 原案格式）：

```json
{ "guid": "...", "type": "sprite", "hash": 0,
  "importer": { "slice": "grid", "cell": [16, 32], "frames": [9, 1] } }
```

- **frames 由作者显式声明**（yami `.anim` hframes/vframes 同款）——DB 层纯文件系统零解码
  即可在 Rescan 时分配**连号切片块**（base..base+cols*rows-1，manifest 记账只增不减）；
  GpuCache 保持 const-DB（读 entry 字段 AddSpriteAt，不参与记账）。
- ImportSprite 切片路径：解码后校验 `cols*cellW ≤ w && rows*cellH ≤ h`（不整除/越界 →
  红字**不切**，宁缺勿错——AddSpriteAt 越界是 assert）；**一页纹理不变**（bindless 槽
  上限 64 不受扰）；登记 = 全幅 sprite（引用兼容/拖入默认）+ 连号切片块。
- 切片块跨会话稳定（manifest 持久）→ 场景引用/clip 解析可复现。frames 增大 → 新块
  （旧块烧号，"只增不减"口径）；缩小 → 多余号留空洞。
- M6 才做：多表 MaxRects 打包、手动/自动切片 UI、切片编辑器、AnimationEditor 时间轴。

**D4 Animator 帧映射语义（M5 版）**：有 clip 时——`playOnStart=0` → 整体冻结（time/
curFrame/spriteId 不动；M5 无 Play() API，此位即暂停开关，文档明示）；否则
`time += speed*dt`（缩放 dt：timeScale=0 冻结动画，批① D5 同语义）→ loop=1 回绕
`period = n/fps`（time 有界）；loop=0 钳末帧（time 钳 total——M2"非 loop 无界增长"
随 clip 落地收口，**无 clip 路径不变**）；负 time 钳 0（speed 负值防御）→
`curFrame = min(n-1, time*fps)`、`sr.spriteId = frames[curFrame]`（SpriteRenderer 可缺 =
纯计时；写入幂等逐帧重写）。time/curFrame 保持 FIELD 行（入档入哈希，**不动字段位**——
动了即破既有场景哈希，违背零重录前提）。

**D5 素材包第一批 = yami Dungeon 精灵表（5–6 张）+ 配套 clip，入库 `Samples/Assets/yami-dungeon/`**：
06 §7 既定"直接采用"（MIT；`THIRD_PARTY.md` 登记来源/许可/再分发声明，07 移植红线合规）。
第一批：hero ×2、monster ×2、boss ×1（.anim hframes 核对切片格：hero 9×16px、
monster 8×16px、boss 以 `首领.anim` 实测为准）+ `hero-walk.clip`/`monster-walk.clip`
样例 + README（来源/许可/切片参数）。**bench-survivor 动画化用程序化 4 帧表自播种**
（temp 项目 hermetic，不依赖仓库相对路径——06 §7"bench 直接用默认素材"的全面接轨推
M6 模板打包）；yami 链路人工核验一次（临时项目 CLI 进 Play）记录 DevLog。

**D6 编辑器消费面 = clip 槽控件（FieldHint::ClipRef）**：`Animator2D.clipId` 行从 ED_TIP
占位升级为 clip 资产槽（下拉 AssetType::Clip 全列 + AssetBrowser 拖入 + 右键清空，
DrawSpriteSlot 同模式；值 = GUID 低 32 位，反查 entry 显示 relPath）。time 的
ED_RANGE(0,1) 放宽到 (0,100)（period 可 >1s）。**sprite 槽的切片直接引用 UI 归 M6**
（本批切片消费面 = clip；Inspector sprite 槽仍全幅）。C# 侧零改动（Animator2D 镜像
已同步，无新 SDK 面；模板用的 Lemon.Anim API 归 M6）。

### 16.3 验收判据（全部满足才勾销）

1. `ctest` 3/3 全绿，含新增引擎测试 ≥6 断言组（§17 T1 所列：帧号纯函数/loop 回绕/
   loop0 钳末帧/playOnStart 冻结/无 clip 回退逐位不变/无 SpriteRenderer 不炸/孪生哈希/
   clipId roundtrip）；
2. **金回放零重录**（本批机制保证：零新组件、零布局改动、无 clip 场景走 M2 旧算术）：
   m5b2 三档 `--replay` mismatches=0；若执行中被迫动 schema → 按 09 §7 重录并记勘误；
3. `--smoke-anim` 新冒烟（程序化 4 帧表 + grid meta + .clip → 进 Play → curFrame 推进 +
   sr.spriteId 落切片号区间）PASS，editor-regression full **12/12**；
4. bench-survivor 动画化（万怪 Animator2D）：3× PASS 判据不变（alive≥10000 &&
   frameAvg≤22.2ms && director 证据）+ 新增 animOk 证据项（采样 mob sr.spriteId ∈
   切片号区间）；Animator 系统 avg 进 09 §6.10 台账；
5. 素材包入库：`Samples/Assets/yami-dungeon/`（≥5 PNG + ≥2 .clip + README）+
   `THIRD_PARTY.md` 登记；yami 链路人工核验记录 DevLog；
6. 回写：06（§2.2 clip2d/§2.1 importer 段落地注记 + §5 打包注记 + §7 第一批记）、
   03（§3.3/§5 Animator 帧映射语义）、05（§5 clip 槽）、08、09（测试数/台账）、DevLog、
   本页勾销。

## 17. 任务分解（T1→T5 依序落地）

### T1 ClipTable（引擎）+ AnimatorSystem 帧映射 + 引擎测试 —— 约 1 天

**新文件** `Engine/ECS/ClipTable.h/.cpp`（CMake 源表追加）：`ClipDef{fps, loop,
vector<uint32_t> frames}`；`ClipTable::Add/Find/Clear/Count`（unordered_map，只 Find
不遍历）。`World.h` 持有 `ClipTable clips_` + `ClipTable& Clips()`。

**AnimatorSystem 重写**（`Systems.cpp:750`）：D1/D4 语义；**无 clip 回退 = M2 代码原样
保留**（先跑既有 `TestVerifyAnimatorAdvance` 作逐位锚）。`Systems.h:163` 注释更新
（"M2 最小"→"M5 帧映射"）。

**测试**（`tests/engine_tests.cpp`，计数 13127→+N）：`TestVerifyAnimatorFrameMapping`
（fps8×3 帧：tick 8→frame1、tick 22→frame2、tick 23 回绕 frame0；loop0 钳末帧+time 钳
total；playOnStart=0 冻结三态；speed 0.5 半速；clipId 未命中回退 M2；无 SpriteRenderer
不炸；孪生世界 300 tick StateHash 相等）；扩 `TestVerifyAnimatorAdvance`（未知 clipId
同回退）+ 孪生（若 archive 测试未覆盖 Animator2D → clipId roundtrip 断言并入）。

### T2 切片导入 + 切片号记账 + AssetType::Clip —— 约 1 天

**`AssetDatabase.h/.cpp`**：`AssetEntry` 增 `cellW/cellH/cols/rows/sliceBase/sliceCount`
（0 = 全幅）；`SyncMeta` 读 `importer.slice=grid` 段（坏值红字忽略）；Rescan 时
`cols>0 && sliceBase==0` → 分配连号块（nextSpriteId 推进）；manifest 条目扩
`"slice": {base, count}` 键（读侧缺键 = 兼容旧档全幅）；`FindClipByLowId`；`TypeOf`
`.clip` → `AssetType::Clip` + `AssetTypeName`。

**`AssetGpuCache.cpp ImportSprite`**：切片路径（网格校验 → 全幅 + 连号块 AddSpriteAt；
热重导入同格重切）；`Evict` 切片页同现有路径（号保留）。

**测试**：切片记账为编辑器域——引擎单测覆盖不了 GPU 面，机械验证归 `--smoke-anim`
（T4）+ asset-chain 冒烟（既有）。DB 层 meta 解析逻辑纯文件系统，若 engine_tests 已有
DB 用例则并入（无则记 09 观察项：DB 单测空白沿用现状）。

### T3 clip 解析 + EnterPlay 建表 + Inspector clip 槽 —— 约 0.75 天

**`EditorContext.{h,cpp}`**：`BuildPlayClipCache()`（D2 格式解析；sheet 缺失/越界/坏
JSON 红字跳过；低 32 位碰撞去重告警——BuildPlayPrefabCache 同款）；EnterPlay 在
BuildPlayPrefabCache 后调用（`playWorld_->Clips()`）。

**`ComponentRegistry.h`**：`FieldHint::ClipRef = 1u<<8`。**`ComponentCatalog.cpp`**：
kEdAnimator2D[0] ED_TIP → ED_CLIPREF + tip 更新；time ED_RANGE(0,1)→(0,100)；
curFrame ED_TIP。**`InspectorPanel.cpp`**：`DrawClipSlot`（DrawSpriteSlot 同模式；拖入
payload 增 clip kind）+ dispatch。

### T4 素材包第一批 + smoke-anim + bench-survivor 动画化 —— 约 0.75 天

**素材包**：`Samples/Assets/yami-dungeon/`（hero×2/monster×2/boss×1 PNG + grid meta
+ hero-walk/monster-walk .clip + README）；`THIRD_PARTY.md` 登记（MIT/来源路径/再分发
声明）；人工核验：临时项目 `--project` + 复制素材 + 进 Play 看动画（DevLog 记录）。

**`--smoke-anim`**（`EditorApp.cpp`）：SeedSmokeProject 扩展（程序化 128×32 四帧表 +
grid meta + .clip 固定 guid）+ 实体（SpriteRenderer+Animator2D）+ 进 Play 断言
（Clips().Count()==1、curFrame 曾 >0、sr.spriteId ∈ [sliceBase, sliceBase+4)）→
`smoke-anim: ... => OK`；`tools/editor-regression.sh` full 增 1 步（11→12）。

**bench-survivor 动画化**：temp 项目自播种程序化 4 帧表 + clip；BenchMob prefab 增
Animator2D（fps 10）→ 万怪动画进压测口径；RESULT 行增 `anim(...)` 证据（采样 mob
spriteId 落切片区间数）；PASS 门槛不变。

### T5 文档收口 + 全量验证 —— 约 0.25 天

06（§2.2 clip2d 落地 + §2.1 importer 段 + §5 注记"单页网格切片 M5 最小集，MaxRects M6"
+ §7 第一批素材记）、03（§3.3 Animator 行 + §5 帧映射语义小节）、05（§5 clip 槽）、
08（素材包里程碑注）、09（§6.10 动画化基线行 + 测试计数）、DevLog 批③条目、本页勾销。

## 18. 确定性与回放影响（批②教训前置规避）

- **零 schema 变化**：不新增组件、不改组件布局、不动注册表行数/字段序/kFieldRuntime
  位——ComputeStateHash 的组件名与字段集不变（批②勘误根因直接规避）。
- **无 clip 路径逐位同 M2**：同一算术（time += speed*dt；period 1.0 回绕）——即便
  金档场景含 Animator2D 实体也逐位一致；m5b2 场景核对（bench-sim/script 播种无
  Animator2D）双保险。
- 有 clip 路径：帧号纯函数 `time*fps` 截断（IEEE 确定）；回绕同款 while 减法；
  curFrame/sr.spriteId 逐帧重写幂等；无 RNG 消费（子流占用不变 Director=1/Spawn=2）。
- 切片 spriteId 跨会话稳定（manifest 连号块记账）→ 场景档/clip 解析可复现；bench
  temp 项目每跑重建但播种序固定 → 分配序确定。
- **预期：金回放零重录**（判据 2）；执行中若被迫动 schema → 09 §7 重录口径 + 本页勘误。

## 19. 风险与对策

| 风险 | 对策 |
|---|---|
| 切片记账破坏旧 manifest 兼容 | 读侧缺键 = 全幅默认（0）；写侧仅 sliceCount>0 补键；版本号不动（向后兼容追加）；`--final` 冷启动链冒烟覆盖旧档路径 |
| yami 表切片格核错（整除失败） | T4 先读 `*.anim` hframes 定格（hero 9/monster 8 已核；boss 实测）；网格校验失败红字不切（宁缺勿错，不 assert） |
| PNG 二进制入库体积失控 | 第一批限 5–6 张（每张 ≤40KB 量级）；音效/UI/tileset 明确排除（后续批） |
| 切片块与"pages_ 按 spriteId 升序"不变式 | AddSpriteAt 任意号+空洞已支持（Atlas.cpp resize 哨兵）；RebuildAll 按资产基号序 → 块内连号保持 |
| bench 万怪动画推高 frameAvg | 门槛不动（22.2ms），实测进 09 台账；超限优化路径预留（spriteId 条件写/并行），本批不预优化 |
| playOnStart=0 无恢复面（无 Play API） | M5 语义 = 暂停开关（文档明示 + Inspector tip）；Play/CrossFade 归 M6 模板 |
| ClipTable 悬挂（ExitPlay 后资产热改） | 表 = 进 Play 快照（同 prefab 缓存语义）；文档注记"Play 中改 clip 不生效" |

## 20. 验证命令（批③完工口径）

```bash
ctest --test-dir build/mac --output-on-failure                        # 单测＋布局探针＋脚本测试
# 金回放零重录（判据 2：零 schema 变化 + 无 clip 场景走 M2 旧算术 → 不重录）
build/mac/Samples/bench-sim/lemon-bench-sim --frames 3600 --replay build/goldens/m5b2-sim-mt.txt
build/mac/Samples/bench-sim/lemon-bench-sim --frames 3600 --threads 1 --replay build/goldens/m5b2-sim-st.txt
build/mac/Samples/bench-script/lemon-bench-script --frames 1800 --replay build/goldens/m5b2-script.txt
build/mac/Editor/lemon-editor --bench-survivor --frames 900           # 动画化后判据不变 + anim 证据，×3
build/mac/Editor/lemon-editor --project <tmp> --smoke-anim --frames 120   # 单跑新冒烟（回归内含）
tools/editor-regression.sh full build/mac                             # 12/12（新增 smoke-anim 步）
```

**完工记录（2026-09-23）**：全部判据满足——ctest 3/3（engine-tests **13145** 检查，
+18：TestVerifyAnimatorFrameMapping 九组断言——帧界 tick 8/22、回绕 tick 23、loop0
钳末帧、playOnStart=0 冻结三态、半速、负速钳 0、无 SpriteRenderer 不炸、未知
clipId 回退 M2、roundtrip、孪生世界 300 tick StateHash 相等；script-tests **1460
不变**，C# 零改动）；**判据 2 金回放零重录兑现**（m5b2 三档 `--replay`
mismatches=0——零新组件/零布局改动/零字段位变化 + 无 clip 路径逐位同 M2，批②勘误
教训的前置规避成立）；`--smoke-anim` PASS 且 editor-regression full **12/12**（新增
anim-chain 步；首轮 smoke-ui rename=0/1 与 final overlay sel=0 两步时序飘忽，直跑
×3/×2 全绿后重跑全量过——09 §8 已登记同族）；bench-survivor 动画化三跑
**58/67/66 fps PASS**（alive 10435、anim 切片命中 **10003/10003**、三跑逐位一致；
判据门槛 22.2ms 不动，Animator avg **0.128ms**）；素材包第一批入库
`Samples/Assets/yami-dungeon/`（5 PNG 80KB + 3 clip + README，帧数与 yami .anim
hframes 逐表核对；THIRD_PARTY.md 登记）+ yami 真素材链核验（--smoke-anim 拷入项目：
5 表切片全登记 + 4 clip 建表 + 双链断言 OK）。执行勘误：无（分解预判全部兑现——
含 D5 预声明的"bench 用程序化表自播种保 hermetic"）。

---
