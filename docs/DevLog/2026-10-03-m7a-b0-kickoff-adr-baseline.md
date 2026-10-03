# M7a 批⓪ 开工：ADR-016 草稿 + 开工基线刷新

2026-10-03 · M7a 批⓪（[批文件](../Plans/M7a/2026-10-03-b0-kickoff-adr.md)）——用户指令「开工 M7a，从批⓪ 的 ADR-016 草稿 + 基线刷新做起」。

## ADR-016 草稿落盘

[ADR-016-Standalone-Runtime-And-Minimal-Packager.md](../ADR/ADR-016-Standalone-Runtime-And-Minimal-Packager.md)（草案态）：

- 决策段 M1–M10：平台顺序（D1 已拍板）/ lemon-game 入口与 CLI + D8 渲染合成 / Engine/Assets 资产层 / Play 装配下沉六件表 / **`.baked` 家族全类型口径**（音频 LBA1 既有、图集 LAT1 草案字节表——批⑥ 定稿追记、结构化 JSON 直拷声明）/ packager 形态与出包布局 / dotnet self-contained / entryScene 字段 / 存档便携落位。
- D2–D8 以「建议」标注，拍板项表齐备待用户；拍板后状态行转「已采纳」。
- 范围边界与风险段与 M7a.md §6/§7 对齐（不扩 scope）。

## 开工基线刷新（构建 = 172975a review 批③ 尾，增量零改动）

M7a 规划落位（a7db601）后插入五轮 review 修复（2fce9f0→172975a）——计划 §1 #11 的收官基线已过时，全量重跑：

| 指标 | M6c 收官 | 本次 | 备注 |
|---|---|---|---|
| 回归 full | 17 步 | **17/17**（第三轮） | 前两轮各 16/17 抖动位轮换，见下 |
| ctest | 3/3 | 3/3 | |
| engine-tests | 34036 | **34073** | +37 review 三批新测 |
| script-tests | 1771 | **1776** | +5（#71 等） |
| bench-survivor 1000 帧 | fps=82 | **fps=84** | alive=10493、尖峰>25ms=0、anim/fx 饱和全过 |
| vtable | 46 | **47** | review #71 `audioPausedGet` 表尾（零重录口径内） |
| 系统 / 组件 | 20 / id 至 31 | 20 / id 至 31 | InstallDefaultSystems 与目录 32 断言双核 |

「vtable/组件 id/系统序零变动 → 金回放零重录」的 M7a 搬运纪律前提**成立**（vtable +1 属表尾零调用口径）。

### 回归抖动定性（两轮 16/17，失败位轮换）

1. 第一轮：`script-chain` FAIL 于 `overlay-visible sel=3(≥20)`——单独复跑 ×2 绿且 sel=190 稳定。
2. 第二轮：`smoke-ui` FAIL 于汇总行 `save=0`——单独复跑绿（save=1）。
3. 失败位轮换 + 单独确定性绿 = 满载抖动（M6c 批④ 起既有先例）；第三轮 full 17/17 收口。
4. 工具坑登记：full 回归经 `\| tail` 管道调用会 mask 脚本退出码（管道取尾命令）——判定以 summary 行为准，CI 化时（批⑧）注意裸调用。

## 现场

- 用户 svr-test WIP 未跟踪文件在场（Animations/*.override 等）——本批不触碰。
- 仓库根游离 `Assets/` 目录（未跟踪）非本批产物，登记待用户处置。
- 修复与文档均未提交（待用户指令）。

## 下一步

D2–D8 拍板 → ADR-016 转正 → entryScene 字段落地（编辑器解析/写入 + svr-test 与模板回填）→ 批出口复验（回归 17 步 + entryScene 回显）。
