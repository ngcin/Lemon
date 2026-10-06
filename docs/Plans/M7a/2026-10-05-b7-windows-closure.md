# M7a 批⑦ Windows 真机收口 —— 首编清账 + win 出包 + CI runner（2026-10-05 开工）

Status: done 代码面 2026-10-05 + **review 轮 done 同日**（[review DevLog](../../DevLog/2026-10-05-b7-review-hardening.md)：mock 门（mac clang 真编 `_WIN32` 段）实抓 hostfxr char_t 全 API 面 + NOMINMAX 等**五实锤全修** + 阴性验证夹具重造；修后回归 full 19/19 复跑全绿 + ctest 4/4 + selftest 七例）；mac 门格全绿：回归 **full 19/19** + ctest **4/4**（pkg-pe-selftest 新第 4 项）+ 单测 34346 + smoke-uirml `font=Noto`（FreeType 切源把守）；A1–A8/B1–B4/C1 全落；**真机清单执行中**（W1–W7）：W1/W2/W3/W4 ✅ 2026-10-06（[VM 构建链](../../DevLog/2026-10-06-win-vm-local-build-green.md) + [W3 lavapipe/W4 出包验收](../../DevLog/2026-10-06-win-vm-package-verify.md)）；**W6 ✅ 2026-10-06**（①③④ 机器面 + ②拖入真人过；codepage 实抓五修 ⑬–⑰ 含拖入双 free/manifest UTF-8 两层闪退链，[W6 DevLog](../../DevLog/2026-10-06-w6-behavior-table-and-codepage-fixes.md)；②全过（FilePicker 手输 C:\\ 直达真人验过））；W5 真机 GPU 待物理机；**W7 ✅ CI run #10 = SUCCESS**（`63a842e` win-build-test 首绿 2026-10-06）；输入 = M7a.md §4 批⑦ / [07-Porting-Matrix](../../EngineDesign/07-Porting-Matrix.md) §3.5/§3.6 / [ADR-016](../../ADR/ADR-016-Standalone-Runtime-And-Minimal-Packager.md) D7（win-x64 publish））

## 0. 开工研读结论（2026-10-05，读码实证）

