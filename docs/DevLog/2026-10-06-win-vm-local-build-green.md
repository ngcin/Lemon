# 2026-10-06 Windows 本地 VM 构建链闭环：Parallels 首编全绿（ctest 4/4）

## 背景

M7a 批⑦ Windows 收口期，CI（windows-2025 runner）单轮 15-25 分钟、六连修复全靠
push 往返，迭代成本过高。改搭 Parallels Desktop（Windows 11 Enterprise LTSC 2024）
本地验证链：mac 改代码 → 共享目录即达 → VM 内 MSVC 构建 + ctest，分钟级闭环。

## 环境（guest）

| 组件 | 版本/位置 |
|---|---|
| VS2026 Community | `C:\Program Files\Microsoft Visual Studio\18\Community`（MSVC 14.51.36231，仅勾 C++ 桌面开发） |
| CMake 4.3.1-msvc1 / Ninja / ctest | VS 自带（支持 "Visual Studio 18 2026" 生成器） |
| Windows SDK | 10.0.26100 |
| .NET SDK 10.0.401 | `C:\dotnet`（zip 离线解压；机器级 `DOTNET_ROOT=C:\dotnet`——非标准位必须设，否则 CoreCLRHost 找不到 fxr） |
| Vulkan SDK 1.4.363.0 | 官方离线包静默装（`--accept-licenses --default-answer --confirm-command install`），系统 PATH 自动加 Bin |
| MinGit 2.56 | `C:\git`（CPM 依赖拉取/校验需要 git；与"VM 内不 clone"决策不冲突，纯构建工具） |

## 工作流（win-share 链路）

- 源码：mac 仓库目录共享为 `\\Mac\GameEngine`（用户配置），**mac 侧编辑即时生效**；
  构建产物落 VM 本地盘 `C:\lemon-build\win`（preset `win-share`，继承 win-ci 的
  VS2026 生成器，binaryDir 外置）。
- CPM 依赖：guest 直连 GitHub 会 0 字节卡死。链路 = mac 侧以
  `CPM_SOURCE_CACHE=~/lemon-vm-share/cpm-cache` 预热（一次 280s）→ guest
  robocopy 到本地 `C:\cpm-cache`（444MB，避免依赖编译走 prl_fs 共享 + 规避
  共享上 git 脏检查告警）。**注意 CPM_SOURCE_CACHE 是 CMake 缓存变量，首次
  configure 用错值会钉死在 CMakeCache 里，须清 build 目录重配**。
- 全程脚本通道：prlctl exec（SYSTEM 会话）+ base64/certutil 上传 .cmd（prlctl
  直传反斜杠/`%` 会被吞）。

## 本轮发现并修复的 Windows 问题（6 类）

均为 CI 此前未到达的深水区（前六轮只修到编译早段）：

1. **UNC 源码路径 × dotnet→MSBuild MSB1001**（`Engine/Scripting`、
   `Samples/anim-smoke`、`Samples/bench-script` 三处 CMakeLists）：源码在
   `\\Mac\...` 时 CMake 以 `//Mac/...` 传参，MSBuild 把开头 `/` 当开关。
   修：`file(TO_NATIVE_PATH)` 转项目路径（posix 原样）。
2. **`far` 变量名撞 windows.h 老宏**（tests/engine_tests.cpp 两处局部变量，
   minwindef.h 将 far/near 展开为空 → `Entity ` 语法错误）：改名 farEnt/farGem。
3. **`strtok_r` POSIX 专有**（tests/script/main.cpp）：MSVC 下 `#define strtok_r strtok_s`。
4. **指定初始化器顺序**（engine_tests.cpp 两处 PlayParams）：MSVC 严格执行
   标准的声明序要求（clang 宽松）。按 volume/pan/group/loop/fadeInSec 重排。
5. **`fs::path::c_str()` 传 ImGui**（EditorAppChrome.cpp 两处）：MSVC 下
   value_type=wchar_t。修：`.filename().string().c_str()`（临时串生存期覆盖
   完整表达式，安全）。
6. **HintPath 反斜杠 + 测试手拼 JSON 非法转义**：ProjectWizard/VsTemplateGen 三处
   `.string()`→`.generic_string()`（csproj 输出确定性正斜杠，MSBuild 接受）；
   recent-scenes 测试手写 JSON 嵌 Windows 绝对路径 = 非法转义会被 nlohmann 整档
   判坏（产品自写 JSON 有转义无此问题，纯测试侧问题），改 generic_string 拼写。

## 结果

- **VM 全量构建**：lemon-engine / lemon-editor / lemon-game（含 dotnet 运行时
  staging）/ lemon-packager / 四个 bench / anim-smoke / rhi-smoke / 全部测试目标 —— 全通过。
- **ctest -C Release：4/4**（engine-tests、imgui-isolation、script-tests、pkg-pe-selftest）。
  engine-tests 含 ECS/资产/表格/向导/音频各段约 7800 行测试全过；无 GPU 下未触发
  Vulkan 硬依赖（渲染段以静默/离屏路径通过）。
- mac 侧每步修复均回归构建 + ctest 4/4 后才进 VM 验证，双侧绿。

## 遗留 / 后续

- CI 上轮（d0bd8c6）状态未确认——本批测试期修复（far/strtok_r/初始化器序/
  HintPath/ImGui 路径）预计 CI 同样需要，推送后验证。
- W1-W5 真机清单：W1/W2/W4 已可在本 VM 满足；W3（lavapipe 软件 Vulkan）待配置；
  W5（GPU 真机验收）仍需物理机。
- 07《移植矩阵》§3.6 补记本地 VM 验证结论（批⑧ 收尾时一并）。
- mac CI 任务恢复 + 触发条件回退（批⑧）。
