# M6a 批② T5/T6：存档分档三通道 + 批② 全量收口

2026-09-28 · 批② 计划内尾批（[批文件](../Plans/M6a/2026-09-25-b2-content-production.md)
T5/T6；06 §10 M5 推迟项收口）。用户口径两条（防后续里程碑撞僵 schema）：
**settings 档 = 版本化 key-value**（首键 `version` = 键集结构版本——M6b 音量/
手柄键位/画质档位直接加键，别把结构定死成字段）；**meta 档预留收集条目结构**
（`col.<条目id>.state` / `col.<条目id>.count`，条目 id → 状态/计数映射）。
约定落 `SaveChannel.h`/`Save.cs` 头注 + 06 §10 修订注——引擎不解析、纯键约定层。

## 事件（T5）

- 引擎面：`World` 单 `saves_` → `saves_[3]`（档常量 `kSaveSlot/kSaveSettings/
  kSaveMeta` + `ClampSaveChannel` 落 SaveChannel.h）；`Saves()` 默认返 slot
  （既有调用点零改）+ `Saves(ch)` 重载。通道语义零哈希面（先例二原例——数组化
  不入 StateHash）。
- 编辑器面：`EditorContext` 三函数参数化 `SaveFilePath(ch)/WriteSaveFile(ch,)/
  LoadSaveFile(ch,)`（文件名 slot_0/settings/meta.sav；.bak 兜底随档走）；
  **旧 game.sav 惰性迁移**——slot 载入链 slot_0.sav → .bak → game.sav →
  game.sav.bak（新档不存在才读旧名；写恒写新名免 rename 竞态，旧文件保留）；
  EnterPlay 三档循环载 / ExitPlay 三档循环落（空档跳过、逐档独立不因一档
  失败断链）/ HookSaveFlush 三档全落。
- ABI：vtable 尾加 3 项 `saveSetEx/saveGetLenEx/saveGetEx`（尾参 `uint8 ch`；
  越界红字 + 落 slot；空宿主同旧四指针降级）。`saveFlush` 不加 Ex——语义 =
  全档落盘。
- SDK：`Save.Chan` 枚举（Slot/Settings/Meta）+ 五方法可选参默认 Slot（源码
  兼容——模板/用户项目零改即编译）；旧宿主（无 Ex 表项）chan 忽略走唯一档。
- 模板面：PlayerCombat `vs.best` 两点改 `Chan.Meta`（读 Start / 写死亡新纪录
  + Flush 全档）；`--gen-vs-template` 重生成入库。
- smoke-template 断言升级：种子双载体（meta.sav vs.best=123 = Chan.Meta 载入
  回显 + game.sav = 旧名迁移链载体）→ Stop 后裁决 `saves(slot_0/meta/legacy/
  skipEmpty)` 四断言（slot_0.sav 存在 + 解码含种子键（迁移闭环）+ meta.sav 存在
  + 空档 settings.sav 不落文件）。

## 事件（T6 全量收口）

- 文档回写：06 §10 分档修订注（含键约定）/ 03 落地注（Tables + saves_[3]）/
  04 vtable 批② 全量流水（T2 表 3 项 + T3c/T3d + T4 xpCurveK + T5 Ex 3 项 +
  Lemon.Table/Lemon.Balance/Save 扩参）/ 05 §2 面板集解冻注记 + §5 双击先例 +
  §7 AnimationEditor 落地口径 / 09 §6.8 先例二通道族扩员 + §6.10 台账行 +
  回归 14 步订正 + smoke-template T5 断言注记 / 08 M6a 批② 勾销 / 本条 +
  批页勾销。

## 验证

- engine-tests 新增 `TestSaveChannelSplits`（+22 = **33541 checks**）：三档
  路径独立/越界钳 slot/三档落盘互不覆盖/空通道跳过/回读独立/**旧档惰性迁移
  全链**（删新档留旧名 → 载入走旧路径 → 写恒写新名 → 旧文件保留）/坏档兜底
  按档隔离（主垃圾→bak 回退 × 邻档不受影响 × 无 bak 空档开局）/16 MiB 上限
  按档（超限跳过 → 迁移接力钉板）。
- script-tests 新增 `TestSaveChannels`（+13 = **1679 checks**）：引擎侧三通道
  隔离/默认参 = slot/越界钳位/新 World 自清 + SaveChanProbeBehaviour（typeId
  16 表尾）端到端——settings 版本键/meta 收集条目键/越界 chan 落 slot 可见/
  档间九项回读 + C++ 原生通道对拍 + RtUi 回读双证。探针清单计数 16→17。
- ctest 3/3；smoke-template 全绿（`saves(slot_0=YES meta=YES legacy=YES
  skipEmpty=YES) => OK` + 既有 tables=YES/saveLoad=YES 链全保）；金回放三档
  现录现放 **mismatches=0**（sim-st/sim-mt/script threads 1+4——通道族零哈希
  + 基准场零分档脚本的结构性自证）；bench-survivor **fps=82 PASS**（playerHp
  275793 逐位一致第六次，09 §6.10 台账行）；回归 full 14/14（数字见批页）。
- 用户项目零强制：svr-test key 前缀在 slot 档照旧工作；`vs.best` 若自迁
  Chan.Meta 属可选（DevLog 注记即本条——无代码侵入）。
