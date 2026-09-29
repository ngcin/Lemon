# 批③c-2：UI ops staging 同值去重（T8 后续卫生批）

- Status: **代码面收口 2026-09-29（T1–T5 done；script-tests 扩 2 判据 + smoke-uirml 阴性验证过 + 回归 full 16/16；真人验收 n/a——纯通道行为，无 UI 观感变化）**
- 日期：2026-09-29；前置：③c（GameUI.cs ops 通道）/ ③d-1（PlayerHud 每帧写范式）
- 动机：T8 后修期间实证 PlayerHud 每帧 10 op 中稳态 ~95% 冗余（time 每秒变
  1 次/best/wave 近乎不变），而 RmlUi `SetInnerRML`/`SetAttribute` 无等值早退
  （源码双证）——同值也全量拆建文本元素。主流 retained GUI 在 setter 层均有
  `if (same) return`（UGUI Text / Godot Label / cocos setString 同位）。本批把
  该标配补到 SDK 层。

## 设计定案（实现前冻结）

1. **落点 = SDK staging（GameUI.cs）**，引擎侧 ApplyOps 去重被否：引擎缓存
   "每元素上次写值"会被 DOM 外部变异路径打破（模板克隆整容器重建、文档重装载、
   Edit 双击预览），失真即静默丢写；staging 是 C# 侧唯一写入口，缓存与写入
   历史天然对齐。
2. **白名单 = 幂等值 op**：SetText / SetAttr / SetClass / SetStyle / SetInnerRml。
   **Show/Hide/SetItems 绝不去重**——Show 重复语义 = D1 层序提顶（去重即改
   行为）；SetItems 全量替换非幂等键值（模板 ReplayCards 重灌依赖）。
3. **缓存键** = `类型前缀|doc|key|限定名`（attr/cls/prop），**值** = 载荷串
   （SetClass 用 on 的 "1"/"0"）。C# 侧原始串比较（引擎侧 EscapeText/GUID
   协议转换是纯函数，同原始串 → 同转换结果）。
4. **失效点五处**（漏一处 = 静默丢写）：
   - `Reset()`（换域）/ `PlayReset()`（进 Play——帧计数归零重灌场景）
   - `PullOps` 两丢弃路径（-1 超容 / 无钩子弃置）——载荷未达引擎，缓存却记
     "已发送" = 谎言
   - `DiscardPending()`（导出异常强制丢弃，同上）
   - `DispatchEvents` 见 DocumentReloaded → **整表清**（DOM 已重灌全视为未
     发送；热重载为开发期低频事件，宁可整批多送一拍，不做按 doc 前缀裁剪）
5. PlayerHud **零改动**——去重在 SDK 层吸收，应用侧每帧直写仍是标准姿势。

## 任务分解

- T1 GameUI.cs：`s_lastSent` 表 + `DedupSkip()`（持 s_lock）+ 五 setter 接入 +
  五失效点。
- T2 TestScript.cs：`GameMain.UiDedupProbe()`（帧 2：写 A → 同值 A → 异值 B →
  Apply，恰 2 op）+ UiProbeBehaviour 帧 2 分支。靶元素 = `body`（夹具静态行，
  编辑器/脚本测试两侧无内容断言——不碰 title，避开 smoke-uirml 终帧契约）。
- T3 tests/script/main.cpp TestUiSdk ③④ 段：③ 帧 2 步进 → 恰 2 op + 值序断言；
  ④ 注入 DocumentReloaded（s_uiInjectEvent 复用）→ OnUiEvent → UiRefill 同值
  重写 → 恰 6 op 全发（缓存复位契约）。
- T4 验证：script-tests 全量 + smoke-uirml 双模式 + smoke-template（行为零漂移
  ——数值逐位一致）+ **阴性验证**（注释 DispatchEvents 清缓存 → ④ 必红：重灌
  塌缩 6→3 op——同值 SetText×2 被吞）→ 复原复绿。
- T5 文档：DevLog + 本批勾销 + M6b.md 批次表 + AGENTS.md ③c 段注记。

## 验收判据（全过才勾销）

1. ✅ script-tests：既有 ① 6-op 对拍零变化 + 新 ③ 恰 2-op（同值跳/异值过）+
   新 ④ 重装载复位恰 6-op。
2. ✅ smoke-uirml 双模式全绿（`ev=c1r4 contract=1/textOK`——重灌链未被去重吞）。
3. ✅ 阴性验证：去 DocumentReloaded 清缓存 → script-tests ④ FAIL（6→3 op 实抓）
   → 复原复绿。
4. ✅ smoke-template 全绿数值逐位一致（PlayerHud 每帧写经去重后行为不变——
   断言读 DOM 态非 op 计数）。
5. ✅ 回归 full 16/16。

## 实现期发现（偏离批文件预设计的落账）

1. **预设计的"smoke-uirml 终帧 title = 内建阴性"判断错误**：冒烟结尾三局
   Play→Stop→Play，每局 PlayReset 本就清缓存重灌，终帧 title 是最后一局帧 1
   写的——帧 100/140 重装载被吞与否终帧都绿（实测阴性跑 OK 证伪）。真阴性
   改 script-tests 事件注入法（④），确定性且常驻。**残余缺口**：编辑器侧
   重装载中途的 title 内容无断言（终态有、中途无）——SDK 契约已被 ④ 钉死，
   编辑器侧中途观感留 ③d-2 冒烟扩位再议。
2. **事件驱动 UI 写的可见时点 = 下一帧**（既有行为，非本批引入）：本测试管线
   #16 派发插 CSharpBatch 后，事件 handler 的 ops 晚于当轮拉取点入 ready →
   ④ 需两步步进。编辑器内 DocumentReloaded → 重灌同样下一帧可见（R5 同口径）。
3. **阴构建目标坑**：TestScript.dll/SDK 走 `lemon-dotnet-asm` 自定义目标
   （`--target TestScript` 不存在）——阴验证必须确认 dotnet 侧真重编（本次
   首轮假阴性即此：旧二进制照跑全绿）。

## 风险与既知边界

| # | 坑 | 处置 |
|---|---|---|
| R1 | 失效点遗漏 → 静默丢写 | 五处清单化 + script-tests ④ 注入 DocumentReloaded = 复位契约常驻哨兵（真阴性验证过） |
| R2 | 同 (doc,key) 交替写 A/B/A → 缓存不合并（每次异值都过） | 语义正确（终态幂等），非缺陷；HUD 交替场景罕见 |
| R3 | 缓存无界增长 | 键 = 游戏静态 UI 面（doc×key×限定名），有界；换域/进 Play 清 |
| R4 | 多写入口绕过 staging（引擎直灌 op） | 直灌 op 不经缓存——不记也不查，无失真（缓存只对 staging 谎言敏感，方向单一） |
| R5 | 事件驱动 UI 写下一帧才可见（#16 派发在拉取点后） | 既有行为非本批引入；④ 两步步进口径化 |

## 关联

- 前因：[DevLog 2026-09-29-m6b-b3d1-t8-bar-fill-fix.md](../../DevLog/2026-09-29-m6b-b3d1-t8-bar-fill-fix.md)（性能问询起点）
- 同批族：③c（GameUI.cs 通道本体）
