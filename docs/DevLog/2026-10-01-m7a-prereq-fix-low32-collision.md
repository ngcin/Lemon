# M7a 开工前置修复：GUID 低 32 位防线落地 + svr-test 数据修复 + script-spawn 冒烟门控

- **日期**：2026-10-01（接续 [前置核清条目](./2026-10-01-m7a-prereq-update-guid-collision.md)，同日第二轮）
- **用户拍板**：ui.scene 整场景删除；guid 数据修复由引擎侧代做；B+C **独立落地不进 M7a 批**；"家族前缀"赋值法非用户刻意（重生成 GUID 入口 D 档维持登记）。
- **勘误**：前条目 guid 十六进制值多带前导 `d`（提取脚本把 key 名 `"guid"` 里的 d 混进了值）——真实家族 = 模板 `7e571x` / svr-test 自建 `7e574x`，碰撞对与结论不变。

## 1. 数据修复（svr-test）

- `Scenes/ui.scene` 删除（M4 时代 UI 试验场，不在四屏流程；场景非资产无伴生 meta，唯一悬空引用所在）。
- Player/Director prefab guid → 随机值 `ee516071c5ca3e29` / `b2d7cc08d3ada0cd`（生成时对全库做全宽 + 低 32 双重唯一校验）；引用面 = 两个 `.prefab.meta` + `Game/GameFlow.cs:15-16` 常量（全项目 grep 零其他引用）。
- **manifest 十进制坑（实测撞出）**：`.lemon/manifest.json` 以**十进制**存 guid，且 Rescan"记账优先"会用 manifest 值覆盖 .meta 新值（`AssetDatabase.cpp` Rescan manifestCarry 路径）——只改 .meta 会被缓存洗回。首轮复验碰撞红字仍在（**新体检当场抓住了修复不完整 = 防线自证**），补 manifest 两处十进制替换后清零。教训：改 guid 的完整闭环 = .meta + manifest + 引用**三同步**。

## 2. 引擎防线（原 B+C 方案；B 经代码核实改形）

- **B（键升 u64）否决**：低 32 位约定不止 `playPrefabCache_`——`World::SpawnFn` 签名与 `Spawner.prefabId`/`Shooter.projectileId` 组件字段恒 uint32（03 §69 冻结 schema，注释明示 M7 dense id 表为既定替换路径）；而 C# `Instantiate.Prefab` 路径本就全宽（`InstantiatePrefabAsset` → `FindByGuid(u64)`，每 spawn 现读文件不走缓存 map）。全 guid 重复检测 Rescan 已有（后者重发号）。B 无新代码，收敛进 C。
- **C 落地**（`Editor/Assets/AssetDatabase.{h,cpp}`）：
  - `IsLow32Keyed()`：五个键域 = Prefab / Clip / AnimSet / Controller / Table（各自独立键空间——跨类型同低 32 位合法，体检按域不按全库）；
  - `GenerateUniqueGuid(type)`：发号拒绝采样 = 全宽全库唯一 + **同类型域内低 32 唯一**；`SyncMeta` 新发号与 GUID 冲突重发号两调用点换装；
  - Rescan 低 32 位碰撞体检：域内同值红字（计入 HealthIssues）**只报不重发**——碰撞对两 guid 全宽互异、全宽引用完好，重发反而断引用；修复动作 = 重新生成其一。分配期防线兜不到的两条路（手工改 .meta / 模板拼装）由本体检守。
- **换算法（Unity 128 位 / 标准 UUID）对本案无效**：碰撞不在随机性（`Guid.h` splitmix64 全宽碰撞 2⁻⁶³ 量级），在截断约定（03 §69 恒 u32）——就算 128 位 UUID，截成低 32 位存进组件照样撞。Unity 免疫是因为**从不截断**且不允许手工家族赋值，不是算法优势；对症解 = 域内唯一性不变量 + 体检。

## 3. 附带修：script-spawn 冒烟门控

`--smoke` 播种挂 `SpawnerBehaviour` 的旧门控是 `ctx_.Scripts()`（任何脚本后端为真）——但该类是 `--script` 测试装置（TestScript）注册的，真项目 Game 程序集本无此类 → svr-test `--smoke --play` 恒 FAIL（M6c 期间误归因"用户 WIP"的另一半真身）。修 = 挂载前先 `ResolveScriptTypeId` 解析，可解析才挂、断言随挂载成立（`smokeSpawnScript_`）。svr-test 该检查从恒 FAIL → 门控跳过、退出码 0。

## 实测

- **阴性验证**：新单测先在未修复代码上跑红（`FAIL: same-type low32 collision flagged exactly once`）→ 修复后绿。
- engine-tests **34036 → 34040**（+4，`TestAssetDatabaseLow32Collision`：同域红字恰一次 + 跨域不报 + 发号避开域内已占低 32）；ctest 3/3。
- 回归 full **17/17 两轮**（C 落地后 + 门控修正后；script-chain 步走 `--script` 真装置 = 门控零误伤的对照）。
- svr-test `--smoke --play --frames 240`：碰撞红字 **2 → 0**；音频装载 9 成功（流式 2）；play-roundtrip byte-exact YES；exit=0。剩余体检红字 1 条 = 既有孤儿 meta（`Assets/player/player01/player001_nobg.png.meta`，png 早删 meta 残留——用户侧一删即净，未代删）。

## 发现

1. manifest 记账优先于 .meta 是 guid 稳定性的**设计语义**（06 §2）；"改 guid"操作的三同步面（.meta/manifest/引用）此前无档可查，漏一会被新体检红字抓回。
2. 冒烟检查挂外部装置类名前应先解析——检查的前提要与被测对象结构解耦（真项目 Game 程序集没有义务包含测试装置类）。
3. 体检在修复落地当天就抓住了自己数据修复的不完整（manifest 洗回 .meta）——红字响亮原则的即时回报。

## 落账

[M7a.md](../Plans/M7a/M7a.md) 现状盘点 #9 / 批⓪ 开工核对 / 风险表三处同步。
