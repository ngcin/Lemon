# Lemon Windows 构建指南（安装 → 构建 → 测试 → 运行 → 打包）

> 定位：Windows 平台的**完整使用级构建手册**——从裸机到出可分发游戏包，每步给
> 具体命令与预期结果。实测锚点：Windows 11 Enterprise LTSC 2024（10.0.26100）
> 物理兼容机上的 Parallels VM，2026-10-06 全流程走通（构建 ctest 4/4 + 出包真人
> 验收，[DevLog](../DevLog/2026-10-06-win-vm-local-build-green.md) /
> [W3/W4 验收](../DevLog/2026-10-06-win-vm-package-verify.md)）。
>
> **怎么用**：
> - §1–§4 = 裸机标准路径（约 40–60 分钟装机 + 首编约 15 分钟）；
> - §5–§7 = 运行/无 GPU/打包；
> - §8 = 网络受限环境（境内网络或离线机）；
> - §9 = mac 开发 + Windows VM 构建的共享工作流（可选）；
> - §10 = 故障排查表（**遇到报错先查这里**，全部条目为实测抓获）。
>
> 版本基线：VS2026（MSVC 14.51）· CMake 4.3.1 · .NET SDK 10.0.401 ·
> Vulkan SDK 1.4.363.0 · git 2.56。CI（GitHub Actions `win-build-test`，
> windows-2025 runner）与本指南同工具链（preset `win-ci`）。

---

## §1 系统要求

| 项 | 要求 | 实测参考 |
|---|---|---|
| 操作系统 | Windows 10 22H2+ / Windows 11 x64 | Win11 LTSC 2024（10.0.26100） |
| 磁盘 | ≥ 30 GB 空闲（工具链 ~14 GB + 构建树 ~8 GB + 出包 0.2 GB/个） | SSD 强烈建议 |
| 内存 | ≥ 8 GB（编译 + 编辑器运行） | 12 GB VM 实测宽裕 |
| CPU | 4 核+（全量构建并行编译） | 6 核 i5-9400F 全量 ~13 min |
| GPU | 可选（见 §6：无 GPU 用 lavapipe 软件 Vulkan） | — |
| 网络 | 首次构建需可达 github.com（拉 CPM 依赖，~450 MB）；离线方案见 §8 | — |

---

## §2 工具链安装（四件 + 一项系统设置）

> 全部装完约 14 GB。以下按依赖顺序给出；每小节末尾有**验收命令**，逐个确认再进下一步。

### 2.1 Visual Studio（C++ 工作负载）

- **版本**：Visual Studio 2026 Community（免费）或 2022 17.14+。CI 用 VS2026
  （windows-2025 runner 自 2026-06 起只装 VS2026），本地二者皆可。
- **安装组件**：勾选工作负载 **「使用 C++ 的桌面开发」** 一项即可——自带
  MSVC 编译器 + Windows 11 SDK + **CMake + Ninja + ctest**（无需单独装 CMake）。
  官网 <https://visualstudio.microsoft.com/> 下载 Community 安装器。
- 静默安装（可选，管理员 PowerShell）：
  ```powershell
  # 以官方 setup 引导为例；GUI 安装跳过此段
  vs_setup.exe --add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended --passive
  ```
- **验收**：
  ```powershell
  # vswhere 报出安装路径
  & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
  # MSVC 工具集存在（14.5x 即可）
  dir "${env:ProgramFiles}\Microsoft Visual Studio\18\Community\VC\Tools\MSVC"
  # VS 自带 CMake ≥ 4.2（"Visual Studio 18 2026" 生成器要求）
  & "${env:ProgramFiles}\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --version
  ```

### 2.2 .NET SDK 10（C# 脚本层，必需）

`LEMON_BUILD_SCRIPTING=ON`（默认）需要 .NET SDK 10；不装则 configure 直接
FATAL_ERROR。引擎运行期 CoreCLR 宿主也用它定位运行时。

