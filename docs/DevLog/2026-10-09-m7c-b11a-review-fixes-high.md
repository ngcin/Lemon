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
