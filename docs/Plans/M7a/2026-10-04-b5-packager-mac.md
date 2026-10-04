# M7a 批⑤ packager 最简 + mac 干净包（2026-10-04 开工）

Status: done 2026-10-04（门格与勾销记录见 §4;[DevLog](../../DevLog/2026-10-04-m7a-b5-packager-mac.md);输入 = [ADR-016](../../ADR/ADR-016-Standalone-Runtime-And-Minimal-Packager.md) M6/M7 + [M7a.md](./M7a.md) §4 批⑤）

## 0. 开工研读结论（2026-10-04，实证）

| # | 事实（实测/读码） | 对批⑤ 的含义 |
|---|---|---|
| 1 | `lemon-game` otool：`/usr/local/opt/vulkan-loader/lib/libvulkan.1.dylib`（绝对 brew 路径）+ `@rpath/libfreetype.dylib`；唯一 LC_RPATH = **JDK 目录**（链接期残留）——`DYLD_PRINT_LIBRARIES` 实证 freetype 从 `/Library/Java/.../jdk-17.jdk/.../libfreetype.dylib` 装载 | **隐式假设实抓**（08 隐含判据兑现）：干净机必炸;闭包须重锚 + 剥 JDK rpath 依赖 |
| 2 | brew 闭包真身：vulkan-loader（deps 全系统）→ freetype（deps：z/bz2 系统 + **libpng16 brew**）→ libpng16（deps 全系统）;MoltenVK = ICD 装载非链接依赖 | 递归闭包必要（freetype→libpng 链）;MoltenVK 走 ICD 清单显式收 |
| 3 | ICD 发现走系统注册位 `/usr/local/etc/vulkan/icd.d/MoltenVK_icd.json`（brew 布局;`library_path` 相对路径已实证可用） | 干净机无注册位 → GameEntry 需包内 ICD 自举（`VK_ICD_FILENAMES` 指包内清单,不覆写用户显式设置） |
| 4 | `CoreCLRHost::LoadHostfxr` 解析链 = 显式参（`<root>/host/fxr/<ver>/` 多版本布局）→ `$LEMON_DOTNET_ROOT`/`$DOTNET_ROOT` → `/usr/local/share/dotnet`;GameEntry 传 `nullptr` | self-contained publish 是**平铺布局**（`libhostfxr.dylib` 直接在产物根）——解析链需扩第三形态;显式根落空须回退链（dev 形态 exe 旁 runtime/ 只有托管件） |
| 5 | `Game.csproj` = 类库（Reference+HintPath 指构建树 `Lemon.SDK.dll`）;`dotnet publish --self-contained` 拒类库 | packager **合成宿主工程**（Exe + ProjectReference→Game.csproj + Reference→Lemon.Entry.dll）一次 publish 出全树（runtime + Game.dll + SDK 单源,无手拷漂移）;`Host.runtimeconfig.json`（自含式）拷作 `Lemon.Entry.runtimeconfig.json`——hostfxr 以 config 所在目录为 app base 找 hostpolicy |
| 6 | AudioMount 烤制路径 = `<root>/.lemon/baked/audio/<%016llx guid>.baked`（`AudioMount.cpp:13-20`;M7a.md 批⑤ 行"同相对路径"表述过时,以代码为准）;BakeStale 按 mtime 判重烤 | packager 预烤同路径 = 运行时零分支（mtime 新鲜不重烤）;loop/preload 已由 AssetIndex 从 .meta 读入 IndexedEntry |
| 7 | manifest schema（`AssetIndex::LoadFromManifest`）= `{assets:[{path,guid,type,spriteId,slice{base,count}}],nextSpriteId}`;类型串 AssetTypeName 单源;guid 记 **十进制 u64** | 导出器落 `AssetIndex::ExportManifest`（schema 单源,防 packager 私写漂移）;运行时快路径扩 `manifest.pkg.json` 先于 `manifest.json` |
| 8 | 编辑器构建 Game.dll → `<root>/.lemon/bin/`（`Game.dll + Game.deps.json + Lemon.SDK.dll` 实测形态）;GameEntry `LoadUserAssembly` 只认此路径 | 包内 `data/.lemon/bin/` 从 publish 产物拷同三件（不依赖 dev bin 在场） |
| 9 | 字体链：`<root>/Fonts/NotoSansSC-Regular.otf`（packager 拷入）→ `LEMON_ENGINE_FONT_DIR` 源树回退;OFL.txt 须随包（OFL 许可义务） | packager 从引擎源树拷 Noto+OFL（编译期定义,与 lemon-game 同源） |
| 10 | 回归第 18 步 game-smoke 已有 `${TMP}/game` 向导夹具（含 Game/ + dotnet build） | 第 19 步 pkg-smoke 复用同夹具出包 → 包体**零参** `--smoke`（data/ 缺省路径的端到端） |