**方式 A（推荐，标准位）**：官方安装器
<https://dotnet.microsoft.com/download/dotnet/10.0> → SDK x64 Installer（exe）。
装到 `C:\Program Files\dotnet` 并自动配置 PATH，**无需任何环境变量**。

**方式 B（zip 免安装，非标准位）**：
```powershell
# 解压到 C:\dotnet（或任意目录）
Expand-Archive dotnet-sdk-10.0.401-win-x64.zip C:\dotnet
# 非标准位必须设机器级 DOTNET_ROOT —— 否则测试/包体的 CoreCLR 宿主找不到 fxr
[Environment]::SetEnvironmentVariable('DOTNET_ROOT', 'C:\dotnet', 'Machine')
# PATH 追加（注意勿用 setx，PATH 超长会截断）
[Environment]::SetEnvironmentVariable('Path',
  [Environment]::GetEnvironmentVariable('Path','Machine').TrimEnd(';') + ';C:\dotnet', 'Machine')
```
> **DOTNET_ROOT 为什么必须**：CoreCLRHost 的 fxr 根候选链 =
> 显式参 → `LEMON_DOTNET_ROOT` → `DOTNET_ROOT` → `%ProgramFiles%\dotnet`。
> 方式 B 不设 `DOTNET_ROOT` 时链尾落空，`script-tests` 报
> `no usable dotnet fxr root`（§10-T1）。

- **验收**（新开终端）：`dotnet --version` → `10.0.4xx`。
- 离线注意：SDK zip 自带本平台 runtime pack，`dotnet publish -r win-x64
  --self-contained` **不需要网络**；仓库所有 csproj 零外部 NuGet 包，restore 离线可用。

### 2.3 Vulkan SDK（着色器编译 + 链接库 + 测试期 loader）

构建期需要它的 `glslangValidator.exe`（引擎 .vert/.frag → SPIR-V）与
`vulkan-1.lib`；测试期需要 loader。**不需要 GPU 也能完成全部构建**（§6）。

- 官网 <https://vulkan.lunarg.com/sdk/home> 下载 Windows x64 安装器。
- 静默安装（管理员 cmd，Qt Installer Framework 接口——**老版 `--mode unattended`
  参数已废弃**，错参会报 `Unknown option: mode`）：
  ```bat
  VulkanSDK-1.4.363.0-Installer.exe --accept-licenses --default-answer --confirm-command install
  ```
  默认装 `C:\VulkanSDK\1.4.363.0`，自动把 `Bin` 加入系统 PATH 并安装 Vulkan 运行时。
- **验收**：`where glslangValidator` 命中 SDK Bin；`vulkaninfoSDK --summary`
  能跑（无 GPU 时列表为空是正常的，只要求命令存在）。

### 2.4 git（CPM 依赖管理需要）

即使你不打算在 Windows 上 clone（比如走 §9 共享目录），**构建本身也需要 git**
——CPM 对每个依赖做 git 校验/拉取（`Could NOT find Git` = configure 必败，§10-T2）。

- 任意 Git for Windows ≥ 2.4x 均可；最小化选择 MinGit：
  <https://github.com/git-for-windows/git/releases> → `MinGit-2.xx.x-64-bit.zip`
  → 解压到 `C:\git` → PATH 追加 `C:\git\cmd`。
- **验收**（新开终端）：`git --version`。

### 2.5 系统设置：长路径

引擎模板/依赖路径较深，开启长路径支持（管理员 cmd）：
```bat
reg add "HKLM\SYSTEM\CurrentControlSet\Control\FileSystem" /v LongPathsEnabled /t REG_DWORD /d 1 /f
```
（改完对之后新起的进程生效；不重启也能用，建议顺手重启一次。）

---

## §3 获取源码与配置

### 3.1 clone

```bat
git clone git@github.com:ngcin/Lemon.git C:\Lemon
cd C:\Lemon
```

### 3.2 preset 体系（CMakePresets.json）

