# M7a 批⑦ review 轮 —— mock 门 + 五实锤全修（2026-10-05，用户令 review 后）

事件：批⑦ 代码面收口后、push/CI 前的审查轮。方法 = diff 全读 + **mock 门**
（mac clang 上真编 `_WIN32` 分支——CI 前抓掉 mac 编不到的错）。修后门格：
构建绿 + selftest 七例 OK + ctest 4/4 + **回归 full 19/19 复跑全绿**（rid guard
后 pkg-smoke 零变化）。

## 1. mock 门（会话级工具，未入仓；配方留档复用）

win 分支代码在 mac 上 `#ifdef` 掉 = 语法/名字错误只有 CI 首跑才现形。mock 门：

- **prelude.h**：全量 std 头先含，**再** `#define _WIN32 1`——libc++ 只在头文件
  首次包含时读平台宏，先含后定 = 不毒化（否则 `__locale` 的 `_SPACE`/fstream 的
  `_wfopen` 全炸；直接 `-D_WIN32` 同样毒化，实测）。
- **假 `windows.h`/`process.h`**（/tmp）：最小声明集（DWORD/HMODULE/
  GetModuleFileNameA/LoadLibraryW/GetProcAddress/MultiByteToWideChar/
  WideCharToMultiByte/GetCurrentProcessId/_beginthreadex…），SDL3 头的 win 分支
  也能吃下。
- `-D_WCHAR_T_DEFINED` 模拟 MSVC（vendored hostfxr.h 的 `char_t` = wchar_t 路径）。
- 覆盖：CoreCLRHost/packager 整文件过门 rc=0；GameEntry 因 `-U__APPLE__` 会拆
  libc++ 配置 → ExeDir win 分支抽段过门（其余 win 专属面为零）。
- 本机坑追加：zsh 下 `-I $VAR` **不分词**（SH_WORD_SPLIT 默认关）——include
  路径要内联展开，否则整串变单参数、报"file not found"极难排查。
- 定位：CI win job 仍是唯一权威 MSVC 门；mock 门价值 = push 前抓下述 ⑤ 类实锤
  （本日实抓两处，省一轮 CI 往返）。

## 2. 实锤清单（全修）

| # | 实锤 | 修 |
|---|---|---|
| ① | `CoreCLRHost.cpp:198` 用 `fs::path` 但该 TU 无 `fs` 别名（win-only，mac 编不到，MSVC 必炸） | 改 `std::filesystem::path`（后续演进为 Widen，见⑤） |
| ② | windows.h 的 min/max 宏咬 `std::min/std::max`（GameEntry 1 处/engine_tests 8 处；`Process.h` 公共头把 windows.h 带进所有消费方） | 根 CMakeLists MSVC 分支 `add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)` 全局（覆盖自有 TU + CPM 子项目，SDL_vulkan.h 在 win 侧也引 windows.h） |
| ③ | `PeImports.cpp` 两处越已验证窗口读：SizeOfOptionalHeader 声称过小时 numRva/dataDir 读越界；dataDir 条目 1 读在 8B 校验窗之外 | `kMinOpt` 下限（PE32+ 128 / PE32 112——真实 PE 恒 ≥224/240，取不到 = 坏档）；selftest 补两例（小 opt / 野 RVA） |
| ④ | packager：win publish 产物 `Host.exe` 剪除缺失；`--rid` 与宿主平台无一致性守卫（mac 机 `--rid win-x64` = exe/runtime 不匹配的静默坏包） | 剪除列表补 Host.exe；宿主一致性 guard 响亮拒绝（交叉出包 v1 不背，注释留再立项口径） |
| ⑤ | **mock 门实抓**：hostfxr API 的字符串参数全是 `char_t`——此前只分支了 set_error_writer/initCmdLine，漏了 `initForConfig(runtimeConfigPath)` 与 `GetExport` 三参（entry asm/type/method）→ MSVC 编译错 ×2；另自查纠编码坑：`fs::path(std::string)` 在 win 按 **ACP** 解释窄串（非 UTF-8），中文 UTF-8 路径宽化必须走 CP_UTF8 的 `Widen` | 四处调用全 `Widen` 宽化（ASCII 恒等无扰）；mac 行为零变化（全在 `#if _WIN32` 内） |

排除项（疑点核实非实锤）：BakedClip/AtlasBake 的 `_getpid` 已带 `<process.h>` ✓；
FileOps/GameEntry 残留 unistd/setenv 为扫描窗口误报（守卫在位，目视确认）。

## 3. 阴性验证事故与夹具重造（流程教训）

kMinOpt 修复的阴性验证首跑失败——撤检查后 selftest 仍过：首版夹具的节表仍按
240 摆，sizeOpt=64 使节表读位错到全零区 → 走"import RVA 落节外"错误路径 err
非空 = 夹具没钉住修复本身。重造：节表随**声称值** 64 摆正、numRva/importRVA
字节保持文件内在场——撤检查时解析"成功"（`err=[] dlls=2`，源级实证）、修复时
err 命中"SizeOfOptionalHeader 过小"，阴性成立。教训：**防御性检查的阴性验证
必须让"无检查路径"走到成功分支**，否则测的是别的错误路径。（另：ninja 一次
mv 往返后 mtime 平局出现过"复原即 no work to do"跑旧二进制的假象——touch 后
复核，涉临时文件搬运时以 touch 强制重建为准。）

## 4. 遗留与登记

- brew mingw-w64 交叉编译门尝试：安装未完成（网络慢），未等待——mock 门已覆盖
  高危面，CI win job 权威兜底；安装完成后可作二线门（GCC 语法面）复用。
- mock 门未入仓（假头漂移维护成本 > 收益）；配方留本条 DevLog，后续批次可快速
  复活。
- 07 §3.6 批⑦ 增补表已随 review 扩行（⑫ char_t 全 API 面 + NOMINMAX）。
