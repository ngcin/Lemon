# 2026-09-24 编辑器后修：最近场景同名重复（路径异形拼写绕过精确去重）

## 现象（用户发现）

File → 最近场景 菜单出现多个 `Main.scene` 条目，指向同一文件；多次打开后持续累积，
挤占 5 条历史上限。`recent-scenes.json` 实测 4 条中 3 条是同一 Main.scene 的不同拼写：

```
/Users/.../Lemon/demo/svr-test/Scenes/Main.scene      ← 文件选择器（绝对路径）
demo/svr-test/Scenes/Main.scene                       ← 相对路径（--scene 相对 CWD）
/Users/.../Lemon/./demo/svr-test/Scenes/Main.scene    ← 冗余 /./ 段（argv 原样透传）
```

## 根因

`OpenScene` 把原始传入字符串直接记 `scenePath_` + 最近列表——`std::ifstream` 对相对
路径 / `/./` 段都能解析，功能不坏但拼写各异；而 `RecordRecentScene` 与
`LoadRecentScenes` 的去重都是**字符串精确相等**，异形拼写各成条目。路径形态随入口漂移：
文件选择器给干净绝对路径；命令行 `--scene/--project` 原样透传 argv；向导/自动打开拼
`projectDir + "/Scenes/Main.scene"`（继承 projectDir 拼写）。

连带：`MenuOpenRecentScene` 的"当前场景"短路 `path == ctx_.ScenePath()` 同为字符串
比较——当前场景换拼写再点会整场重开而非短路。

## 修复（EditorContext.cpp 单文件）

新增匿名命名空间助手 `CanonicalPath()`（`std::filesystem::weakly_canonical`，失败/空串
兜底原串；悬空路径按最长存在前缀 + 词法归一不失败），两处接入：

1. `OpenScene`：打开成功后 `scenePath_ = CanonicalPath(path)`（文件刚打开必然存在），
   `RecordRecentScene(scenePath_)`——场景路径、最近记录、保存默认路径、选择器起始目录、
   当前场景短路自此统一规范形态；
2. `LoadRecentScenes`：加载时对存量条目逐条 `CanonicalPath` 再去重（保序留首见）——
   存量脏档开一次项目即自动清洗，无需手工改 JSON。

回归防线（机械可复验）：无头启动传畸形拼写
`--scene "$PWD/./demo/svr-test/./Scenes/../Scenes/Main.scene"`，断言落盘 JSON 收敛为
2 条规范路径（Main.scene + Perf10k.scene）。本次实测通过。

## 验证（2026-09-24）

- `cmake --build --preset mac --target lemon-editor` 零告警；
- 畸形拼写无头 60 帧：`recent-scenes.json` 4 条（3 重）→ 2 条规范路径；日志
  "场景已打开"即规范路径；
- `tools/editor-regression.sh quick`：**6/6**（ctest 3/3 + smoke 5 项）。
