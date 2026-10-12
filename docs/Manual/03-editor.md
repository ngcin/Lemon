# 编辑器

单 OS 窗口 docking 布局（ImGui），编辑器 = 运行时内核 + 编辑器层同一份引擎库——
SceneView/GameView 是真渲染管线渲到纹理贴进视口，**所见即实机**。冷启动秒开是验收红线。

## 面板一览

| 面板 | 职责 |
|---|---|
| Hierarchy | 实体树、搜索、拖拽父子、右键菜单（创建/复制/删除/Prefab 化）；Play 态显示场景组与跨场景幸存实体（DDOL 徽标） |
| Inspector | 选中实体的组件列表 + 属性控件 + Add Component 菜单；Prefab 覆盖头栏（Apply/Revert/Break/Select） |
| SceneView | 编辑相机（平移/缩放/F 框选/网格吸附/像素对齐）、移动/旋转/缩放 gizmo、拾取 |
| GameView | 游戏相机预览；Aspect 下拉（Free/16:9/4:3/1:1 letterbox）；光照与后处理在此渲（SceneView 恒不渲光照，以 gizmo 圈代替） |
| AssetBrowser | 项目资产树、缩略图、拖拽进场景/组件槽、导入设置；clip 双击进动画工作台 |
| Console | 日志分级过滤、脚本异常红字 |
| Profiler | F3 性能面板（帧时间分解/实体数/粒子数/批次/纹理内存/GC 等） |

动画工作台（AnimationEditor）按需从 AssetBrowser 双击 clip 打开。

## Play / Stop 沙盒

Play 运行在编辑器进程内的**独立 World**：Edit World（被编辑的数据）与 Play World
物理隔离。进入 Play 前对 Edit World 做全量快照，Stop 时反序列化恢复——零状态泄漏。

- **Play 中允许编辑，改动只落 Play World**（Unity 心智，Stop 即丢，无确认弹窗）。
- Undo 在 Play 中禁用；Inspector 有「改动随 Stop 丢弃」横幅提醒。
- 存档在进 Play 时自动载入、退 Play 兜底落盘（`.lemon/saves/`）。

## 日常操作

- 保存：Ctrl+S（脏标记 + 关闭确认 + 定时自动备份 `.lemon/autosave/` 与启动恢复）。
- 摆实体：Hierarchy 右键创建或 AssetBrowser 拖入 → Inspector 加组件（新增组件
  由反射注册表驱动，**零编辑器代码**即出现在 Add Component 与 Inspector）。
- 常用字段控件：枚举下拉、颜色、Range 滑条、角度（按度数编辑）、资产槽
  （下拉/拖入/右键清空）。
- 挂脚本：Inspector 的 Script 段 8 槽，逐槽选类型（同类型同实体唯一）。
- Prefab：改过字段蓝标；组件头栏 Apply（回写源）/ Revert / Break / Select。

## 界面语言

编辑器界面双语（zh-CN 首发，en 随后），字符串表驱动。

> 设计依据：[05 编辑器分册](../EngineDesign/05-Editor.md)（面板框架/Play 沙盒/
> Inspector 反射注册表/视口工具）。
