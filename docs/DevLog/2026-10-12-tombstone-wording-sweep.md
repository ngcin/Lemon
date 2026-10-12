# 墓碑退役扫词收尾：动画编辑器右键「删除动画文件（进墓碑）」等残留措辞

- 日期:2026-10-12（用户问"进墓碑还有必要吗"引出——答:机制已退役，此为文案残留）
- 背景:[2026-10-01 墓碑退役](./2026-10-01-orphan-meta-sweep-and-tombstone-retirement.md)
  （用户拍板"删了就是删了"，06 §2.2 修订）当日扫词改了浏览器右键
  "删除（转墓碑）"→"删除"，动画编辑器右键与若干代码注释是漏网之鱼。
  用户在验证动画编辑器修复时看到该菜单项，误以为墓碑机制还在。

## 事实核对（今日复核）

- 跨会话墓碑不存在：`SaveManifest` 跳过 `missing` 条目（`AssetDatabase.cpp`
  SaveManifest 首行守卫），Rescan 消失文件同轮出表。
- `AssetEntry.missing` = `Remove()` 的**同帧隐藏位**（防浏览器瓦片循环迭代器
  失效）+ `lastChange_.removed` 即时推送（GPU 幽灵页 Evict 链），下轮 Rescan
  出表——非墓碑功能，属必要实现细节。
- 同名复活 guid 继承仅限本会话（Rescan 路径命中）；跨会话编辑器删除后重建
  = 新号（.meta 已同删）；外部删文件留下的被引孤儿 .meta 可续号（误删恢复
  设计，06 §2.2）。

## 改动（纯措辞，零行为变化）

- `anim.json` zh/en：`anim.set.ctx_delete_file` 去"（进墓碑）/（tombstoned）"。
- 注释同步：AnimationPanel（SetClipDir 史话/TryCreateClip 重建语义/新建集
  口径/接回注释/SaveAll 悬空因）、BuiltInPanels.h（TryCreateSet 摘要）、
  AssetBrowserPanel 头注、AssetDatabase.h `Remove()` 契约、AssetDatabase.cpp
  复活事件注——全部改为如实描述（同帧隐藏/下轮出表/已删放行）。
- smoke/uirml 等行为注释里的旧词速记（描述同帧位语义、行为准确）不在本轮
  清理面，权威记述以 06 §2.2 + AssetDatabase.h 为准。

## 验证

- 构建 + smoke-anim（zh）`adopt=YES => OK`；无字符串断言受影响（grep 无锚定）。