## 1. 行级分解（开工即完成态勾销）

### A 引擎侧三小件（清障 + 消费契约,均为 ADR-016 批⑤ 明列项）

- [x] `Engine/Scripting/CoreCLRHost.cpp` `LoadHostfxr`：候选根链（显式参 → `$LEMON_DOTNET_ROOT` → `$DOTNET_ROOT` → brew 默认）× 两形态（`<root>/libhostfxr.dylib` 平铺 self-contained → `<root>/host/fxr/<ver>` 多版本）;显式根两形态皆落空 → 继续链（dev 回退语义不变）。
- [x] `Engine/Assets/AssetIndex.{h,cpp}`：`Open()` 快路径前置 `manifest.pkg.json`（包账）→ `manifest.json`（+.bak）→ 回退扫描;新增 `ExportManifest(path)`（schema 与 LoadFromManifest 单源对齐,`nextSpriteId` = 号域上界）。
- [x] `Engine/Entry/GameEntry.cpp`：`host.Initialize(entryDir, …)`（原 `nullptr`;dev 形态经链回退行为不变）+ ICD 自举（exe 旁 `MoltenVK_icd.json` 在场且 `VK_ICD_FILENAMES` 未设 → setenv 指包内清单;须在 `Device::Create` 前）。

### B Tools/packager（D2 独立目标,链 lemon-engine）

- [x] `Tools/CMakeLists.txt` + `Tools/packager/CMakeLists.txt` + 根 `CMakeLists.txt` 挂载（`LEMON_BUILD_TOOLS` 默认 ON）;`LEMON_ENGINE_FONT_DIR` 编译期定义与 lemon-game 同源。
- [x] `Tools/packager/main.cpp`：CLI `--project <dir> --runtime <engine-build-dir> --out <pkg> [--force]`。
- [x] 项目校验最小面（红字 fail-fast）：project.lemon 可解析 / entryScene 解析且文件在场 / **guid 冲突**（索引内重复 guid）/ **引用悬空**（`.scene/.prefab` 等 JSON 的 guid 形字段（u64 ≥ 2^40）对索引红字——scene 实测字段 = `guid`/`spriteGuid`/`sourceAssetGuid`）。
- [x] dylib 闭包：`otool -L` 递归收（系统前缀 `/usr/lib`/`/System` 跳过;`@rpath/NAME` 按搜索序解析本体——**不含 JDK rpath**,freetype 落 brew 本体）;`install_name_tool` 重锚消费方 `-change` → `@rpath/NAME` + 本体 `-id`;lemon-game `-add_rpath @loader_path`;逐件 `codesign -f -s -`（改即失效,重签）。
- [x] MoltenVK + ICD：本体收 `/usr/local/lib` / `/usr/local/opt/molten-vk/lib`;清单 `library_path` 相对（随包可搬迁）,`api_version` 抄系统清单,`is_portability_driver` 同。
- [x] `runtime/`：合成宿主工程（temp 目录,Exe net10.0 + ProjectReference→`<project>/Game/Game.csproj` + Reference HintPath→`<runtime>/Scripting/dotnet/Lemon.Entry.dll`）`dotnet publish -c Release -r osx-x64 --self-contained -o <pkg>/runtime`;`Host.runtimeconfig.json` 拷作 `Lemon.Entry.runtimeconfig.json`;`Host.dll/pdb/deps.json` 剪除。
- [x] `data/`：项目直拷（排除根级 `.lemon/` 生成物）→ `.lemon/bin/`{Game.dll,Game.deps.json,Lemon.SDK.dll}（自 publish 产物）→ `.lemon/baked/audio/` 预烤（BakeAudioFile,loop 参数自 IndexedEntry）→ `manifest.pkg.json`（ExportManifest）→ `Fonts/`{Noto+OFL}。
- [x] 自检步（机器化,RESULT 行回归口径）：① otool 闭环——pkg 内每个 Mach-O 的非系统依赖 = `@rpath/<name>` 且 pkg 内在场;② 关键件在场清单（hostfxr/hostpolicy/Lemon.Entry+runtimeconfig/Game.dll×2/project.lemon/entryScene/manifest.pkg.json/字体/ICD+MoltenVK）;③ 清单对账——组装期记账文件 ⊆ 实走（`runtime/` publish 树外零多出）。

