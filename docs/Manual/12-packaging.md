# 出包与独立运行时

出包 = 把项目变成**解包即玩**目录：`lemon-game(.exe) + Vulkan loader + runtime/
（self-contained .NET）+ data/（资产 + 预烤音频/图集 + manifest.pkg.json + Fonts/）`。
目标机只需要一个 Vulkan 设备（GPU 驱动，或软件回退 lavapipe）——不装 .NET、不装引擎。

## 打包步骤

```sh
# 1) 编译项目脚本到 .lemon/bin（构建归编辑器/packager，lemon-game 只消费）
dotnet build path/to/MyGame/Game/Game.csproj -c Release -o path/to/MyGame/.lemon/bin

# 2) 出包（mac 机出 mac 包，win 机出 win 包——不支持交叉出包）
build/mac/tools/packager/lemon-packager \
  --project path/to/MyGame \
  --runtime build/mac \
  --out path/to/pkg \
  --force
```

| 参数 | 语义 |
|---|---|
| `--project` | 项目根（含 project.lemon） |
| `--runtime` | **构建树根**（从这里取 lemon-game 与动态库闭包） |
| `--out` | 出包目录（与 --project 互含会被拒——防自膨胀） |
| `--force` | 整删重建出包根 |
| `--rid` | 缺省按打包宿主平台 |

Windows 路径形态：`build\win\tools\packager\Release\lemon-packager.exe`。

## 自检与运行

成功标志（packager 尾行）：

```text
[lemon-packager] RESULT pkg-selfcheck: files=309 bytes=108MiB dylibs=1 baked=7
  manifest=38 atlasPages=1 atlasSprites=7 closureMiss=0 essentialMiss=0
  inventoryMiss=0 extra=0 => OK
```

四个 `*Miss` 与 `extra` **必须全 0**——非 0 = 包损坏（缺文件/多文件），勿分发。

```sh
path/to/pkg/lemon-game                        # 零参直玩（data/ 缺省路径端到端）
path/to/pkg/lemon-game --smoke --frames 900   # 机器自检
```

包内是烘焙产物：音频/图集/字体 `.baked` 现烤、sprite 源与 `.meta` 不入包、
结构化资产（场景/Prefab/rml）JSON 直拷。干净机解包即跑已验（mac 与 Windows）。

## 注意事项

- **HintPath**：模板 `Game.csproj` 引用 `Lemon.SDK.dll` 靠绝对路径HintPath——仓库
  模板若残留别人机器的路径，需重锚到本机构建产物（向导创建的项目自动锚，无此步）。
- **Windows 构建**：VS2022 x64 + .NET SDK 10 + Vulkan SDK（详规 =
  [Windows 构建指南](../EngineDesign/Windows-Build-Guide.md)，含 lavapipe 软件回退与
  离线网络节）。
- 压缩/加密/增量、安装器与 Steam depot、资源校验属发行段（后续版本）；当前 v1 =
  目录拷贝式分发。

> 设计依据：[ADR-016 独立运行时与最简出包](../ADR/ADR-016-Standalone-Runtime-And-Minimal-Packager.md)
> （布局/闭包重锚/自检判据全量）。
