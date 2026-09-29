# 2026-09-29 · UIDocument 删除残留——根因收口：事件吞噬 → 状态对账

## 背景

真人验收②三轮：兜底（F-B）已生效，但根因未明。用户问：底层引擎渲染问题还是编辑器
问题？本轮给出机器实锤答案。

## 根因（机器复现，非推理）

**事件驱动逐出存在"事件吞噬"逃逸路径**：

```
删 .rml（编辑态）→ 同帧裸 db.Rescan()      ← ImportFile / MakePrefabFrom 形态
  ├─ 墓碑立（missing=true）→ 红字路径成立
  └─ cs.removed 事件被吃（不经 RescanAssets 的 UI 逐出半边）
→ 后续 watcher 重扫：prev.missing 已真 → cs.removed 恒空 → UnloadDocument 永不调用
→ 残留文档持续渲染（僵尸）直到进程重启
```

smoke 形态 C（f304 删除 + 同帧裸 Rescan 播种）修复前实测：`edit=0 pix=8176 FAIL`——
**8176 像素僵尸面板**，与用户观察完全同构（不重启残留 / 重启消失 = 上下文清空）。
裸 Rescan 调用方在正常使用中**可达**：拖拽导入（`ImportDroppedFile`）、Assets 导入
按钮、CSV 转换、Prefab 化（`MakePrefabFrom`）。

用户会话的确切触发序列无法从截图 Console 回溯（历史不全），但该机制是唯一与全部
观测吻合的路径，且已实锤存在。

## 引擎 vs 编辑器的裁决

- **引擎原语干净**：`UnloadDoc` 的 `Close()` 真摘除（像素级验证）；装载/卸载键一致；
  `missing` 只在 `Rescan()` 置位。问题类别在**编辑器的文档生命周期治理**。
- 治理层病根：**正确性依赖"事件恰好经过处理面"**——任何新增的裸重扫调用方都会静默
  击穿它。这类 bug 的温床是架构性的（事件追逐），不是单点代码错。

## 修复：状态对账（不变量层）

- `UiSubsystem::FileBackedDocumentNames()`：文件装载文档枚举面（内存底稿不参与——
  无资产对应，逐出语义不适用）。
- `EditorApp::ReconcileUiDocuments()`：relPath 非健康 .rml 资产（查无/墓碑/非 Rml）
  → `UnloadDocument` + 观测日志「UI 文档对账逐出」。
- 挂两处：`RescanAssets` **尾**（无条件——cs 空也跑，被吞事件的墓碑靠 watcher 下一拍
  自愈，会话中途即治愈）；`MountSceneUiDocuments` **头**（进 Play 不变量——裸重扫后
  未及任何 watcher 拍即进 Play 的窗口）。事件路径保留（首拍即逐出 + 日志），两者幂等。
- 上一轮 F-B（仅声明缺失的逐出）被通用对账取代删除。

## 验证

- 形态 C：修复前 `edit=0 pix=8176 FAIL`（根因实锤）→ 修复后 `edit=1 pix=0 OK`（治愈）
- smoke-uirml：脚本 ×3 + 无脚本 ×1 全绿（`evict2(seed=1 live=0 edit=1 pix=0)`）
- 回归 full 15/15；ctest 3/3

## 关联

- [上一条：僵尸渲染双防线（含静态排查全链）](./2026-09-29-uidoc-evict-zombie-render-guard.md)
- [批文件·后续二轮落账](../Plans/M6b/2026-09-29-b3d-pre-uidocument-scene-mount.md)
