# 新建脚本流落地（A 件）+ M8.5 脚本作者路径立项

- 日期：2026-10-14（用户实报"添加脚本只能从已有类型里选，脚本也不能引用 prefab 等资源"→ 方案讨论三分法 A/②/③）
- 决策（用户拍板）：**A 立即做**；**②③ 立项为独立段 M8.5、前置 M9，开发时点后续再拍**（08 路线图 §0 已加行 + 文首注记）
- 结论先行：A 件 = Inspector「添加脚本」菜单顶部**「新建脚本…」**对话框（**二轮重设计**，用户参照 Godot New Script 图拍板）：名称 + 存储位置（可手输 + `...` 浏览钮走 FilePicker 目录模式，默认 `Game/`，会话内记忆）+ 只读预览行；支持子目录组织（`Game/UI`、`Game/Role`…按需创建），命名空间随目录推导。引擎内核零改动，纯编辑器层。

## 1. A 件实现要点（二轮重设计后）

| 项 | 决策 | 依据 |
|---|---|---|
| 对话框形态 | 名称 / 位置（手输 + `...` 浏览）/ 只读预览 三行左标签对齐 + 右下 取消/创建（创建按钮无效态置灰） | 用户参照图（Godot New Script）拍板 |
| 目录浏览 | 面板私有 `scriptPicker_.OpenDir`（Game 根内；快捷目录 = Game）+ **模态不叠加**回路（开 picker 先关对话框，选定/取消回填重开——向导 WizardDir 同款） | FilePicker 目录模式既有（M4.6 §4-2）；ImGui 模态叠加避坑 |
| 子目录组织 | 位置支持 `Game/UI` 等（按需 `create_directories`）；SDK csproj 递归 glob 免改工程；`scriptWatcher_` 监视整棵 Game/ | 用户："脚本要分路径放，UI/role 等形式" |
| 命名空间 | 随目录推导（`Game/UI` → `namespace UI`；段名净化，非法段跳过；根 = 全局与既有约定一致） | C# 惯例；注册行走全限定 `Lemon.Behaviours.Register<UI.X>()` |
| 注册 | `GameMain.Configure` **末尾**追加（最后一个 `Register<` 之后，缩进同款） | **typeId = 注册序（跨帧稳定契约）**——中插平移既有 typeId |
| 校验链 | 宿主 → C# 标识符（≤48）→ 路径域（词法归一后须在 Game/ 内——外侧 .cs 不进 glob 无 watcher）→ 类型短名重名（Behaviours 注册表身份 = `t.Name`，跨命名空间同名短名同样歧义）→ 目标目录同名 .cs | 重名类炸编译 / 越界静默失效都不可接受 |
| 兜底 | GameMain 无 `Register<` 行/缺失 = 告警提示手动注册（脚本文件不回滚） | 手写工程不静默改坏 |

改动面：`InspectorPanel.cpp`（对话框 + `CreateNewScript(app,name,locRel)` + PathWithin/FolderNamespaceOf 辅助）、`BuiltInPanels.h`（scriptPicker_ + 三态成员）、`EditorApp.h`（`FindGameProject` private→public，纯查询）、i18n 中英各 +8 key。自动挂载未做（等换域完成需跨域状态机，留 M8.5 顺手项）。

## 1b. 二轮验证（算法面 + 编译面）

- 独立测试 15 断言全过（/tmp 一次性夹具，未入库）：PathWithin 五态（内/自身/外/`..` 逃逸/绝对路径）、命名空间推导五态（根空/单级/嵌套/空格净化/数字头段跳过）、模板渲染（namespace + 缩进骨架）、GameMain 插行（末尾追加 + 缩进一致 + 既有顺序不动）。
- **生成物真编译**：namespaced 模板 + GameMain 注册行在真实 Lemon.SDK 引用下 `dotnet build` **0 错误**。
- 三回归绿：smoke-drag / smoke-ui（15 项）/ smoke-anim。
- 真人走查待用户（对话框 → 浏览选目录 → 预览核对 → 创建 → "Game/ 编译"日志 → 菜单出现新类型）。

## 1c. 三轮补全（用户首走查实报，同日）

实报两项：① 创建后未自动挂载（要等编译完再手动挂，体验断裂）；② Game/ 脚本在 Asset 浏览器不可见（须去系统文件管理器找）。

