# ADR-009：Prowl2D 升格为理念/API 蓝本，C# 门面 Unity 命名级对齐

- 状态：**已采纳**（2026-09-18，用户拍板）
- 影响分册：00 / 01 / 02 / 03 / 04 / 05 / 07（本次修订，文档版本 v1.1 → v1.2）
- 关联：ADR-003（EnTT 唯一实体模型 + C# 门面不重复造 ECS）、ADR-004（边界仅双通道）、ADR-008（运行时 UI，开放）

## 1. 背景与动机

Prowl2D（自有前序路线，已归档）的目标原话：*"It aims to provide a seamless transition for developers familiar with Unity by maintaining a similar API while also following KISS and staying as small and customizable as possible. Ideally, Unity projects can port over with as little resistance as possible."*

M0 GO 之后、M1 动工之前重新对齐方向时，用户明确：

1. Lemon 的操作与理念目标**与 Prowl2D 一致**——核心是一个**高度类似 Unity 的 2D 引擎**，以 Prowl2D 为主要参考（含引擎设计思路）；
2. 其他引擎参考优点、性能优化方式与扩展内容；
3. 总体基调：**API 风格类跟 Prowl2D/Unity 类似，底层 async/await 这些以 C++ 等能提高性能的实现为主**。

此前文档将 Prowl2D 定位为"归档的教训来源"（07 §1.7："经验/结论任意复用；代码复用价值低"），与蓝本地位不符，需要升格并逐项吸收其已验证设计。

## 2. 决策

### D1 Prowl2D 升格为"理念/API 蓝本"（归档状态不变）

- Prowl2D = Lemon 的**理念、工作流与 C# API 风格蓝本**；归档状态不变（不新增功能、保持可编译，仓库/文档/坑记录保留）。
- 代码级供体维持不变：Luma（渲染 + C# 宿主，唯一 C++ 直接移植来源）、MoteurJV、yami、Editor-RPG2D、duality（07 移植矩阵）。Prowl2D 的价值在"已验证的设计与 API 面"，不在 C# 代码本身。

### D2 C# 门面命名级对齐 Unity（Prowl2D 同款目标，不追求移植兼容级）

- **GameObject 正名**：C# 门面与编辑器 UI 以 `GameObject` 为公开命名；底层即同一 EntityHandle（uint64），`Entity` 一词保留给档②③批量/性能层。
- **LemonBehaviour 基类** + Unity 原名生命周期：`Awake / OnEnable / Start / Update / LateUpdate / OnDisable / OnDestroy / OnTriggerEnter / OnTriggerExit / OnGameEvent`。
- **AddComponent/GetComponent 双路由**：单一 API 面（`where T : IComponent`），T 为数据 struct 走 EnTT 组件（档③）、T 为 LemonBehaviour 走 ScriptBox（档①）。
- `Scene.Instantiate/Destroy`、`FindWithTag`、`Transform` 视图（position/rotation/scale/parent/SetParent(worldPositionStays)/GetChild）、`Tag/Layer/CompareTag`。
- **不模拟**：协程（StartCoroutine/yield）、SendMessage/BroadcastMessage、Invoke、ScriptableObject、变步长 Update/FixedUpdate——差异以"对齐/不对齐清单 + 移植指南"显式管理（04 §3.2/§3.3）。
- 目标：Unity 开发者零文档上手；典型脚本移植 ≈ 改基类名 + using + Vector3→Vec2。

### D3 吸收 Prowl2D 六项已验证设计

| 项 | Prowl2D 源 | 去处 |
|---|---|---|
| SceneDispatcher 位掩码调度（未 override 生命周期零成本；dense 数组；每帧至多一次重建；[ExecutionOrder]） | `Prowl.Runtime/GameObject/SceneDispatcher.cs` | 04 §2.1（M3） |
| PrefabLink + PropertyOverride（链接+覆盖列表、嵌套 prefab、Apply/Revert/Break、运行时物化零开销） | `Prowl.Runtime/GameObject/PrefabLink.cs`、`Prowl.Editor/Prefabs/PrefabUtility{.Api,.Overrides}.cs` | 03 §2（M4 落地） |
| Undo 属性级双轨（属性快照 diff + action 命令；按 Guid 找回对象；连续操作合并） | `Prowl.Editor/Core/Undo.cs` | 05 §4（M4，替代全场景快照栈为主方案） |
| Camera2D 清单（像素完美 snap、指数阻尼跟随、边界钳制、编辑器相机自愈） | `Prowl.Runtime/Components/Camera2D.cs` | 02 §3.5（M1） |
| async/await 主线程上下文（协程替代） | `Prowl.Runtime/Tasks/MainThreadContext*` | 04 §3.1（M3） |
| UGUI 式运行时 UI（RectTransform/GameCanvas/Layout） | `Prowl.Runtime/Components/UI/` | ADR-008 第四候选（M5 决策，对照不拷） |

### D4 基调原则（写入 00 §2、04 头部）

**API 表面像 Unity/Prowl2D（上手体验），底层实现永远性能优先（C++ 内核、批量边界、热路径零托管分配）。**

表面相似绝不意味着机器相似：命名级对齐全部落在低频/门面路径；热路径（万怪/弹幕/粒子）始终走 C++ 原生系统与批量通道；性能预算红线（00 §4）一概不变。

## 3. 证据与依据

- spike-03 实测：C# 批量回调开销比 1.45–1.62×（可接受）；`MethodInfo.Invoke` 反射调度 0.057µs/call 为最差情况——命名级对齐的调度成本由 SceneDispatcher 位掩码方案消化（未 override 零成本、已 override 缓存委托直调）。
- Prowl2D M0（22 提交、1095 单测全绿）已验证：图集/合批/排序层地基、2D 编辑器工作区、S8–S11 交互清单。
- 命名/生命周期对齐不触碰任何 C++ 内核与 M1 渲染范围——影响集中在 M3（SDK API 面）与 M4（Inspector/Prefab/Undo）。

## 4. 后果

- **正面**：Unity/Prowl2D 开发者上手成本最小化；编辑器与脚本文档叙述统一；Prowl2D 已验证设计不流失。
- **代价**：04 §3 API 草案重写；"期望落差"须靠对齐/不对齐清单管理；~150 个手写导出的命名需按 Unity 风格定稿。
- **性能影响**：零（纯命名与门面）或正向（SceneDispatcher 调度、Prefab 运行时物化、Undo 属性级）。
