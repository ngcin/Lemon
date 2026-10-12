# 新建项目

## 项目长什么样

一个 Lemon 项目就是一个普通目录（升级引擎永不触碰它）：

```text
MyGame/
├── project.lemon        # 项目清单（引擎版本锚点/入口场景/项目 GUID）
├── Assets/              # 资产源文件 + 同名 .meta（GUID 与导入设置）
├── Scenes/              # 场景文件（.scene）
├── Prefabs/             # Prefab 资产（.prefab）
├── Game/                # C# 脚本工程（标准 csproj）
├── Data/                # Team 表/波次/本地化等数据资产（.asset）
├── .lemon/              # 编辑器状态（manifest/缓存/布局/存档，gitignore）
└── Builds/              # 打包输出（gitignore）
```

`project.lemon` 是 JSON 清单：

```json
{
  "schemaVersion": 1,
  "name": "MyGame",
  "engineVersion": "0.5.0-m5",
  "guid": "8f3a...项目唯一号（存档隔离键）",
  "entryScene": "Scenes/MainMenu.scene"
}
```

## 新建项目向导

编辑器启动的新建项目向导：选模板 → 选路径 → 复制模板 → 生成新项目 GUID →
直接打开进 Play，全程零配置。向导会自动把模板 `Game.csproj` 里的 SDK 引用
（HintPath）重锚到本机构建产物。

| 模板 | 内容 |
|---|---|
| blank | 空场景 + 最小脚本工程 + 种子资产 |
| vs-survivor | 完整吸血鬼幸存者：竞技场、8 向移动、三武器、经验宝石与升级三选一、波次导演、Boss 计时、HUD、死亡结算（含夜战场与昼夜循环） |

## vs-survivor 模板与生成器

`Templates/vs-survivor/` 是一个完整可玩项目（仓库自带）。两个入口：

- **向导复制**（做游戏）：如上，复制后自由改造。
- **生成器重生成**（开发工具）：改引擎或模板玩法后，一条命令重生成模板产物——

```sh
./build/mac/Editor/lemon-editor --gen-vs-template Templates/vs-survivor
```

机械验收旗标 `--smoke-template`（向导复制 → 编译 → Play → HUD/波次/升级卡断言）
在回归里盯着这条链不断。

## 打开与运行

```sh
# 编辑器打开项目
./build/mac/Editor/lemon-editor --project path/to/MyGame

# 不带编辑器直接跑（独立运行时入口）
./build/mac/Engine/Entry/lemon-game --project path/to/MyGame

# 指定场景 + 自动化帧数（测试用）
./build/mac/Engine/Entry/lemon-game --project path/to/MyGame --scene Scenes/Grass.scene --frames 600
```

游戏脚本改完在编辑器里**热重载**（约 2 秒生效，不用重启）；`Game/` 工程由
编辑器/打包器负责编译，lemon-game 只消费编译产物（`.lemon/bin/`）。

## 接下来

- 摆实体、挂组件 → [编辑器](03-editor.md) 与 [组件目录](04-components.md)
- 写玩法 → [C# 脚本](05-scripting.md)
- 发布游戏 → [出包](12-packaging.md)

> 布局与向导流程的设计依据：[06 资产分册 §1/§7](../EngineDesign/06-Asset-Pipeline-Out-of-Box.md)。