| # | 事实 | 对批⑦ 的含义 |
|---|---|---|
| 1 | `win` preset 已在 CMakePresets（VS2022 x64 多配置 + `condition` 限 Windows host，07 §3.6 伴生项半边已除）；`/utf-8 /permissive-` 已挂（根 CMakeLists:16-17）——全仓中文注释 MSVC 侧无虞 | 首编清账只差代码面 POSIX/宽字符阻断与依赖缺口 |
| 2 | 07 §3.6 五条阻断 M7 批⓪ 处置完（Process.h / PRINTF_FORMAT / std::strcmp / _popen 宏分支 / RenameReplace），但**MSVC 真编从未发生**；本机是 mac，MSVC 编译的机器门禁 = CI windows runner（真机窗口外的唯一 MSVC 面） | 本批 mac 侧"静态清账 + CI 落地"，真机批尾收口 |
| 3 | 本轮静态扫描新增 MSVC 编译阻断（全部 file:line 实证）：<br>① `Engine/Entry/GameEntry.cpp:100-103` `_WIN32` 分支用 `DWORD`/`GetModuleFileNameA` 但全 TU 无 `<windows.h>`（73-75 只 include 了 mach-o）<br>② `GameEntry.cpp:410-414` `setenv` POSIX-only；整块 = MoltenVK ICD 自举（mac 专属概念，win 侧 Vulkan ICD 走驱动注册表）<br>③ `Engine/Scripting/CoreCLRHost.cpp:91-92` `hostfxr_set_error_writer` 回调 lambda 签名 `const char*`——win 侧 `char_t`=`wchar_t` 编译不过<br>④ `CoreCLRHost.cpp:161-163` `initCmdLine` 的 `const char_t* argv[1] = {entryAssemblyPath}` 同源 char_t 分叉<br>⑤ `CoreCLRHost.cpp:72` dotnet 根默认链尾 `/usr/local/share/dotnet`（win 无默认根）<br>⑥ `tests/engine_tests.cpp:14` `<unistd.h>` + 8× `::getpid()`<br>⑦ `Samples/bench-script/main.cpp:62` `::setenv`<br>⑧ `Tools/packager/main.cpp:35` `<unistd.h>` 无守卫、`:74` `::popen`<br>⑨ `spike/03-csharp/main.cpp:12` `<dlfcn.h>` 无守卫（spike/04 的 vendored vulkan.h 反而有守卫） | ①–⑧ 全修；⑨ 走 win preset `LEMON_BUILD_SPIKES=OFF`（M0 mac 验收产物，win 面零验证价值，不为死代码维护 win 兼容——决策 D-7d） |
| 4 | RmlUi 依赖 FreeType：其 `cmake/Dependencies.cmake:9-18` 走**软依赖 + target 检查**（注释明说"consuming project can declare them by other means"）；mac 现走 brew 2.14.3（`Dependencies.cmake:82` 注记）——**win 无源**，CI/真机都要装 vcpkg 级外部件 | FreeType CPM 化（`VER-2-14-3` 锁 tag，SDL3 先例）三平台单源；接线 = CPM 在 RmlUi 之前 + freetype 自建 `Freetype::Freetype`（其 CMakeLists 自带 alias，缺失则补）——决策 D-7a |
| 5 | MSVC 默认 `/MD` → `vcruntime140.dll` 进依赖 → 干净 Win 机（无 VC redist）首跑即缺件 | 静态 CRT（CMP0091 + `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`）——决策 D-7c |
| 6 | win 闭包工具面：无 otool；dumpbin 要 vcvars 环境才在 PATH（用户裸 shell 出包会哑火） | 内置 PE import 表解析器（~120 行平台无关件，非条件编译段 mac 也编译）+ `lemon-packager-selftest` ctest 目标（内嵌合成 PE 断言）——决策 D-7b |
| 7 | Windows loader DLL 搜索序 exe 目录优先于系统位 → `vulkan-1.dll` 放包根零 PATH/rpath 注入；ICD 由驱动注册表发现（ADR-016 口径"ICD 归显卡驱动"）→ GameEntry 的 ICD 自举块 win 侧整体不适用 | packager win 分支无签名/无 rpath 重锚/无 ICD 三件套；vulkan-1.dll 来源序 `$VULKAN_SDK/Bin` → `System32`（loader 再分发合规，LunarG 条款允许） |
| 8 | `AssetIndex.cpp:379` / `AssetDatabase.cpp:583` relPath 已 `generic_string()` 正斜杠归一；`FileWatcher` = 快照轮询纯 std::filesystem；`FindFreetype` 之外无 dirent/kqueue/FSEvents | 编辑器/索引的 win 路径分隔符风险不存在，零处置 |
| 9 | 根 CMakeLists:55 `add_subdirectory(tools)` 而目录名是 `Tools/`——mac 大小写不敏感 FS 侥幸命中，NTFS 默认不敏感但非保证 | 顺手改精确大小写 |
| 10 | CI 现状：仅 `workflow_dispatch`（用户 2026-10-04 拍板暂停自动触发）；mac job = 构建 + ctest 三项（无 GPU 口径） | windows job 同口径同策略（编译 + ctest 逻辑面，dispatch-only）；win 是多配置生成器 → `ctest -C Release` |
| 11 | CoreCLRHost win 分支已半备（`LoadLibraryA`/`GetProcAddress`/hostfxr.dll 名/平铺形态）；`LoadLibraryA` = ACP 窄码路径——中文安装路径下 hostfxr 装载哑火（FileOps.cpp:71-79 widen 先例） | 宽字符深水区不扩 scope（决策 D-7e）：仅修 load-bearing 两处（LoadLibraryW + initCmdLine argv 宽化）；其余（manifest 编码/editor CJK 资产名）维持 07 §3.5"归真机首调"口径 |

## 1. 设计决策（本批内拍板，随 DevLog 落账）

