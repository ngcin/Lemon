# 帧序事故修正批：InsertFrameAfter -1 头插 + 上一帧图标镜像

2026-09-27 · 批② 动画生产线 · 用户实测两问题（svr-test Monster03 dying
多选加帧反序 / prev-next 图标同形）。诊断与事件流水见
[DevLog](../DevLog/2026-09-27-m6a-b2-frame-order-incident.md)。

## 根因

1. **帧序**：`AnimationPanel::InsertFrameAfter` 注释称"idx=-1 = 末尾追加"，
   实现 = `insert(begin() + 0)` 头插。四处"末尾追加"意图的调用点全中招：
   多图加帧（流 2，用户案例）/ 拖 Assets 图入带尾 / 拖入预览加帧 / 空帧
   （未选中时）。多图逐张头插 = 精确反序（dying.anim 实测 Dying_014..000）。
2. **图标**：预览条 animprev/animnext 出生起同用 `IconKind::Step` 无镜像。

## 改动

- `Editor/Panels/AnimationPanel.cpp`：
  - `InsertFrameAfter` 契约注释重写：**-1 = 头插**（「在之前插入副本」i=0
    依赖此，保留）；末尾追加传 `(int)edit_.frames.size()`。四处调用点改之。
  - `##animprev` 传 `flipX=true`。
- `Editor/Tooling/Icons.h`：`IconButton` 增 `flipX` 参数（UV 交换）。
- `Editor/Tooling/FilePicker.cpp`：「打开」登记 `picker.open` 探针。
- `Editor/Panels/BuiltInPanels.h`：`EditFrameCountForTest` /
  `EditFrameSheetForTest`（帧序断言面）。
- `Editor/App/EditorApp.cpp`：smoke-anim f90–f104 帧序回归链（Ctrl+A 全选
  → 真实点「打开」→ 逐位断言 = EntriesInDir 文件名升序 guid 序）；RESULT
  增 `multiadd(count/order)` 位进 animOk。

## 验证

- **阴性验证**：临时把流 2 调用点回退 -1 → `multiadd(count=YES order=NO)`
  + `=> FAIL` + "帧序 != 文件名升序"错误行——回归确实抓得住事故。
- smoke-anim `=> OK`（multiadd 全绿）；full 回归 14/14（anim-chain 首跑一次
  环境抖动，隔离 3/3 + 重跑确认）。
- 用户数据：svr-test Monster03 walk/attack/idle/dying 四 clip 帧表按源图
  文件名重排（guid 映射，schema 不动），修后逐一校验 OK。

## 遗留

- 真人验收：多选加帧后首帧 = 文件名最小图；预览条上一帧箭头朝左。
- 注入回归的窗口服务抖动（本批 anim-chain 首跑 set create/pick 链全灭、
  重跑全绿）与 T3-UX5 记录的不可见 Space 问题同类——若再现频繁再立批根治。
