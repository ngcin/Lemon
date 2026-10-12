# Manual — 用户手册（文档站源）

面向引擎**使用者**（用 Lemon 做游戏的开发者）的操作手册。设计依据、实测数字、
决策记录一律不在此复载——链回 `../EngineDesign/`、`../ADR/`、`../DevLog/`
（批⑦ D6 纪律：手册稳定优先，数字是 DevLog 的事）。

## 规约

- 页命名 `NN-slug.md`，数字前缀 = 站点导航序（目录树即索引，同 README 总规约）。
- 侧栏分组在 `tools/build-manual.mjs` 的 `GROUPS` 表登记。
- 受限 markdown 子集（标题/表格/两级列表/围栏代码/引用/水平线）；**不识别的语法
  构建硬失败**——白名单见生成器头注，先扩子集再写内容。
- 本 `README.md` 是区维护说明，不进站。

## 构建与查看

```sh
./tools/build-manual.sh          # → build/manual/（零依赖 node，产物已 gitignore）
open build/manual/index.html     # 浏览器 file:// 直开
```

站内死链 / 图片缺失 / 越子集语法 = 构建失败（exit 1）。

## 页清单（v1 十三页，M8 批⑦ B 组）

| 页 | 主题 | 状态 |
|---|---|---|
| 01-intro | 定位与快速跑起来 | 编写中 |
| 02-getting-started | 新建项目/模板/运行/出包 | 待写 |
| 03-editor | 编辑器布局与工作流 | 待写 |
| 04-components | 组件目录全表 | 待写 |
| 05-scripting | C# 脚本模型与 SDK 总览 | 待写 |
| 06-scenes | 场景与多场景 | 待写 |
| 07-assets | 资产管线 | 待写 |
| 08-lighting | 2D 光照 | 编写中 |
| 09-fx | 后处理链 | 待写 |
| 10-ui | RmlUi 界面 | 待写 |
| 11-audio | 音频 | 待写 |
| 12-packaging | 出包与独立运行时 | 待写 |
| 13-debug | 调试旗标与常见坑 | 待写 |
