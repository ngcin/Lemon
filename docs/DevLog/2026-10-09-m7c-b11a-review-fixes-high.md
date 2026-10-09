# M7c 批⑪ b11a — 引擎评审高危三项修复（review 2026-10-09 #H1/#H2/#H3）

2026-10-09 · [批⑪ 批文件](../Plans/M7c/2026-10-09-b11-engine-review-fixes.md) · 来源 [评审报告](../Reports/2026-10-09-engine-code-review.md) · D1–D5 全按建议追认（用户同日）

## 事件

b11a 当日清：评审确认的三处高危全修，D1 口径（红字 WARN + 默认值，句柄解引用 API 全族统一）落地。

- **#H1 失效实体句柄幽灵注入**：`ScriptHost.cpp` 三处 emplace 前加 `Alive` 闸——NativeWrite（vtable write，:98）、ApplyStructural case2（op2 AddComponent）、AttachBehaviour（op4/编辑器挂载共用）。失效 = 红字 WARN（实体 id + 组件名/typeId）+ 返回默认值/命令丢弃。DontDestroyOnLoad 原已有闸（:543，复核确认）。`NativeRead/NativeHas` 原本就 null 安全 = 全族口径一致。
- **#H2 BuildClipCache 弱解析器 terminate**：内联 nlohmann 解析整段删除，改调 `AnimAsset::ParseClipJson`（review 2026-10-02 #30 加固版）——同构纪律「解析下沉唯一实现」落地；本文件 nlohmann include 随之退役。语义收紧两处随源：坏 events 帧号越界拒整 clip（原静默跳过事件）；sheet 非 hex GUID 拒（原由 FindSprite miss 兜住）——均落「坏档红字跳过」契约。loopMode→bool 面（`loopMode != 0`，PingPong 档面按循环收，运行时权威在实体）。
- **#H3 UI 容器缓存 UAF**：新增 `Impl::InvalidateContainersUnder(docName, el)`——SetText/SetInnerRml 两 op 在 `SetInnerRML` 突变**前**按祖先链判定，命中追踪容器自身/祖先即整条 ContainerState 失效（container/tpl/proto/itemRoots 四组裸指针，惰性重建）。原失效面只有文档装载/重载/卸载，DOM 内突变不失效。

## 验证（机器面）

- 构建零警告（mac）。
- 单测 **34,721** OK（基线 34,716 + 5）：新增 `TestClipCacheBadArchive`（AssetsTests）——`"loop":1`/`"fps":"8"`/sheet 数字三类坏档红字跳过不 terminate，好 clip 登记 + spriteId 解析 + 帧事件携带断言。
- script-tests **1,830** OK（基线 1,818 + 12）：新增 `TestStaleHandleSdk` + C# 探针 `StaleHandleProbeBehaviour`（typeId 22，表尾注册；热重载清单断言 22→23 同步）。编排：帧1 Spawn target（真实句柄）→帧2 Destroy→帧4 stale 三连（SetComponent=write 闸 / AddComponent 值组件=op2 闸 / AddComponent 脚本=op4 闸）→帧5 健康标记后自毁；断言 1801–1804 标记序 + 终局 Velocity/ScriptBox/Transform2D 三视图零计数（无闸时幽灵恰好注进这三个池且永不清除）。C++ 二组：真实 stale id 直走 ops 命令流同验。三闸红字 WARN 实测命中（日志实证）。
- ctest 4/4。
- smoke-uirml `--validate` 脚本+无脚本双模式 **=> OK**（H3 前置失效对既有 470 帧编排零扰动：items=2/1、contract=1、d8/evict3 全绿）。

## 遗留与登记

- H3 专项 wipe 用例（SetText 命中容器 → 失效/响亮失败）随 **b11b 首项**补：wipe 连 `<ui-template>` 原型一并销毁，重建须专用夹具容器或文档重载，复用 cards 会扰动 contract==1/items==2 终局断言。
- ASAN（mac-san）复跑归批⑪ 收批统一门（H 类 + M1/M2）。
- 下一步 = b11b（坏数据 abort 面 5 项起）。

## 追记：b11a review 轮（同日）

两份独立只读评审（修复正确性 / 测试质量与回归风险）对 `f3d9f9b` 复核：主体结论「可合入、H2 零保留（41 个实档全量核对零误伤）、H1 三闸无第四漏网点」。发现并已修：

- **H3-1（medium，残 UAF）**：`InvalidateContainersUnder` 谓词漏「命中 `<ui-template>` 自身」形态——tpl 带 id 即可被 GetElementById 寻址，SetInnerRML 销毁其唯一元素子 proto = 克隆源悬挂，下次 SetItems `proto->Clone()` UAF。已补 `tpl == el` 独立判定（tpl 是容器直接子，不在祖先链上）。
- **H1-a（low）**：占位句柄（kPlaceholderBit 高位）经 vtable 直写入口被 `ToEntt` 截断高 32 位、可别名低位活实体绕过 Alive 闸。已加 `NativeHandleDereferenceable`（高 32 位非零拒），NativeWrite/Read/Has/IsAlive/DDOL 全族生效。
- **H1-b（low）**：三闸红字无去重，脚本逐帧重试 = 60+ 条/秒刷屏。已按 `g_batchStaleWarned` 先例改每 tick 一条（三旗随 tick 重置）。
- **H1-c（low）**：DDOL 原闸静默 return 0，与 D1「红字 + 默认值」口径不符（本 DevLog 原文「全族统一」表述过强）。已补红字。
- **测试卫生（F1–F4）**：探针事件号 1801–1804 与 SceneProbe 1800–1819 撞段 → 迁 **1871–1874**；Spawn 破产静默降级 → `Entity.Id != 0` 门控 Mark；TestClipCacheBadArchive 补**切片 cell 连号解析**与**越界 cell 悬空拒 clip**两分支；注释帧号订正（三连在帧3、健康标记帧4——帧3 Essential 应用 op1 + #17 提交后即死）。

**采纳评审建议**：ASAN（mac-san）门从收批提前至 **b11b**（H3 是 UAF 类、残洞恰是静态读出的——先跑门再继续）。b11b 首项 H3 专项 wipe 用例须覆盖「命中容器」与「命中 tpl」两种形态。

复验：构建零警告 / 单测 **34,723**（+2）/ script-tests **1,830** / ctest 4/4 / smoke-uirml `--validate` **OK**。