| preset | 用途 | 生成器 | binaryDir |
|---|---|---|---|
| `win` | 本地标准构建（Release+Debug 双配置） | VS2022 | `build/win` |
| `win-debug` | 显式 Debug 树 | VS2022 | `build/win-debug` |
| `win-ci` | CI 同款（**VS2026 生成器**） | VS2026 | `build/win` |
| `win-share` | 源码在 `\\Mac` 共享上的 VM 构建（§9） | VS2026 | `C:/lemon-build/win`（外置本地盘） |

> 装的是 VS2026 → 用 `win-ci`（VS2026 生成器需要 CMake ≥ 4.2，VS 自带 4.3 ✓）；
> 装的是 VS2022 → 用 `win`。两者产物等价。

### 3.3 configure + build + test（标准流程）

```bat
cd C:\Lemon
:: 用 VS2026 时把 cmake 换成全路径或加 PATH：
set CMAKE="C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

"%CMAKE%" --preset win-ci
"%CMAKE%" --build --preset win-ci

"%CMAKE%" -E chdir build\win ctest -C Release --output-on-failure
```

- **首次 configure**：CPM 从 github.com 拉全部依赖（SDL3 3.2.14 / VMA 3.4.0 /
  EnTT 3.15.0 / nlohmann_json 3.11.3 / Dear ImGui 1.92.9b-docking / stb /
  FreeType 2.14.3 / RmlUi 6.3），约 450 MB，网络正常 3–5 min（网络受限见 §8）。
- **全量构建**：约 800+ 编译单元，6 核 SSD 约 13 min；输出全部目标——
  `lemon-engine / lemon-editor / lemon-game / lemon-packager / 四个 bench /
  anim-smoke / rhi-smoke / lemon-tests / lemon-script-tests / packager-selftest`。
- **ctest 判据**：4/4 全过——
  `engine-tests`（约 34000 断言）· `imgui-isolation` · `script-tests`（CoreCLR
  桥）· `pkg-pe-selftest`（出包器 Windows 自检）。
- VS 生成器是**多配置**的，增量构建/调试切 Debug：`cmake --build --preset win-ci
  --config Debug`（或直接开 `build\win\Lemon.sln` 用 IDE 调试）。

### 3.4 构建产物在哪