| # | 决策 | 取舍 |
|---|---|---|
| D-7a | **FreeType CPM 化**（vs win 装 vcpkg/系统包） | 单源纪律（SDL3/EnTT 先例）+ 干净 Win 机 `cmake --preset win` 一步到位零外部件（Vulkan SDK 除外，本就是系统件口径）；代价 = mac 侧从 brew 2.14.3 切 CPM 同版（回归面有 smoke-uirml `font=Noto` 断言把守）；`FT_DISABLE_ZLIB/BZIP2/PNG/HARFBUZZ/BROTLI` 全关（RmlUi 只要光栅化核心，免 zlib/png 子依赖） |
| D-7b | **PE import 内置解析**（vs dumpbin 外呼） | dumpbin 依赖 vcvars 环境（裸 shell 不可用）= 批⑤ "依赖本机工具"教训的 win 版；解析器平台无条件编译 = mac 编译覆盖 + selftest ctest 双平台跑 |
| D-7c | **静态 CRT（/MT）**（vs /MD + vcruntime 入闭包） | 干净机判据直接要求零 VC redist；游戏 exe 静态 CRT 是行业惯例；闭包因此只剩 vulkan-1.dll 一件，自检面更小 |
| D-7d | **win preset `LEMON_BUILD_SPIKES=OFF`**（vs 给 spike 补 win 守卫） | spike = M0 mac 验收产物已冻结；补守卫是无验证价值的维护负担 |
| D-7e | **宽字符范围收窄**：CoreCLRHost 两处（LoadLibraryW、initCmdLine argv）+ packager/GameEntry 窄 argv 维持 | 全链 UTF-8→UTF-16 是 M8 级工程；本批只保"包在非 ASCII 安装路径能起 CoreCLR"这一真机判据直接依赖的面 |
| D-7f | **CI windows job = 编译 + ctest 逻辑面**（dispatch-only 同现行） | 无 GPU 口径同 mac；不抢批⑧ 的每日回归/基线门禁 scope；Vulkan SDK 静默安装实现期核实（choco 包或 LunarG 安装器直下） |

## 2. 任务分解

### A. 编译面清账（mac 侧完成 + CI windows 首跑机器验证）

| # | 件 | 改动 |
|---|---|---|
| A1 | `Engine/Entry/GameEntry.cpp` | `_WIN32` 段补 `<windows.h>`（93-108 ExeDir 作用域）；ICD 自举块（405-415）收窄 `#if defined(__APPLE__)` |
| A2 | `Engine/Scripting/CoreCLRHost.cpp` | set_error_writer 回调按 char_t 分叉；initCmdLine argv win 侧 UTF-8→UTF-16（fs::path 宽串）；DotnetRootCandidates 尾根 win = `%ProgramFiles%/dotnet` → `C:/Program Files/dotnet`；OpenLibrary win 侧 LoadLibraryW + UTF-8→UTF-16（复用 FileOps widen 语义，就地小实现不引依赖） |
| A3 | `tests/engine_tests.cpp` | `<unistd.h>` 删；8× `::getpid()` → `lemon::CurrentProcessId()`（`Core/Process.h`） |
| A4 | `Samples/bench-script/main.cpp` | `::setenv` → `_MSC_VER` 分支 `_putenv_s`（ProjectWizard LEMON_POPEN 同款过渡口径） |
| A5 | `CMakePresets.json` | `win`/`win-debug` 增 `LEMON_BUILD_SPIKES=OFF`（含注记性 displayName 或批文件落依据） |
| A6 | 根 `CMakeLists.txt` | MSVC 静态 CRT（CMP0091 NEW + `CMAKE_MSVC_RUNTIME_LIBRARY`，MSVC 分支内）；`add_subdirectory(tools)` → `Tools` |
| A7 | `cmake/Dependencies.cmake` | FreeType CPM（`VER-2-14-3`，FT_DISABLE_* 五连关）置于 RmlUi 之前；无 `Freetype::Freetype` target 则补 alias；THIRD_PARTY.md 登记 + 07 §2 矩阵加行 |
| A8 | `Tools/packager/main.cpp`（编译面） | `<unistd.h>` → `Engine/Core/Process.h`（packager 链 lemon-engine ✓）；popen/pclose → `_MSC_VER` 宏分支；`Run()` 命令引号 win 侧双引号（cmd.exe 不认单引号） |

### B. packager win 出包面（代码落 mac 侧，行为验证归真机）

