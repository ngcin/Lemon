# 资产管线

核心机制一句话：**引用走 GUID，导入走后台**。重命名/移动文件不破坏任何引用。

## GUID 与 .meta

每个资产文件旁伴随同名 `.meta`（JSON）：

```json
{ "guid": "b0348bc4e21a598a", "type": "sprite",
  "importer": { "slice": "grid", "cell": [32, 32], "pivot": [0.5, 0.5] },
  "hash": "sha256:9f2c...", "importedAt": 874312400 }
```

- 场景/Prefab/组件槽只存 GUID；`.meta` 随文件移动。
- `.lemon/manifest.json` = 项目级索引（`guid → path/type/依赖/hash`），导入器生成物，
  随状态目录 gitignore。编辑器启动按 manifest 做一致性体检（孤儿 meta / GUID 冲突 /
  缺失引用红字报告）。
- 编辑器改精灵表后重导入，切片 GUID 稳定匹配不漂移；引用漂移自愈 = 删
  `.lemon/manifest.json` 重开（确定性扫描重记账）。
- 删除资产 = 条目出表 + 孤儿 `.meta` 清扫（仍被引用的保留 + 红字提示恢复源文件）。

## 支持的资产类型

| 类型 | 源格式 | 说明 |
|---|---|---|
| sprite | png/jpg | 单页网格切片先行（`.meta` 声明 cell/frames） |
| audio | wav/ogg/mp3/flac | PCM16 预解码，烘焙入 .baked；`.meta` 记循环点 |
| font | ttf/otf | 位图字体页烘焙（LBF1 .baked；词表/字号/描边可配） |
| clip2d | json（.clip） | 帧动画（AnimationEditor 产出/编辑） |
| animset | json（.ani） | 动画集：段名 → clip 引用清单 |
| controller | json（.controller） | 动画状态机（双击进 AnimController 工作台编辑；`Assets/Controllers/` 为共享库约定落点） |
| rml/rcss | — | RmlUi 界面文档与样式表（双击装载，rcss 热重载） |
| particles | json | 粒子发射器预设 |
| data/curve | json（.asset） | Team 表/波次表/曲线等数据资产 |

导入器在编辑器后台线程池跑；`FileWatcher` 监听变更触发增量导入 → 按依赖图通知
（贴图变更 → 受影响视口刷新），Play 中的资产热替换即时生效。

## Prefab

- 扩展名 `.prefab`，JSON 结构同场景实体段；支持嵌套（Prefab 引 Prefab）与
  实例覆盖（`overrides` 字段级深合并）。
- Inspector 组件头栏：Apply（实例改动回写源）/ Revert / Break / Select；
  改过字段蓝标。
- 运行时 `Instantiate` 走装配期一次解析的缓存，无 JSON 热路径。

## 图集与 .baked

- **编辑器态**：一 sprite 文件一纹理页（bindless 槽 + 静态 spriteId → 页/UV 表），
  简单可调试。
- **发布态**：packager 现烤图集 `.baked`（LAT1 容器：shelf 确定性装箱 + RAW RGBA），
  运行时零 PNG 解码；音频（LBA1）与字体（LBF1）同属 .baked 家族。
- 出包时 sprite 源与 `.meta` 不入包（详见 [出包](12-packaging.md)）。

## 给脚本用的资产引用

C# 侧资产引用统一用 **GUID 十六进制串**（如音频 `Audio.Play("b0348bc4e21a598a")`）；
GUID 在 AssetBrowser / manifest 里可查。场景内的组件资产槽（精灵/clip/音频/rml）
则是下拉选择或拖入，作者不需要碰裸 GUID。

> 设计依据：[06 资产分册](../EngineDesign/06-Asset-Pipeline-Out-of-Box.md)
> （GUID schema/导入器表/Prefab/图集/.baked 注记全量）。