- **自动挂载**：创建成功 → InspectorPanel `pendingAttach{Entity,Class,Deadline}`；每帧轮询 `ScriptTypeNames()`，新类型出现即走菜单同款挂载（guid 反查 / `autoExpandScript_` / 结构轨 Undo / Play 路由复用 `AttachScript` 入口闸）。挂载目标 = 存储句柄不依赖当前选中；15s 超时（编译失败）红字提示改手动；实体销毁静默放弃。轮询不挂换域钩子（时序在 EditorApp 侧，面板侧轮询更简）。
- **浏览器 Game 脚本区**：根级（currentDir_ 空）网格下方只读平铺列表（分隔标题 + BulletText 相对路径，filter_ 同过滤）；2s 节流重扫 Game/**/*.cs（obj/bin/点开头排除与扫描同规）。**不改资产管线**——06 §1"Game 非资产源"（meta/manifest 不进脚本工程）维持，脚本 guid 化与选择器交互登记随 M8.5 ③。
- 回归：smoke-drag / smoke-ui（15 项）/ smoke-anim 全绿；构建零警告。

## 3. M8.5 立项登记（②③，时点待拍）

- **② 脚本字段 Inspector 化**：`[ShowInInspector]` 元数据通道（05 §5.159 设计落地，当前零实现）+ vtable 协议扩展 + 类型族双向 marshal + 场景 `scripts[].fields` 序列化 + Undo 属性轨 + 热重载保值。估 4–6 批。
- **③ 资产引用字段**：AssetRef GUID 字段类型 + 对象选择器（FilePicker/资产浏览器弹窗，拖放候选池既有）；Spawner/Shooter 手填 GUID 低 32 位收口；SDK `Instantiate` GUID 重载；sprite 引用 GUID 化同族并段。估 1–2 批，骑在 ② 上。
- 字段元数据协议 = 协议级拍板，开工时需 ADR；出口验收载体 = M9 TD 模板作者路径（塔属性 = prefab + 脚本字段）。

## 1d. 四轮：自动挂载超时根因——同路径换装镜像缓存坑（同日）

用户二走查实报：`GemTest` 创建后日志"编译+换装+重装配 2900ms（类型 11 个）"但自动挂载超时。**编译没失败**——三层实证定位：

1. **现场核对**：GameMain.cs 注册行在（12 个 `Register<`）；`.lemon/bin/Game.dll`（mtime 与创建同分）`strings` 含 GemTest；编辑器完整 stderr 日志（`~/.zcode/cli/exec/...`）无任何 C# 侧错误（无名字冲突/无装配失败）。
2. **独立探针**（/tmp，未入库）：ScriptAlc 同款 ALC 复刻加载该 dll + 反射 Configure → **12 个类型全注册**——dll 与 C# 注册逻辑均无辜。
3. **活体复现**：对运行中编辑器 touch GemTest.cs / 再加 AutoProbe+注册行，每次换装"类型 N 个"恒为 11（= 12:52 启动装载值）——**类型表冻结在首载，换装从未装载新镜像**。
4. **机制复现**（合成 A/B/C 两版 dll）：旧 ALC 存续（泄漏不卸）+ 同路径文件替换 + 新 ALC `LoadFromAssemblyPath` → **返回旧镜像**（A,B 而非 A,B,C；`same-asm-object=False` = 新壳旧码）。唯一路径副本 / `LoadFromStream` 两对照组均取到新镜像。

根因：本 runtime（.NET 10.0.12 macOS）旧脚本 ALC 泄漏不回收（ADR-010 A 线已知项）**钉住 CoreCLR 路径级装配镜像缓存**——`dotnet build` 覆写同路径 dll 后，新域从该路径加载命中缓存旧镜像，Configure 跑的是旧代码 → 新注册类型永不出现 → 自动挂载轮询 15s 超时（且不止新类型：**方法体编辑同样从未生效**，此前被"编译+换装"成功日志掩盖）。

修复（`ScriptHost.cpp` `HotReloadAssembly`）：换装前复制到**唯一路径副本**（`Game.hr<N>.dll`，pdb 同名随行，复制失败回退原路径不劣于修复前）；跨会话清理在 `InitScriptHostFrom` 开头（本会话未装载副本时目录内 `.hr*.dll` 遗留全可删，删失败静默下次再清——Windows 映射中不可删）。C# 侧零改动（无协议/version bump）。

验证：编辑器活体（12:52 工程启动 12 类型 → 注入 AutoProbe2 换装 **13 类型**（修复前必为 12）→ 回退再换装 12 类型，增删双向）；quick 6/6 + **full 26/26**（含 script-chain / final acceptance hot-reload / template-chain）。

副产物认知：用户工程现场 `~/.lemon/recent.json` 定位（Embrace/game）；`Game/ 编译 Nms` 启动构建会覆写已映射 dll（POSIX 下旧 inode 存续，无害）。
