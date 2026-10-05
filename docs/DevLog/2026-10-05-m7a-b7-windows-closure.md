# M7a 批⑦ Windows 真机收口 —— mac 侧代码面全落（2026-10-05）

事件：批⑦ 开工。mac 侧可完成面（编译清账 + FreeType 单源 + PE 闭包 + CI win job）
当日全落；MSVC 真编/真机出包/行为表归机器窗口（[批文件](../Plans/M7a/2026-10-05-b7-windows-closure.md) §3 三层门格）。

## 1. 开工研读要点（实证清单见批文件 §0）

- `win` preset 早已在（07 §3.6 伴生项半边已除），`/utf-8 /permissive-` 亦在——
  首编清账的真实缺口在**批④⑤ 新代码面带入的 POSIX/char_t 阻断**与 **FreeType
  win 无源**（RmlUi 软依赖 find_package，mac 走 brew）。
- 本机是 mac：**MSVC 编译的机器门禁 = CI windows runner**（批⑦ 落地），
  真机窗口只欠 GPU 运行面与行为表。
- AssetIndex/AssetDatabase relPath 已 `generic_string()` 归一、FileWatcher 是快照
  轮询——win 路径分隔符/文件监视两风险不存在，零处置。

## 2. 改动明细（批文件 §2 编号对齐）

| # | 件 | 内容 |
|---|---|---|
| A1 | `Engine/Entry/GameEntry.cpp` | `_WIN32` 段补 `<windows.h>`（DWORD/GetModuleFileNameA 此前无声明）；MoltenVK ICD 自举块收窄 `__APPLE__`（win 侧 ICD 走驱动注册表 + `setenv` POSIX-only） |
| A2 | `Engine/Scripting/CoreCLRHost.cpp` | ①`hostfxr_set_error_writer` 回调按 char_t 分叉（win=`wchar_t` + UTF-16→UTF-8 转写）；②`initCmdLine` argv win 侧 `fs::path` 宽化；③dotnet 根候选链尾补 `%ProgramFiles%\dotnet`（env 探测 + 字面回退）；④`LoadLibraryA`→`LoadLibraryW` + widen（中文安装路径哑火面） |
| A3 | `tests/engine_tests.cpp` | `<unistd.h>` 删、24× `::getpid()` → `lemon::CurrentProcessId()` |
| A4 | `Samples/bench-script/main.cpp` | `::setenv` → MSVC `_putenv_s` 分支 |
| A5 | `CMakePresets.json` | win preset `LEMON_BUILD_SPIKES=OFF`（spike/03 dlfcn 无守卫；M0 产物不为 win 面维护，D-7d） |
| A6 | 根 `CMakeLists.txt` | MSVC 静态 CRT（`CMP0091 NEW` + `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<Debug:Debug>`）——干净机零 VC redist，win 闭包预期只剩 vulkan-1.dll（D-7c） |
| A7 | `cmake/Dependencies.cmake` | **FreeType CPM `VER-2-14-3`** 三平台单源（`FT_DISABLE_ZLIB/BZIP2/PNG/HARFBUZZ/BROTLI` 五连关）+ `Freetype::Freetype ALIAS freetype-interface` 接线（RmlUi 软依赖 target 检查直取；CMake FindFreetype 的 `NOT TARGET` 守卫短路，零 cache 变量 hack）；mac 自 brew 2.14.3 切同版；THIRD_PARTY 行更新 + 07 §2 注记 |
| A8 | `Tools/packager/main.cpp` | `<unistd.h>`→`Core/Process.h`；popen→`LEMON_POPEN` 宏分支；`Quote()` win 双引号（cmd.exe 不认单引号）；`--rid` 参（缺省按宿主）；win 分支全量见 B3 |
| B1 | `Tools/packager/PeImports.{h,cpp}` | PE import 表解析器（平台无关、非条件编译段）：DOS→PE→COFF→PE32+/PE32 数据目录→节表 RVA 换算→descriptor 名表；防御边界（节数 ≤96 / 链 ≤512 / 名 ≤256 / 文件 ≤512MiB / 全读置边界检查） |
| B2 | `Tools/packager/selftest.cpp` + CMake | 合成最小 PE（双 descriptor）断言解析 + 坏档三例（截断/坏 e_lfanew/非 MZ）+ 无 import 合法形态；`lemon-packager-selftest` 目标 + `pkg-pe-selftest` ctest 注册（**mac/win 双跑**） |
| B3 | `Tools/packager/main.cpp` win 分支 | exe 定位三候选（VS 多配置 `Entry/<Config>/` → 平铺）；PE import 递归闭包（系统 DLL 白名单 + `api-ms-*`/`ext-ms-*` 通配；源候选链 构建树→`$VULKAN_SDK/Bin`→System32）；**无** install_name_tool/codesign/ICD 三段；publish `-r <rid>`；自检 win 口径（PE import 对账 + `hostfxr.dll/hostpolicy.dll/coreclr.dll` 关键件 + `vulkan-1.dll` 在场；MoltenVK 两件剔除） |
| C1 | `.github/workflows/ci.yml` | `win-build-test` job（windows-2025）：Vulkan SDK = LunarG 安装器 `--mode unattended --accept-all-licenses --default-answer yes` + `actions/cache`（整目录）+ 版本目录发现置 `VULKAN_SDK`/PATH（choco 包 2019 废弃不用，Mesa CI 直装先例）；setup-dotnet 10；`cmake --preset win`→build→`ctest -C Release`；**dispatch-only 同现行触发策略**（2026-10-04 用户拍板不越权恢复） |