### C 测试与门格

- [x] `tests/engine_tests.cpp`：`TestAssetIndexPkgManifest`（回退扫描 → ExportManifest → 以 pkg manifest 重开 → 条目全等 + FromManifest 探针）。
- [x] `tools/editor-regression.sh` 第 19 步 pkg-smoke：`${TMP}/game` 夹具出包（temp）→ 自检 RESULT OK → 包体零参 `--smoke --frames 900` → RESULT game-smoke OK + fps ≥ 60（awk 数值判）;步数 18→19。
- [x] 门格：ctest 3/3 + 单测计数增长 + 回归 full 19/19 + 包体自检绿 + 包体零参四屏 smoke OK。
- [x] 干净机判据模拟:全新 temp 目录包,不依赖引擎仓/构建树路径（otool 闭环 = 二进制面证明;brew/dotnet 面以闭包+自含 runtime 在场证明;真干净机归批⑦ Win/M7b mac 复验）。

## 2. 出口判据（M7a.md §3 批⑤ 行）

- **干净目录包（无引擎仓/brew/dotnet 依赖路径）解包即跑四屏 60fps**——机器面 = pkg-smoke 步（fps ≥ 60）。
- **packager 自检（otool 依赖闭环 + 文件清单）绿**——RESULT pkg-selfcheck => OK。

## 3. 风险与兜底

- `dotnet publish` 类库拒自含 → 合成宿主工程（§0.5;Unreal RunUAT 同形态）;Host 产物剪除防混淆。
- JDK rpath 的 freetype 假路径 → @rpath 解析搜索序不含 JDK;闭包自检兜底（漏收 = 红字）。
- ICD 相对路径 loader 兼容性 → brew 系统清单本用相对路径（§0.3 实证）;仍失败 = 回退绝对路径重写清单（登记项）。
- publish 时长（首次 ~30-60s）→ 回归第 19 步接受;CI 化后评估缓存（M7a.md §8 登记项既有）。
- 签名后改文件 → 重签序纪律:改名/改 id 后统一 codesign（漏签 = arm64 干净机 dyld 拒载;x86_64 本机宽松仍签）。

## 4. 勾销记录（2026-10-04 完工）

- **门格**：回归 **full 19/19 首跑全绿**（pkg-smoke 第 19 步 = 出包 + 自检 + 包体零参 smoke + awk fps≥60 数值判）；ctest 3/3；单测 **34220 → 34232**（+12 = TestAssetIndexPkgManifest）。
- **包体实测**：319 文件 / 115MiB / dylib 4（vulkan/freetype/png16/MoltenVK）/ 烤制 7 / manifest 38 / 四类 miss 全零；零参 `--smoke` fps=428（≥60）、uidoc=6、audio=7 预烤全装载零重烤、hud=1 cards=1；`DYLD_PRINT_LIBRARIES` 装载源全在包内（JDK/brew 零泄漏）。
- **三坑实抓与修**（详见 [DevLog](../../DevLog/2026-10-04-m7a-b5-packager-mac.md)）：① JDK rpath 遮蔽 freetype（`@loader_path` 排序在后）→ 包内非 `@loader_path` rpath 全清 + 自检 rpath 卫生面（首版解析空转假绿，`cmd LC_RPATH` 行首判定改 `find` 包含）；② otool -L 第二行 install name 被当依赖 → freetype 双收，修 = 自引用跳过（weakly_canonical）；③ `initialize_for_runtime_config` 拒自含形态（0x80008093）→ CoreCLRHost 增 command_line 回退（apphost 语义）+ fxr 候选链扩平铺形态。
- **阴性验证**：悬空 spriteGuid 注入 → 红字拒绝不进组装 ✓；裸 `"guid"`（实体身份号）不裁决（首版 19 条假阳性实证后收窄）✓；rpath 清扫前后对照 ✓。
- **dev 形态回归面**：CoreCLRHost 显式根落空回退链 + initForConfig 首试命中短路——game-smoke（构建树 lemon-game）第 18 步照绿 = 行为不变佐证。
- **登记移交**：publish 树 PDB/时长（NuGet 缓存后 ~10s）不阻塞，压缩归 M7b；真干净机复验归批⑦/M7b；真人验收面（干净包移交即玩）归批⑧ 总成清单。
