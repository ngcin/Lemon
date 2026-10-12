# 资产族复名：.anim→.clip、.override→.ani（ADR-013 D2 修订，用户实用反馈）

- 日期:2026-10-13
- 决策:用户实用反馈推翻 2026-09-27 的 Unity 对齐命名——"`.override` 太奇特需要修改"。
  复名为 `.clip`（帧动画剪辑）/ `.ani`（动画集）。原反对理由"`.ani` 与 `.anim` 一字母
  差必混"随 `.anim` 退役消失（新对 `.clip/.ani` 视觉区分充分）。Unity 对齐纪律在
  动画族破例。修订落账 [ADR-013 D2](../ADR/ADR-013-Anim-Control-Layer-And-Asset-Family.md)、
  [06 §2.2](../EngineDesign/06-Asset-Pipeline-Out-of-Box.md)、AGENTS 命名行。

## 机制

- **硬切换**：`assets::TypeOf` 只认新扩展名（`.clip`→Clip、`.ani`→AnimSet）；引用语料
  扩展名表（AssetDatabase kRefExt）与 packager 悬空扫描同步换新。
- **开项目自动迁移**（`AssetDatabase::MigrateLegacyAnimExtensions`，manifest 载入后 /
  首次 Rescan 前）：项目根同口径扫描（SkipDir 排除）→ 旧扩展名文件 + `.meta` 同行
  改名（guid/type 字符串零迁移——"clip"/"animset" 自始未动）+ `manifestCarry_` 键随行
  重映射（spriteId 不漂）+ `hashCache_` 旧键清理。目标名已存在（新旧混态）= 跳过 +
  红字不覆盖。会话中途外部放入的旧扩展名按通用档处理，重开项目归位。
- **仓内存量同步改名**（不依赖迁移）：Templates/vs-survivor、demo/svr-test、
  demo/ui-test、Samples/yami-dungeon 共 51 资产 + 51 meta；四份 manifest（含 .bak）
  路径键修补；生成器（VsTemplateGen spec/拷贝清单/README 素材行）与 demo 注释同步。
- 编辑器面：AnimationPanel 全部扩展名构造点（新建 clip/集、改名、复制、接回 wantRel）、
  smoke 种子与断言、i18n 两键（hint_open_target / set.footnote）、ComponentCatalog
  ED_TIP（顺修 ".override/.override" 笔误）、约 30 处注释扫词。

## 实测

- engine-tests 新增 `TestLegacyAnimExtMigration`（旧扩展名夹具 → OpenProject →
  旧名消失 / 新名在册 / guid 跨开稳定 / manifest 落盘用新路径）；既有夹具全部
  换新扩展名（25 处）。ctest 4/4。
- smoke-anim zh/en `=> OK` 全绿（set create/flow/adopt 等断言走新扩展名）。
- **svr-test 实弹**：改名后开项目 `313 条资产 / 体检红字 0 / next id 403`（改名前
  312 条 next id 403——spriteId 账逐位保持；+1 = 用户会话新增资产）；ani.scene
  无头 1500 帧 exit 0。
- full 回归 26 步：25 直绿 + bench-survivor 门禁一红（fps=75<76.5，sim 单帧
  尖刺 48ms——并发噪声家族，昨天同门禁 present 尖刺同款两例）；**单独复跑两连
  PASS**（77/78，第二跑 frameMax 回落 32ms、stepAvg 10.54ms 与改名前 10.55ms
  逐位持平——动画解析走 GUID，sim 路径零影响实证）。26/26 有绿记录。

## 遗留

- 会话中途外部放入 `.anim/.override`（编辑器不重开）按通用档显示——重开项目自动归位
  （已注释登记，非缺陷）。
- 金回放三档：guid 引用不受路径影响，理论零重录——以回归实跑为准。
