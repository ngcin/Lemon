# Lemon 是什么

Lemon 是一个**纯 2D 高性能游戏引擎**：C++20 + Vulkan 内核、C# 脚本、Unity 风格的 ECS 编辑器。目标品类是 ARPG、塔防、吸血鬼幸存者、增量游戏——**坚决不做 3D、不做重物理**。性能预算按割草场设计（万级实体 + 十万级弹幕/粒子 @60fps）。

## 能力面一览

- **渲染**：Vulkan 批渲染（图集合批 + bindless 零重绑）、粒子系统、位图文本、2D 光贴图光照（点/锥/蒙版光 + blob 阴影）、后处理链（bloom/色调曲线/冲击波）。
- **模拟**：ECS 世界（EnTT）多线程步进、**确定性回放**（录输入流 + 逐帧状态哈希——同输入必同结果，回归与查bug的底座）。
- **脚本**：C#（.NET 10）`LemonBehaviour` 组件模型 + `Lemon.SDK` 门面（22 个域文件：输入/场景/资产/音频/UI/补间/存档/配置表……），支持热重载。
- **编辑器**：SceneView/Hierarchy/Inspector/Assets 四面板、Play 模式（编辑器内试玩）、gizmo、双语界面（zh-CN/en）。
- **资产**：sprite 图集、prefab、场景 `.scene`、数据资产 `.asset`、烘焙 `.baked`、GUID 引用体系。
- **游戏系统**：多场景管理（同步 + 分帧异步加载）、RmlUi 界面、音频、存档、补间、配置表。
- **出包**：`lemon-packager` 打出独立运行时包（游戏 + 运行时，不依赖编辑器）。

## 平台与工具链

| 平台 | 图形栈 | 构建 |
|---|---|---|
| macOS | Vulkan（MoltenVK） | `cmake --preset mac && cmake --build --preset mac` |
| Windows | Vulkan（VS2022 x64） | `cmake --preset win && cmake --build --preset win` |

依赖经 CPM 拉取并缓存于 `~/.cache/Lemon-CPM`（SDL3、VMA、EnTT、FreeType）；编译缓存 ccache/sccache 自动探测挂载。首次构建约十分钟量级，此后增量。

## 五分钟跑起来

```sh
# 仓库根（Lemon/）执行；构建编辑器与运行时
cmake --preset mac && cmake --build --preset mac

# 打开编辑器（空项目即可浏览界面）
./build/mac/Editor/lemon-editor
```

编辑器里新建/打开项目、搭场景、按 Play 试玩的工作流见后续章节；要用现成范例，
可直接打开仓库自带的 `demo/svr-test`（吸血鬼幸存者工作项目，含昼夜循环光照）
或 `demo/ui-test`（RmlUi 界面测试场）。

游戏（不带编辑器）以独立运行时启动：

```sh
./build/mac/Engine/Entry/lemon-game --project demo/svr-test
```

常用调试旗标（`--validate` 验证层、`--frames N` 自动化帧数、`--vsync` 垂直同步）
见调试章节。

## 如何读本手册

手册只写**用法与语义**。更深的依据按分层存放，需要时再下钻：

- 设计基准（架构/渲染/ECS/脚本/编辑器/资产/移植/路线/测试）→ `../EngineDesign/` 各分册
- 推翻既定设计的决策记录 → `../ADR/`
- 实测数字、事故复盘、验收记录 → `../DevLog/`

> 更深的分册导航：入门链（[新建项目](02-getting-started.md) → [编辑器](03-editor.md) →
> [组件](04-components.md) → [脚本](05-scripting.md)）→ 工作流（[场景](06-scenes.md) →
> [资产](07-assets.md)）→ 表现（[2D 光照](08-lighting.md) → [FX](09-fx.md)）→
> 系统与发布（[界面](10-ui.md) → [音频](11-audio.md) → [出包](12-packaging.md) →
> [调试](13-debug.md)）。