| 产物 | 路径（binaryDir = build\win） |
|---|---|
| 编辑器 | `Editor\Release\lemon-editor.exe` |
| 独立游戏运行时 | `Engine\Entry\Release\lemon-game.exe`（旁带 `runtime\` 托管件） |
| 出包器 | `tools\packager\Release\lemon-packager.exe` |
| RHI 冒烟样本 | `Samples\rhi-smoke\Release\lemon-rhi-smoke.exe` |
| 测试 | `tests\Release\lemon-tests.exe`、`tests\script\Release\lemon-script-tests.exe` |

> C# 程序集（Lemon.SDK / Lemon.Entry / TestScript）由构建自动 `dotnet build`
> 到 `build\win\Scripting\dotnet\`；样本脚本程序集在各自样本构建子目录。

---

## §4 运行

```bat
cd build\win
:: 编辑器（Unity 风格 ECS 编辑器）
Editor\Release\lemon-editor.exe
:: 独立运行时直开项目（vs-survivor 模板）
Engine\Entry\Release\lemon-game.exe --project ..\..\Templates\vs-survivor --frames 900 --smoke
:: RHI 冒烟（不涉及脚本）
Samples\rhi-smoke\Release\lemon-rhi-smoke.exe
```

窗口 + Vulkan swapchain 需要**可用的 Vulkan 设备**——真机 GPU 驱动自带；
无 GPU 机器/VM 先做 §6。

---

## §5 无 GPU 环境：lavapipe 软件 Vulkan（VM/CI/核显 absent）

LunarG SDK **不含** lavapipe 组件（在线组件安装在境内网络基本不可用）；用
Mesa 官方 Windows 构建，两文件即装：

1. 下载：<https://github.com/pal1000/mesa-dist-win/releases> →
   `mesa3d-<ver>-release-msvc.7z`（任选新版本；7z 用 7-Zip 或系统 `tar -xf` 解开）。
2. 取 `x64\vulkan_lvp.dll` + `x64\lvp_icd.x86_64.json` 两件，**放到同一目录**
   （ICD json 的 `library_path` 是相对路径）。推荐直接放 SDK Bin：
   ```bat
   copy x64\vulkan_lvp.dll     C:\VulkanSDK\1.4.363.0\Bin\
   copy x64\lvp_icd.x86_64.json C:\VulkanSDK\1.4.363.0\Bin\
   ```
3. 注册 ICD（管理员）：
   ```bat
   reg add "HKLM\SOFTWARE\Khronos\Vulkan\Drivers" /v "C:\VulkanSDK\1.4.363.0\Bin\lvp_icd.x86_64.json" /t REG_DWORD /d 0 /f
   ```
4. 验收：
   ```bat
   vulkaninfoSDK --summary
   :: 期望：GPU0: apiVersion = 1.4.3xx   deviceName = llvmpipe (LLVM xx, 256 bits)
   ```
5. `set LP_NUM_THREADS=<n>`（可选）控制 llvmpipe 线程数，默认 = 核数−2。

> 软件 Vulkan 性能参考：2D 渲染链路（1280×720、图集精灵）在 6 核 CPU 上可跑，
> fps 达不到真机 GPU 水平——**用于功能验证，不代表性能基线**。
>
> **重要限制**：必须在**交互桌面会话**运行——服务会话/计划任务（Session 0）里
> GDI present 会失败（`vkQueuePresentKHR` 返回 -5，§10-T7）。CI runner 同为
> 非交互会话，所以 CI 只跑 ctest（不含 present 路径）。

---

## §6 打包出可分发游戏（lemon-packager）

出包 = 把一个 **Lemon 项目**（含 `project.lemon`、`Assets/`、`Scenes/`、
`Game/*.csproj`）变成"解包即玩"目录（ADR-016 布局：`lemon-game.exe + vulkan-1.dll
+ runtime/（self-contained .NET）+ data/（资产 + 预烤音频/图集 + manifest.pkg.json
+ Fonts/）`）。

### 6.1 准备项目夹具（以 vs-survivor 模板为例）

```bat
robocopy Templates\vs-survivor C:\pkgtest\game /E
:: 项目脚本工程编译到 .lemon\bin（"构建归编辑器/packager，lemon-game 只消费"）
dotnet build C:\pkgtest\game\Game\Game.csproj -c Release -o C:\pkgtest\game\.lemon\bin
```

> **HintPath 注意**：模板 `Game.csproj` 的 `Lemon.SDK` 引用靠 `<HintPath>`。
> 若模板文件残留了**别的机器的绝对路径**（本项目历史遗留：仓库模板曾被 mac
> 本机使用后回写），需重锚到本机构建产物：
> ```powershell
> (Get-Content C:\pkgtest\game\Game\Game.csproj -Raw) -replace '<HintPath>.*?</HintPath>',
>   '<HintPath>C:\Lemon\build\win\Scripting\dotnet\Lemon.SDK.dll</HintPath>' |
>   Set-Content C:\pkgtest\game\Game\Game.csproj -NoNewline
> ```
> （编辑器"新建项目向导"创建的项目会自动锚本机路径，无需此步。）

### 6.2 出包 + 自检

```bat
build\win\tools\packager\Release\lemon-packager.exe ^
  --project C:\pkgtest\game ^
  --runtime build\win ^
  --out C:\pkgtest\pkg ^
  --force
```

- `--runtime` = **构建树根**（从 `Engine\Entry\Release\` 取 lemon-game.exe 并收 dll 闭包）。
- `--rid` 缺省按打包宿主平台（Windows 机 = win-x64；**不支持交叉出包**，mac 包须 mac 机出）。
- `--force` 整删重建出包根；`--out` 与 `--project` 互含会被拒绝（防自膨胀/自毁）。
- 成功标志（自检行）：
  ```
  [lemon-packager] RESULT pkg-selfcheck: files=309 bytes=108MiB dylibs=1 baked=7
    manifest=38 atlasPages=1 atlasSprites=7 closureMiss=0 essentialMiss=0
    inventoryMiss=0 extra=0 => OK
  ```
  四个 `*Miss/extra` 必须全 0；非 0 = 包损坏（缺文件/多文件），勿分发。

### 6.3 运行包体

```bat
C:\pkgtest\pkg\lemon-game.exe            :: 直接玩（data/ 缺省路径端到端）
C:\pkgtest\pkg\lemon-game.exe --smoke --frames 900   :: 机器判据：game-smoke OK + fps=
```
包体自带 .NET 运行时（self-contained）与 Vulkan loader（`vulkan-1.dll`），
目标机**只需一个 Vulkan 设备**（GPU 驱动或 §5 lavapipe）。

---

## §7 网络受限 / 离线环境（境内网络实用节）

症状：CPM 或 dotnet 直连 github.com / dotnet CDN 长时间 0 字节卡死（连接建立、
无数据）。对策 = **用一台网络正常的机器预热缓存，再拷入**：

1. **CPM 依赖缓存**（约 450 MB）：网络正常机器上以 `CPM_SOURCE_CACHE` 指向空目录
   跑一次 configure（mac：`CPM_SOURCE_CACHE=~/cpm-cache cmake -S . -B /tmp/warm
   -G Ninja ...`；Windows 同理），随后把整个目录拷到目标机（如 `C:\cpm-cache`），
   构建**前**设环境变量：
   ```bat
   set CPM_SOURCE_CACHE=C:\cpm-cache
   ```
   > 首次 configure 时该变量会**钉死进 CMakeCache.txt**——如果第一次配错了值
   > （比如指向了网络路径），改环境变量无效，须删除整个 build 目录重配。
2. **.NET SDK**：用 §2.2 方式 B 的 zip；NuGet restore 全离线（零外部包）。
3. **Vulkan SDK / MinGit / mesa**：均为单文件离线安装器/zip，任意渠道转存。

---

## §8 mac 开发 + Windows VM 构建（共享目录工作流）

适用：主力在 mac 改代码，Windows 只做本地验证（分钟级往返，免 CI 排队）。
实测环境：Parallels Desktop + Win11 VM（mac 侧操作全程 `prlctl`）。

1. **共享源码**：Parallels 把 mac 仓库目录共享进 VM（VM 配置 → 共享 → 添加，
   或 `prlctl set "Windows 11" --shf-host-add lemonsrc --path <mac仓库路径> --enable`），
   guest 内即见 `\\Mac\lemonsrc\Lemon`。
2. **构建落本地盘**（重要）：源码在共享上可以，**构建树必须放 VM 本地盘**——
   用 `win-share` preset（binaryDir = `C:/lemon-build/win`）：
   ```bat
   set CPM_SOURCE_CACHE=C:\cpm-cache
   cmake -S \\Mac\lemonsrc\Lemon --preset win-share
   cmake --build C:\lemon-build\win --config Release
   ```
3. **CPM 缓存放本地**（`C:\cpm-cache`，见 §7）：依赖编译走共享目录既慢又会让
   git 脏检查误报（共享文件系统上 `git status` 把大量文件标 dirty——无害但刷屏）。
4. 增量迭代：mac 改文件 → guest 重跑 build 命令，秒级到几十秒级。
5. guest 里无浏览器下载需求一律走"mac 下载 → 丢进共享目录"（VM 内直连境外
   站点普遍不可用）。

---

## §9 CI 对应关系（GitHub Actions）

`.github/workflows/ci.yml` 的 `win-build-test` job（windows-2025 runner）与本手册
等价链路：SDK 缓存 + Qt IFW 静默装（§2.3 命令）→ `cmake --preset win-ci` →
`ctest -C Release`。runner 上 VS2026/CMake 4.x 已预装。mac job 暂时下线
（M7a 批⑦ 临时态，批⑧ 恢复）。

---

## §10 故障排查表（实测抓获，按命中频率排序）

| # | 症状 | 原因 | 处置 |
|---|---|---|---|
| T1 | `no usable dotnet fxr root`（script-tests 失败 / lemon-game 起不了 C#） | dotnet 装在非标准位且未设 `DOTNET_ROOT` | §2.2 设机器级 `DOTNET_ROOT`；或临时 `set LEMON_DOTNET_ROOT` |
| T2 | configure 报 `Could NOT find Git (missing: GIT_EXECUTABLE)` | 未装 git（CPM 必需） | §2.4 |
| T3 | configure 卡死/龟速在 `CPM: Adding package ...` | guest/受限网络直连 github | §7 预填充 `CPM_SOURCE_CACHE`（注意钉死重配） |
| T4 | `glslangValidator not found` | Vulkan SDK 未装 / PATH 无 Bin | §2.3 |
| T5 | `SDL_CreateWindow failed: Installed Vulkan doesn't implement the VK_KHR_surface` | 无 Vulkan ICD（无 GPU 且未装 lavapipe） | §5 |
| T6 | `MSBUILD : error MSB1001: 未知开关 //Mac/...` | 源码位于 UNC 共享路径，dotnet 步骤传参被 MSBuild 当开关 | 已修复（三处 CMakeLists `TO_NATIVE_PATH`）；旧版本绕法：把源码放本地盘 |
| T7 | 测试/包体在**任务计划/服务**里跑，`vkQueuePresentKHR` 返回 -5 崩溃 | Session 0 无交互桌面，GDI present 失败 | 桌面会话运行；自动化 present 需要 headless 方案（登记于 DevLog，未实现） |
| T8 | `dotnet` 步骤报 `hostpolicy.dll not found`（包体） | 旧版本 CoreCLRHost 探测 `libhostfxr.dll`（Windows 实为 `hostfxr.dll`） | 已修复（5cf1f75）；拉最新代码 |
| T9 | 出包自检 `closureMiss/essentialMiss/inventoryMiss/extra` 非 0 | 构建树被改残 / runtime 参数给错 | 干净重建后重出包；`--runtime` 指构建树根 |
| T10 | 项目 `Game.csproj` 编不过：`未能找到程序集 Lemon.SDK` | HintPath 指向别的机器路径 | §6.1 重锚到本机 `build\win\Scripting\dotnet\Lemon.SDK.dll` |
| T11 | `vulkaninfo` 命令不存在 | SDK 的工具名是 `vulkaninfoSDK.exe`（不是 vulkaninfo） | 用 `vulkaninfoSDK --summary` |
| T12 | MSVC 编译错 C3615/C2466/C7560 等（旧分支） | MSVC 严格性（constexpr math / 零长数组 / 指定初始化器顺序 / windows.h 宏污染） | 均已在主修复（c26e0bf）；新代码命中参照 DevLog 2026-10-05/06 系列条目 |

---

## 附：实测环境清单（问题复现对照用）

| 组件 | 版本 |
|---|---|
| OS | Windows 11 Enterprise LTSC 2024（10.0.26100）|
| Visual Studio | 2026 Community（MSVC 14.51.36231 · Win SDK 10.0.26100 · MSBuild 18.10） |
| CMake / Ninja / ctest | 4.3.1-msvc1（VS 自带） |
| .NET SDK | 10.0.401（zip → C:\dotnet + DOTNET_ROOT） |
| Vulkan SDK | 1.4.363.0（静默装） |
| git | MinGit 2.56.0.windows.1 |
| lavapipe | mesa-dist-win 26.2.4 release-msvc x64 |
| 首编时长 | ~13 min（6 核 + 源码在共享、构建/依赖在本地盘） |
| ctest | 4/4（engine-tests ~34000 断言） |
| 出包 | pkg-selfcheck OK（309 files / 108 MiB），桌面双击真人验收通过 |