| # | 件 | 改动 |
|---|---|---|
| B1 | `Tools/packager/PeImports.{h,cpp}` 新建 | PE import 表解析（平台无关，非条件编译段）：DOS `MZ`/e_lfanew → PE 签名 → COFF/OptionalHeader(PE32+ 0x20b) → DataDirectory[1] → section 表 RVA→文件偏移 → import descriptor 数组 → DLL 名列表；边界防御（尺寸上限/截断/坏魔数 → err 非空） |
| B2 | `Tools/packager/selftest.cpp` 新建 + CMake/ctest | 内嵌合成最小 PE（手工字节：单 import descriptor → `VULKAN-1.dll`）断言解析；坏档两例（截断/坏 e_lfanew）断言 err；注册 `lemon-packager-selftest`（mac/win 双跑 = CI 面也吃） |
| B3 | `Tools/packager/main.cpp` win 分支 | exe 名 `lemon-game.exe`；闭包 = PeImportDlls 递归（系统 DLL 白名单 + `api-ms-*`/`ext-ms-*` 豁免 → 包根/构建树/`$VULKAN_SDK/Bin`/System32 解析收件）；**无** install_name_tool/codesign/ICD 三段；`dotnet publish -r win-x64`（RID = 宿主默认 + `--rid` 覆盖参）；关键件表 win 名（`runtime/hostfxr.dll|hostpolicy.dll|coreclr.dll`，MoltenVK 两件剔除）；自检 win 口径（import 表对账替 otool） |
| B4 | `Engine/Entry/GameEntry.cpp`（运行面复核） | exe 旁 vulkan-1.dll 靠 loader 搜索序命中——零代码改动（B3 保证落位）；登记验证项进真机清单 |

### C. CI windows runner

| # | 件 | 改动 |
|---|---|---|
| C1 | `.github/workflows/ci.yml` | 增 `win-build-test` job（windows runner + VS2022 镜像内含）：Vulkan SDK 静默安装（实现期核实 choco `vulkan-sdk` 或 LunarG 安装器 `--accept-licenses --default` 直下；SDK 自带 glslangValidator = shader 编译依赖同源解决）→ setup-dotnet 10 → `cmake --preset win` → `cmake --build --preset win` → `ctest --test-dir build/win -C Release`；触发策略维持 dispatch-only（文件头注记已声明批⑦/⑧ 补齐） |

### D. 文档落账（随完工）

07 §3.5/§3.6 处置状态推进（阻断表新增条目勾销 + 伴生项范围收窄记录）；THIRD_PARTY.md FreeType 行；DevLog `2026-10-05-m7a-b7-windows-closure.md`；M7a.md Status/批次表；AGENTS.md 状态行。

## 3. 验证方案（三层门格）

| 层 | 门格 | 本会话可达 |
|---|---|---|
| mac | 编译面改动零行为变化：构建绿 + 回归 **full 19/19** + ctest（selftest 新增 → 4/4）+ 单测不降 + `--smoke-audio`/pkg-smoke 复跑（FreeType 切源重点盯 smoke-uirml `font=Noto` 位） | ✓ |
| CI | windows job 首跑：MSVC 全目标编 + ctest 逻辑面绿 | 代码就绪；**首跑待用户 push + dispatch**（暂停自动触发是用户拍板，不越权恢复） |
| 真机 | ①`cmake --preset win` 全目标首编零错 ②`ctest -C Release` 3+1 项 ③`lemon-game --project demo/svr-test --frames 300`（GPU/四屏）④packager win 出包 + 干净目录四屏 60fps ⑤07 §3.5 四项行为（CJK 标题/拖入路径/盘符直输/codepage） | **待机器窗口**——本批交付清单化（见 DevLog 尾节） |

## 4. 出口判据（对 M7a.md §3 批⑦ 行）

- 代码面：A/B/C 全落 + mac 门格全绿（含 FreeType 切源回归零降级）。
- CI 门禁：windows job 落地待首跑（首跑绿后 07 §3.6 "MSVC 实际编译"半句勾销、Gate C ② 语义收口）。
- 真机判据 **"干净 Win 机包跑通四屏 60fps"** = 机器窗口项，随窗口勾销；07 §3.5 表逐项过。