决策点（批文件 §1）：D-7a FreeType CPM（vs vcpkg/系统装）= 干净机零外部件；
D-7b PE 内置解析（vs dumpbin）= 免 vcvars、CI 可跑；D-7c 静态 CRT；D-7d spikes
OFF；D-7e 宽字符仅保 CoreCLRHost 两处（全链 UTF-16 归 M8）；D-7f CI win = 编译
+ ctest 逻辑面。

## 3. mac 门格（全绿，2026-10-05）

- 构建 `cmake --build --preset mac` 全目标绿（249 步，FreeType 切源重编 RmlUi 链）；
  产物不再链 brew `libfreetype.dylib`（otool 实证）。
- **回归 full 19/19 首跑全绿**（含 pkg-smoke = packager mac 路径复验、game-smoke、
  smoke-uirml `font=Noto Sans SC` = CPM FreeType 光栅化断言把守）。
- ctest **4/4**（新增 `pkg-pe-selftest`；回归脚本 ctest 标签同步 3/3→4/4）。
- engine-tests **34346 checks**（本批不加引擎单测；新测试面在 pkg-pe-selftest）。

## 4. 真机清单（Windows 机器窗口待办——批⑦ 出口判据）

环境前置（一次性）：VS2022（C++ 桌面负载）+ Vulkan SDK（LunarG 安装器，装后
`VULKAN_SDK` 环境变量通常已设）+ .NET SDK 10 + 源码（git clone / 拷贝；CPM 首配
需网络或拷 `~/.cache/Lemon-CPM`）。

| # | 步骤 | 判据 |
|---|---|---|
| W1 | `cmake --preset win && cmake --build --preset win`（全目标首编；spikes 已 OFF） | 零编译错（07 §3.6 批⑦ 增补表 ①–⑪ 的 MSVC 实证） |
| W2 | `ctest --test-dir build/win -C Release --output-on-failure` | 4/4（engine-tests/imgui-isolation/script-tests/pkg-pe-selftest） |
| W3 | `build\win\Engine\Entry\Release\lemon-game.exe --project demo\svr-test --frames 300`（GPU 面） | 窗口出帧、退出码 0、日志无 VALIDATION-ERROR（可加 `--validate`） |
| W4 | `tools\packager\Release\lemon-packager.exe --project demo\svr-test --runtime build\win --out MyGame-win` | `RESULT pkg-selfcheck ... => OK`（闭包主体 = vulkan-1.dll；缺名红字按白名单补） |
| W5 | 干净目录跑 `MyGame-win\lemon-game.exe`（零参 = exe 旁 data/）四屏全流程 | 08 M7a 判据：**四屏 60fps**（fps≈vsync 锁 60 属正常，参考 mac 包 71/fps） |
| W6 | 07 §3.5 行为表四项：CJK 窗口标题（打开中文项目名）/资源管理器拖 PNG 入编辑器/FilePicker 手输 `C:\` 路径/中文路径项目 codepage（含 `demo\中文测试` 拷贝一档） | 逐项记录，异常归 07 §3.5 表回填 |
| W7 | GitHub 上 push 本批改动 → Actions 手动 dispatch `ci`（win-build-test 首跑） | MSVC 机器门禁首绿；观察 latest 下载 URL/安装目录发现/长路径三观察项 |

W1–W5 过 = 批⑦ 出口判据达成（08 M7a Win 半闭环）；W6/W7 过 = 07 §3.5/§3.6
"真机为准"半句勾销。

## 5. 登记与移交

- CI 触发策略维持 dispatch-only（用户 2026-10-04 拍板）；自动触发恢复归批⑧。
- Vulkan SDK CI 静默装的 `latest` URL 曾有 2025-02 断供记录（LunarG 论坛）——
  首跑若撞，改钉具体版本 URL（`.../sdk/download/<ver>/windows/VulkanSDK-<ver>-Installer.exe`）。
- 白名单漏名（真实 exe import 了清单外系统 DLL）= W4 红字，按名补
  `kSys` 集即可——响亮失败优于静默。
- `dotnet publish -r win-x64` 产物确定性（含 vulkan-1.dll 来源混 System32/SDK）
  归 M7a.md §8 既有登记项，不新增 scope。
