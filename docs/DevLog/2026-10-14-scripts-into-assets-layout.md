# 2026-10-14 脚本工程并入 Assets（布局归一，Unity 式）

## 背景

用户走查新脚本流后拍板：Game/ 与脚本要"跟 Unity 一样进 Assets"（只读平铺区不够）。
设计讨论结论 = 两步走，本文 = 第一步落地；第二步（.cs guid 化 + 拖拽挂载 + 场景
引用 guid+className 双写）随 M8.5 ③。当日同会话先修了同路径换装镜像缓存坑
（见 [1d](./2026-10-14-new-script-flow-and-m85-registration.md)）——本项以其为前提。

## 决策

- 布局：顶层 `Game/` → `Assets/Scripts/`（csproj 随迁；SDK 递归 glob 不变）。
- 身份：**挂载仍按类名**（场景引用零迁移）；guid 化随 M8.5 ③。
- `.cs` 进资产树 = 一等条目（图标/过滤/重命名/删除/移动全放开——`AssetType::Script`
  分类与浏览器渲染本就齐备，等于"接入即得"）；`.csproj/.sln` 不入库不给 guid
  （构建系统文件非内容资产）。
- 旧项目迁移：开项目时 `ProjectWizard::MigrateLegacyGameDir`——`Game/` 有 csproj
  且 `Assets/Scripts/` 不存在才动（清 obj/bin——restore 产物含旧绝对路径——后整树
  rename）；并存/散目录 = 告警交人。`EditorApp::ScriptProjectDir()` 新布局优先、
  顶层 `Game/` 兜底（迁移失败不阻断）。

## 改动面

- `EditorAppScriptReload.cpp`：`ScriptProjectDir()` + `FindGameProject`/
  `ScriptSourceChanged` 改用；`EditorApp.h` 声明。
- `EditorAppScripts.cpp`：开项目先迁移再启动 `scriptWatcher_`（新路径）。
- `ProjectWizard.h/.cpp`：`MigrateLegacyGameDir` 实现；blank 骨架目录清单、
  `WriteGameProject`、`ReanchorSdkHintPath` 路径改新布局。
- `VsTemplateGen.cpp`：模板再生成写新布局；remove_all 前置标记检查双布局兼容。
- `Templates/vs-survivor`：`Game/` → `Assets/Scripts/`（丢弃模板内 stale obj）。
- `EditorAppChrome.cpp`（菜单旧版新建脚本对话框）、`EditorAppFinal.cpp`（终验
  播种/探针路径）、`InspectorPanel.cpp`（新建脚本对话框默认位置/快捷目录标签/
  脏检查字面量）。
- `AssetBrowserPanel`：根级只读 Game 区退役（成员/渲染/i18n key `assets.game_section`
  中英双删）；`AssetDatabase.cpp`：扫描跳过 `.csproj/.sln`；`SkipDirTop` 保留
  `Game` 排除作旧布局兜底（注释更新）。
- `tools/editor-regression.sh`：game-smoke 夹具 csproj 路径。
- 文档：06 §1 目录树 + 布局归一注记。

## 验证状态

- 构建零警告。**回归与活体迁移验证未跑**（用户当轮叫停"先不跑验证了"）——
  full 26 项（template-chain/script-chain/final acceptance 均踩新布局）与
  Embrace 真项目迁移走查待补；下次动编辑器批次前应先补跑。
