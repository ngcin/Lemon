# 2026-10-11 M8 批① —— Light2D + BlobShadow2D 组件落位（渲染零消费）✅

## 事件

M8 批① 数据面全链落地：两组件登记（id 32/33 表尾追加）+ C# 镜像 + i18n 双语 + 单测/script-tests 双侧新用例 + **金回放三档一次性重录闭环**（ADR-019 后果与风险预定项）。渲染侧零消费（提取/合成归批②），引擎内核零系统改动。

## 落地物

| 域 | 文件 | 内容 |
|---|---|---|
| C++ 组件 | `Engine/Components/LightComponents.h`（新） | Light2D（**36B**，u32×8+u16×2；type/blend 枚举常量 + kLightEnabled）/ BlobShadow2D（16B）；布局冻结 static_assert ×2；镜像默认值坑注释（Type=Point/Color 白/Flags 启用 vs C# default 零值） |
| 登记 | `ComponentCatalog.cpp` | REGISTER_ED ×2 表尾（id 32/33，AudioSource 31 前序不动）；kLightTypeNames/kLightBlendNames 枚举名表；Enum/ColorHex/Range/Degree hint 全现成零新 FieldHint |
| C# SDK | `Interop/Components.cs` + `LayoutTables.cs` | 镜像 struct ×2（IComponent；坑注释三连）+ 探针行 ×2（顺序 = 注册序 1:1；ComponentTable.Bind ⇒ GameObject 门面自动可用） |
| i18n | `Editor/Strings/{zh-CN,en}/display.json` | Light2D 10 键 + BlobShadow2D 5 键双语（组件名 + 全字段） |
| 测试 | `tests/engine/LightTests.cpp`（新 TU，第八域） | 登记对账（id/尺寸/字段序）/ 默认值 + 生命周期 / .scene 往返（全字段 + flags=0 边缘 + 二次往返不动点）/ **哈希流入证**（加组件/加字段写均变哈希——重录口径机械证据；销毁两阶段提交后回空场基准） |
| 测试 | `tests/script/main.cpp` + `TestScript.cs` | TestLightSdk（LightProbeBehaviour typeId 26，Mark 1900..1919 段）：门面 Add/Get/Set/Remove 全链 + C++ emplace 默认 C# 读回对拍（坑反面证据）+ **SetComponent 即时整写引擎池对拍** + op3 移除生效 + 哈希入流；计数断言 32→34 四处（EcsTests ×2 / script main ×2）+ 热重载清单 26→27 |

## 实测（2026-10-11，全绿）

- 单测 **34,797**（+43）/ script-tests **1,905**（+71）/ ctest **4/4**
- **金回放三档重录 → mismatches=0**：sim st（`replay=PASS`）/ sim mt（`replay=PASS`）/ bench-script PASS；终态三元组 alive=8249 / created=16003 / destroyed=7754 与 m5b2 谱系历代档逐项一致（行为零漂移旁证，M5 批② 先例同款判读）
- vtable 59 不变 / 组件 id 至 33 / 系统 21 不变（零新槽零新系统）
- 回归 full 22 步**全有绿记录**：21 步直绿 + smoke-drag 回归内双红 → **隔离三连绿**（失败值 focusΔ=403115 vs 隔离态 0——负载下注入时序抖动，M7a §8 注入抖动家族、09 §9 归因口径，M7c 批⑩ 同款判读；本批零编辑器/渲染改动）
- 构建零警告（编译器面；见下"顺手清"）

## 开工核证发现（批文件 §4 三项 + 意外收获）

1. **maskSprite 不能用 ED_ASSET**：InspectorPanel 的 AssetRef+UInt32 分派硬编码转型 SpriteRenderer（"ED_ASSET 全目录唯一"注记，InspectorPanel.cpp:530-533）——若挂 AssetRef hint 会误转型。本批落 ED_TIP 裸值，**编辑器蒙版选择器接线登记为批② 项**（与 gizmo 同批，编辑器面归竖切批）。
2. **Light2D 实际 36B 非 40B**（批文件预填 ≤40B 目标内）：8×4B 槽 + u16×2 = 36 自然对齐；static_assert/Catalog/C# 注释三处同口径 36。
3. **Global 重复 WARN 语义**：纯渲染语义，本批零消费 = 仅头注声明，落码归批②（批文件 §4.3 预判成立）。
4. **SetComponent（Native.Write → NativeWrite）是即时 memcpy 非暂存命令**（ScriptHost.cpp:114-139）——ADR-019 M4"op2 整写"措辞按此理解：读走 readFn/get-or-create 直写池。TestLightSdk 断言结构据此定形（帧2 末池已是整写值）；昼夜补间"逐帧写"路径的时序语义以此为准。

## 顺手清（出口零警告判据暴露的 ccache 掩盖既存警告，7 站 8 条）

CS9196 ×3（TestScript ForEach `in` → `ref readonly` 对齐 IForEachSystem）/ CS8618（GameObject `_subscriptions` 补 `?`）/ unused-const ×2（ComponentCatalog kEdStatusEffects/kEdInventory——M5 段登记化后死表，删）/ CS8600（DomainManager `string? err`）/ CA2014（Batch.cs stackalloc 出帧循环，定长 8B 逐帧重用）。干净树 stash 实测 17 条警告 → 本批后编译器面 0。**遗留登记**：ld 重复库警告 ×2（lemon-engine/SDL3 链接行重复，HEAD 即有、本批未触链接面）——非编译器警告，登记候选池，随 M9 或下次 CMake 触碰时 dedup。

## 金回放口径说明

金档不随库（历代 worktree/本地录放），本批重录 = 新 schema（34 组件名入哈希流）下 record→replay 同构建自洽 + 终态三元组对旧档——ADR-015 M3 / M5 批② 先例同款。此后光照纯表现（渲染消费）不入金档，跨版本"零重录"口径恢复。

## 落账

- [批① 文件](../Plans/M8/2026-10-11-b1-components.md) Status: planned → done + 出口判据勾销 + 核证结论四条。
- [M8.md](../Plans/M8/M8.md) 批① 行 ✅ + Status 行；AGENTS.md 当前阶段/回归基线行同步。
- 本条目。
