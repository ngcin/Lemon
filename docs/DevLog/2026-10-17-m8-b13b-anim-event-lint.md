# M8 批⑬b —— 反向 lint 三件（编辑期聚合告警 + 订阅者面零槽回流）

日期：2026-10-17（⑬a 同日续批）· 批文件
[2026-10-17-b13-anim-event-registry.md](../Plans/M8/2026-10-17-b13-anim-event-registry.md) §2 行 4 / §4

## 做了什么

- **订阅者面零新槽回流（设计裁决，偏离 ⑬a 登记口径）**：⑬a 登记"订阅者集合 =
  `AnimEvents.DefinedNames` internal 面"——实施时改定为**引擎侧桥查询记录**：
  `AnimEventIdByName`（⑬a 唯一新槽）顺手记录查询名。C# 侧每名每域首解析必过桥
  （`TryIdOf` 缓存域装载起清空，Define/OnFrame 声明即查询）→ 引擎记录即得订阅者
  面，**零新增 vtable 槽、零 C# 改动**；DefinedNames 面保留不动（未来 C# 侧自
  消费可用）。注入即清（表换 = 比较基线换）；编辑器同表重注入被 guid/hash 变更门
  拦 → 退 Play 保留，Edit 态 lint 显示"上次 Play"面。
- **lint 三件纯函数**（`AnimEventLint`，kind 结构化结果——面板按 kind 取 i18n 模板，
  引擎侧零字符串耦合）：
  ① 打点 id 未注册（`used` 序 + 同 (clip,id) 去重，badge 逐处的聚合告警面）；
  ② 注册名 ∉ 订阅者（表行序；**空订阅者门**——本会话未装载脚本整组抑制不误报）；
  ③ 订阅名已注册但全项目无打点（死订阅 info；未注册订阅名归 ⑬a 启动红字不双报）。
- **AnimationPanel 黄字条**（项目级，模态层后常显）：聚合 = 全部 .clip 盘面
  events[].id + **当前编辑未保存态**（编辑目标跳盘读改用 edit_）；缓存戳 = 注册表
  变更门（`EditorContext::AnimEventTableStamp` 新访问器）^ DB 条目 guid/hash 签名，
  编辑脏标（CommitEvent/DeleteEventAt 置位）触发重算；③ 灰显区分 info 级；>5 项
  折叠尾行（M7c 批⑫ 越窗教训续）。i18n +5 keys（anim.json 218→223 双语对齐）。
- **编辑器侧桥污染修复（过程实报，4 处）**：首跑 f137 假红实报 `subs=1`——编辑器
  C++ 四处 `AnimEventIdByName` 调用（AddNamedEventForTest 钩子 / CreateAnimEvent
  重名拒 + 建行自证 + 建行即选中）把作者面操作记成了"订阅者"，②③ 假阳。修 =
  统一 `RegistryIdOf` 条目扫描（`AnimEventAt`——模态下拉本就走的条目数据面），
  smoke 侧 `SmokeAnimEventId` 同改。**纪律沉淀：桥（IdByName）= C# 订阅声明专用
  通道，编辑器 C++ 一律走条目面（AnimEventAt/NameById）**——已写入两侧代码注释。

## 验证

- 构建 Release 零编译警告（ld duplicate-library = b1 既有登记）；单测 **35,012
  checks OK**（+11：TestAnimEventLint——三件各一例/首查询序/含未注册名记录/
  (clip,id) 去重/空门抑制/注入清面/收尾还原）；ctest 4/4（script-tests 含
  TestAnimEventBridge 不受桥记录影响——探针只注册不消费）。
- `--smoke-anim` zh+en：`lint(hit=YES clean=YES)` 首跑红（桥污染）→ 修后双绿；
  既有位（含 reg 四位/events 五位）零回归；帧预算 **138 不变**（f137 空位即本链，
  无新帧号）。
- 回放三档：引擎模拟面零改动（lint = 编辑器/装载期面，AnimEventIdByName 记录
  不入模拟态）→ 零重录口径维持（full 回归内含）。

## 裁决与登记

- "订阅者集合 = DefinedNames"（⑬a DevLog 登记）→ 实施改桥查询记录（零槽优于
  新回流通道）；DefinedNames 保留不删。
- lint ② 的语义口径：**订阅者面 = 自最近一次表注入起的桥查询**——Edit 态显示
  "上次 Play"快照（i18n 文案明示），Play 态实时；项目切换（OpenProject 重注入）
  自动清。
- ②③ 的 smoke 正向路径不可测（临时项目无 C# 消费者）——引擎单测钉死逻辑面，
  smoke 钉接线面（① 聚合 + ②③ 空门 + 清理归零），分工登记。
- ⑬c payload 类型化 / 批⑭ 样本+手册+模板顺延不变；⑮a（终态显式建模，b15 批
  文件）紧随本批。
