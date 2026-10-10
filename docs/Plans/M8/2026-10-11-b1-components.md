# 批① —— Light2D + BlobShadow2D 组件落位（渲染零消费）

Status: planned（开工时分解核证 + 勾销；本文件为批⓪ 探查预填版，行号为 2026-10-11 快照）

> 主题：两组件数据面全链落位——登记即自动获得 .scene 序列化/Inspector/Add Component（反射驱动，零编辑器面板代码）；**渲染侧零消费**（提取/合成归批②，本批不碰 Renderer/）。设计权威 = [ADR-019](../../ADR/ADR-019-Light2D-Lightmap-Component.md) M2/M4。

## 1. 组件字段面（ADR-019 M2 定形）

```cpp
// Engine/Components/LightComponents.h（新文件，样板 AudioComponents.h:14-37）
// —— flags 常量 + 布局冻结 static_assert（POD、trivially_copyable）
struct Light2D {          // 目标 ≤40B（字段序对齐审一遍，AudioSource 25→32B 教训：ADR-015 M3 勘误）
    u32  type;            // 0=Global 1=Point 2=Spot 3=SpriteMask（Enum hint，kLightTypeNames）
    u32  blend;           // 0=Additive 1=Screen 2=Subtract（Enum hint，kLightBlendNames）
    u32  color;           // ColorHex hint
    f32  intensity;
    f32  range;           // 点/锥光半径（Range hint）
    f32  angle;           // 锥光张角（Degree hint）
    f32  innerRatio;      // 锥光内角占比 [0,1]（Range hint）
    u32  maskSprite;      // spriteId（SpriteMask 用；SpriteRenderer 同款字段 hint 先例）
    u16  flags;           // bit0 enabled
    u16  pad;
};
struct BlobShadow2D {     // ≤16B
    f32 radius; f32 aspect; f32 offsetY; f32 opacity;   // opacity [0,1]（Range hint）
};
```

- 字段终形开工时对 ComponentRegistry FieldType 表核证（u32 枚举/颜色、f32 数值、Range/Degree hint 全现成——ComponentRegistry.h:18-76）；不新增 FieldHint 即达成 Inspector 面板（验收点 InspectorPanel.cpp:1-4 头注）。

## 2. 触点清单（探查核实，六域）

**C++ 引擎（3 文件）**

| # | 文件 | 动作 | 锚 |
|---|---|---|---|
| 1 | `Engine/Components/LightComponents.h` | 新建：两组件 + static_assert + 头注（运行时状态严禁回写组件——快照确定性铁律，AudioComponents.h 同款） | 样板 :14-37 |
| 2 | `Engine/Components/ComponentCatalog.cpp` | kLight2D/kBlobShadow2D FieldMeta 表 + kEd* 编辑元数据 + 枚举名表（kLightTypeNames/kLightBlendNames）+ **REGISTER_ED ×2 表尾追加（id 32/33，只增不改序）** | AudioSource 先例 :135-153、:492 |
| 3 | `Engine/Systems/Systems.h` | 仅头注系统计数说明（若本批不加系统则不动；光照提取走 SceneExtractor 而非系统——ADR-019 M1） | — |

**序列化：零代码**（SceneArchive 元数据驱动，SceneArchive.cpp:164-295；字段名即 .scene JSON 键）。

**C# SDK（2 文件）**

| # | 文件 | 动作 | 锚 |
|---|---|---|---|
| 4 | `Engine/Scripting/dotnet/Lemon.SDK/Interop/Components.cs` | 镜像 struct ×2（`IComponent`；**镜像默认值坑注释**：C# default=0 vs C++ 默认值，AudioSource :362-376 同款） | :362-376 |
| 5 | `Engine/Scripting/dotnet/Lemon.SDK/Interop/LayoutTables.cs` | 探针行 ×2（**顺序 = 注册序 1:1**，AudioSource :292-300 先例） | :292-300 |

（`Lemon.Lighting` 门面归批③——本批组件可用 GameObject.AddComponent/GetComponent 直接读写，无需门面。vtable 零改动。）

**编辑器（2 文件，均为数据行非代码）**

| # | 文件 | 动作 | 锚 |
|---|---|---|---|
| 6 | `Editor/Strings/zh-CN/display.json` | `display.Light2D`/`display.Light2D.<field>`×8 + `display.BlobShadow2D`/`.field`×4（zh 词条） | AudioSource 先例 :175-181 |
| 7 | `Editor/Strings/en/display.json` | 同 key 英文词条 | :175-181 |

**测试（4–5 处）**

| # | 位置 | 动作 | 锚 |
|---|---|---|---|
| 8 | `tests/engine/EcsTests.cpp` | 计数 32→**34**（两处：:168、:219） | ADR-015 M3 同款四处先例 |
| 9 | `tests/script/main.cpp` | 计数同步（:93、:100） + 布局双向对照自动覆盖（TestLayoutAgainstRegistry :90-166 现成，两组件进表即测） | :90-166 |
| 10 | `tests/engine/` 新 TU（LightTests.cpp）或并入 EcsTests | 生命周期（emplace/remove/回写禁）、.scene 往返、Global 重复语义（首个生效+WARN——若语义落 C++ 侧；纯渲染语义则归批② smoke）、字段默认值；登记 TestMain.cpp | AudioTests.cpp:1098-1163 样板 |
| 11 | `tests/script/` TestScript.cs + main.cpp | TestLightSdk：AddComponent/字段读写/RemoveComponent 往返；GetExport + main 序列登记 | TestAudioSdk :700 样板 |

**金回放（必做项，非可选）**

| # | 动作 | 说明 |
|---|---|---|
| 12 | 三档金回放**一次性重录** → mismatches=0 | 组件注册入哈希流（StateHash.cpp:75，ADR-015 背景 4）；重录后跨版本口径恢复"零重录"。基线 = M7c 批⑪ worktree 录档口径 |

## 3. 出口判据（对照 M8.md §2 批① 行）

- [ ] 组件 id 32/33 尾插（ComponentCatalog 只增不改序）；C++/C# 布局 static_assert + 探针双向对照绿。
- [ ] .scene 往返单测绿；Inspector/Add Component 出现两组件（零编辑器代码——截图或 smoke 断言）。
- [ ] 计数六处同步（EcsTests :168/:219、script main :93/:100）；i18n 双语 key 加载 WARN=0。
- [ ] 单测/script-tests/ctest 4/4/回归 full 22 步全绿；构建零警告。
- [ ] **金回放三档重录闭环后 mismatches=0**。
- [ ] vtable 59 不变、系统 21 不变（本批零新槽零新系统）。

## 4. 开工前核证（首批动作）

1. FieldType 表对字段面逐项核证（u32 枚举/ColorHex/f32 Range/Degree）；maskSprite 字段 hint 用 SpriteRenderer 的 sprite 字段同款（核对其 FieldMeta 形态）。
2. 两组件进 `Interop/Components.cs` 的注册序 = LayoutTables 探针序 = ComponentCatalog REGISTER_ED 序（1:1 钉死）。
3. Global 重复 WARN 语义落位确认（渲染消费侧批②；本批若无处消费则纯注释语义，不写死代码）。
