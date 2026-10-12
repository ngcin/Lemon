# 场景与多场景

## 场景文件（.scene）

JSON 格式（schema v2），大数组（地形/碰撞层）以行程编码（RLE）字符串嵌入——
保持文本可 diff 又不膨胀。实体段长这样：

```json
{ "schemaVersion": 2, "name": "Grass", "viewport": [1920, 1080], "tileSize": 32,
  "entities": [
    { "guid": "<prefab 引用>", "pos": [128, 64], "overrides": { "Chase.speed": 88 } },
    { "components": { "Transform2D": {}, "SpriteRenderer": { "spriteGuid": 9103... } },
      "scripts": [ { "class": "PlayerBehaviour" } ] } ] }
```

一般不手改——编辑器是正道；格式细节见设计分册。

## SceneManager（C# 换场门面）

| API | 语义 |
|---|---|
| `SceneManager.LoadScene(nameOrPath)` | 同步换场（Single）。寻址：项目相对路径 > 唯一文件名 stem；解析失败红字 |
| `SceneManager.LoadSceneAsync(nameOrPath)` | 异步换场，返回 `AsyncSceneLoad` 句柄 |
| `GetActiveScene()` / `GetSceneAt(i)` / `GetSceneByName/Path` | 查询（`Scene`：name/path/isLoaded/rootCount） |
| `sceneCount` | 已装载场景数 |
| `SceneManager.sceneLoaded / sceneUnloaded / activeSceneChanged` | 三事件（换场帧内同步直推） |
| `LemonBehaviour.DontDestroyOnLoad(gameObject)` | 标记跨场景幸存（仅根生效，根树整体幸存） |

**事件时序（换场帧协议）**：`LoadScene` 当帧照常跑完 → 下一帧换场窗口内
`sceneUnloaded → sceneLoaded`（新场脚本 Awake/OnEnable 后、同帧 Start/Update 前）
→ `activeSceneChanged`。三事件都早于新场脚本首帧 Update。

## LoadSceneAsync

分帧状态机（解析 → 构建 → 资产 → 门 → 激活），**加载期间旧场照常 tick**——要冻结
自己 `Time.Scale = 0`。主场景拆小 + 后台加载 = 标准用法：

```csharp
var op = SceneManager.LoadSceneAsync("Scenes/Grass.scene");
op.completed += _ => Console.WriteLine("到了");
// 或 await op;（域线程同步续跑，恢复点先于新场脚本同帧 Update）
```

| `AsyncSceneLoad` 成员 | 语义 |
|---|---|
| `progress` | 0..1 纯呈现量——**玩法逻辑禁挂 progress 分支**（分帧预算依赖、跨机器不确定），只许挂 `sceneLoaded/isDone/completed` |
| `isDone` | 完成态 |
| `allowSceneActivation` | 激活门（false = 预备完停 0.9 等门；true 后激活）——加载屏「读条到 90% 等玩家确认」的实现位 |
| `completed` | 恰推一次；订阅已终态 op = 立即同步触发 |

单在途：新请求（同步或异步）WARN 取代在途者，被取代 op 的 `completed` 不推。

## 跨场壳模式（模板同款）

- **GameFlow 种子**：入口场景放一个 `DontDestroyOnLoad` 的流程脚本（菜单/战斗/结算
  状态机），跨场幸存管全局节奏。
- **加载屏**：`LoadingScreen.Begin("Scenes/Grass.scene")` 风格——壳脚本先弹加载 UI、
  `Time.Scale = 0`，异步换场 `completed` 后恢复并清屏。
- **声明式 UI 随场装卸**：场景里的 UIDocument 换场即卸（声明式装载）；跨场要活的
  UI 挂在 DDOL 实体上。

## v1 边界

Single 唯一装载模式；`Additive` 红字未实现（预留）；无 buildIndex（按名/路径寻址）；
场景快照入存档尚未落（长档续局归后续版本）。

> 设计依据：[ADR-017 场景管理与 LoadScene](../ADR/ADR-017-Scene-Management-And-LoadScene.md)
> （API 面与 D1–D8 裁决全量）；SDK 本体 = `Lemon.SDK/SceneManager.cs`（注释即契约文档）。
