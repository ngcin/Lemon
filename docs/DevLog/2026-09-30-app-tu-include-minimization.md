# App 族 TU include 瘦身（减脂系列收尾清偿）

- 日期：2026-09-30（批④-2 同日续；批③ 起登记的低优项清偿）
- 性质：编译期卫生——减脂系列各新 TU（批②起）机械复制 EditorApp.cpp 全量
  49 行包含块，本批按"体区符号命中才保留"逐 TU 收敛。零行为变化（只删不加
  的 include 不改变生成代码；编译器为最终裁判）。

## 改动

- 11 个 App 族 TU（EditorApp/Actions/Bench/Chrome/Final/ScriptReload/Scripts/
  Smoke/SmokeTpl/SmokeUirml/UiBridge）：**501 → 197 个 include（-61%）**，
  幅度 EditorAppScriptReload 45→8 至 EditorAppSmoke 45→30 不等（197 = 最小化
  后 195 + 三处补回 3 − Bench 重复头去重 1）。
- 方法：标记表驱动（每个 include ↔ 体区特征正则：组件头 = ecs:: 类型名、
  工具头 = namespace 前缀、imgui 系 = API 形态、std 头 = std:: 符号），
  未知头保守保留；头区连续空行收敛。

## 编译器抓住的三个缺口（标记表盲区，全部还原/修正）

1. `EditorAppBench.cpp` 漏 `UiPanelProbe/HierarchyPanelProbe`（BuiltInPanels.h
   声明；探针符号无类型名特征）→ 恢复该 include + 注记。
2. `EditorAppSmoke.cpp` 注入链走 `scenePanel_->`/`assetPanel_->` 成员调用，
   标记表只认 `SceneViewPanel` 类型名 → 恢复 BuiltInPanels.h（完整类型）。
3. `EditorAppSmoke.cpp` **原本就没有直接包含 App/EditorApp.h**——定义
   EditorApp 成员函数的 TU 靠 Panels/BuiltInPanels.h 传递拿到类定义
   （IWYU 反模式，减脂前继承下来的）。本批补显式类头。

## 顺手处置（登记）

- `EditorAppBench.cpp` 头部**重复的** `#include "App/EditorApp.h"`×2
  （批③c-6 建 TU 时手误，include 防重复故一直无害）去重。

## 事故记录（诚实账）

最小化脚本首版有低级 bug：写盘只拼了过滤后的头区、丢了 `lines[ns:]` 体区
——11 个 TU 被截成光杆包含块。编译"全过"（空 TU 无从报错）但链接缺
ClosePanel/RescanAssets 等全部成员函数定义 + EditorApp.cpp 报静态变量未
使用，当场定位。因批④+④-2 已提交（5285b8f），`git checkout` 整体恢复、
零已提交面损伤、无坏二进制落地（链接失败未产出）。修正后脚本加防呆断言：
写盘文本必须含 namespace 行、必须以 `} // namespace lemon::editor` 收尾、
行数账必须 == 原行数 − 删 include 数 − 收敛空行数。

## 验证

- 构建：净（唯一 warning 为 EditorAppSmoke.cpp 旧有 unused-parameter，
  行号 1804→1787 系删 include 移位）。
- 回归：`tools/editor-regression.sh full` 16/16 PASS（含 imgui-isolation
  ctest——ImGui 头只进 Editor/ 的纪律面不受影响：只删未增）。
