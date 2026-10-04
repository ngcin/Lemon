# M7a 批⑤ done：packager 最简 + mac 干净包——lemon-packager 落地（2026-10-04）

[批文件](../Plans/M7a/2026-10-04-b5-packager-mac.md) · [ADR-016](../ADR/ADR-016-Standalone-Runtime-And-Minimal-Packager.md)

## 事件

`Tools/packager/lemon-packager`（D2 独立目标，链 lemon-engine）落地，目录拷贝式
出包 v1（ADR-016 M6/M7 布局）：

- **dylib 闭包**：otool 递归收 + `install_name_tool` 重锚 `@rpath` +
  lemon-game `-add_rpath @loader_path` + ad-hoc 重签；收件 = libvulkan.1 /
  libfreetype / libpng16.16（freetype→libpng 递归链）+ libMoltenVK（ICD 显式收）。
- **ICD 自举**：包内 `MoltenVK_icd.json`（相对 library_path 随包可搬迁，
  api_version 抄系统注册位）；GameEntry 在 `Device::Create` 前按在场自设
  `VK_ICD_FILENAMES`（不覆写用户显式设置）。
- **runtime/**：合成宿主工程（Game.csproj 是类库，dotnet publish 的
  self-contained 开关拒非 Exe 工程）`dotnet publish -r osx-x64 --self-contained`
  一次成树；`Host.runtimeconfig.json` 拷作 `Lemon.Entry.runtimeconfig.json`，
  宿主产物四件剪除。
- **data/**：项目直拷（排除 `.lemon` 生成物）+ `.lemon/bin/`（自 publish 产物）+
  `.lemon/baked/audio/` 预烤 7 件（AudioMount 同路径 `%016llx.baked`，mtime 新鲜
  = 运行时零重烤）+ `manifest.pkg.json`（`AssetIndex::ExportManifest` 新增导出器，
  schema 与读取面单源）+ `Fonts/`（Noto + OFL.txt 随包，OFL 许可义务）。
- **自检步**（机器化）：otool 依赖闭环 + **rpath 卫生**（非 `@loader_path` 残留
  = 红）+ 关键件在场 + 清单对账（记账 ⊆ 实走；runtime/ publish 树外零多出）。

## 三个实抓的坑（隐式假设判据兑现）

1. **JDK rpath 遮蔽**：lemon-game 链接期残留 JDK LC_RPATH（排序在
   `@loader_path` 前）——`DYLD_PRINT_LIBRARIES` 实证 freetype 从 JDK 目录装载，
   "能跑但是假象"的教科书案例。修 = 包内二进制非 `@loader_path` rpath 全清 +
   自检纳入 rpath 卫生面。首版清扫静默空转（`otool -l` 行首是 `cmd LC_RPATH`，
   前缀判定 `rfind(...,0)` 永不命中 → 假绿），改 `find` 包含判定后复验归零。
2. **otool -L 第二行是 install name 非**：freetype 的 id 名（libfreetype.6.dylib）
   ≠ 引用名（libfreetype.dylib）→ 同库双收；其余库 id 名与引用名同名被去重
   掩盖。修 = 闭包游走跳过"解析后即自身"条目（weakly_canonical 比对），包内
   dylib 5→4、零未解释文件。
3. **hostfxr 拒自含组件**：`initialize_for_runtime_config` 明确拒
   `includedFrameworks` 形态（rc=0x80008093）——该 API 只收 framework-dependent。
   修 = CoreCLRHost 增 apphost 同款 `initialize_for_dotnet_command_line` 回退
   （argv[0] = 入口程序集；dev 形态首试命中即短路，行为不变）。随批扩展
   `LoadHostfxr` 候选根链：显式根两形态（self-contained 平铺
   `<root>/libhostfxr.dylib` → `host/fxr/<ver>` 多版本）→ env → brew 默认。

## 阴性验证

- 悬空引用：夹具注入 `spriteGuid=12345678901234567890` → 红字拒绝、不进组装 ✓。
- 引用键判定：裸 `"guid"`（实体 Meta 身份号）不裁决——首版 19 条假阳性实证后
  收窄为"含 guid 后缀键"（spriteGuid/sourceAssetGuid 族）✓。
- rpath 清扫：修复前 JDK 路径残留 / 修复后仅 `@loader_path`，装载源全在包内 ✓。

## 门格

- 包体零参 `--smoke --frames 900`：**fps=428**（判据 ≥60）、uidoc=6、audio=7（预烤
  全装载零重烤）、hud=1 cards=1（点击进局 + 动态弹卡）、contractErr=0 => **OK**；
- `DYLD_PRINT_LIBRARIES` 装载源实证：freetype/png16/vulkan/MoltenVK/coreclr/
  hostfxr/hostpolicy 全部出自包内，JDK/brew 零泄漏；
- packager 自检：319 文件 / 115MiB / dylib 4 / 烤制 7 / manifest 38 /
  closureMiss=0 essentialMiss=0 inventoryMiss=0 extra=0 => **OK**；
- 回归 **full 19/19 首跑全绿**（pkg-smoke 第 19 步：出包 + 自检 + 包体零参
  smoke + awk fps≥60 数值判）；ctest 3/3；单测 **34220 → 34232**（+12：
  TestAssetIndexPkgManifest——导出/回读全等 + 包账优先于编辑器账）。

## 登记

- publish 树 PDB 在场（~1MB 级）与 publish 时长（NuGet 缓存后 ~10s）：体积/时长
  登记不阻塞，压缩归 M7b；
- 真干净机（无 brew/dotnet/JDK）复验归批⑦ Windows 真机 + M7b mac 侧；本批
  "干净机模拟" = 全新 temp 目录 + otool 闭环 + 装载源实证；
- 真人验收面（干净包移交即玩）归批⑧ 总成清单（既有条目）。

## 下一步

批⑥ 图集 `.baked` v1（LAT1 容器 + packager writer + 引擎 reader；与批⑤ 顺序
可换，批⑦ Windows 真机窗口先到则先批⑦）。
