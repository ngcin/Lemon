# M7a 批④：GameEntry + lemon-game 竖切（2026-10-04）

[批文件](../Plans/M7a/2026-10-04-b4-gameentry-vertical.md) · [M7a.md](../Plans/M7a/M7a.md) · [ADR-016](../ADR/ADR-016-Standalone-Runtime-And-Minimal-Packager.md)

## 事件

ADR-005「同源双入口」兑现：`Engine/Entry/GameEntry.cpp` + `add_executable(lemon-game)`
落地——不链 editor-core/ImGui 的独立游戏可执行，编辑器与游戏共用批②③④ 下沉的同一套
装配件。CLI `--project/--scene/--frames/--smoke/--validate`；entry dll 运行期定位
（exe 旁 `runtime/` → 构建树回退，替换 `LEMON_SCRIPT_DIR` 编译期宏单源依赖）；
pipeline cache 显式传 `<root>/.lemon/game/`；字体链 `<root>/Fonts/` → 引擎源树回退。

随批三件下沉/勘误：

1. **PlayCaches**：三缓存构建（clip+动画集/controller/table）自 EditorContext 下沉引擎
   （日志逐字节保留），`PlayCacheSource` 接口 + 编辑器/运行时双适配；
2. **GameFx**：Play 表现层段（血条/飘字）自 ViewportRenderer 下沉，编辑器/lemon-game
   两薄壳——防第三套实现；
3. **AssetTypeName 漏 Audio 分支勘误**（M6c 起既有）：manifest 把音频全记 "generic"，
   AssetIndex 快路径信串则 AudioMount 漏装全部音频（svr 9 件/模板 7 件实证）。修 =
   写入面补分支 + 读取面 generic→TypeOf 扩展名自愈 + IndexedEntry 补 importer 字段
   （.meta 小 IO，manifest 不扩）；单测四锁（34210→34220）。

另：TextureStore::RebuildAll（设备丢失重建，AssetGpuCache 同款）；回归残留守卫扩
lemon-game。

## 实测

- **模板夹具 900 帧端到端**（smoke 点击直灌驱动：150 定位 `btn-start` → 151 down →
  152 up）：`alive=39 visible=25 batches=6 ticks=900 uidoc=6 audio=7 scriptSys=1
  contractErr=0 hud=1 => OK`，fps=1010（Immediate）；Gem.prefab 运行时实例化日志在链
  （HookInstantiate = 编辑器 InstantiatePrefabAsset 的 Play 态等价）。
- **svr-test 拷贝**：clip 5/动画集 7/状态机 1/表 4 全建、uidoc 4/4、音频 9（流式 2）、
  交互档 Fifo 启动零错误（相机跟随命中）。
- **git clean 判据模拟**：删 manifest(+.bak) → 回退扫描 36 条 + guid 归一 → smoke 复跑 OK。
- **回归 full 18/18**（game-smoke = 第 18 步，首跑绿）；ctest 3/3；单测 34220。
- 实现期事故一例：imgui-isolation 扫中 GameEntry.cpp——注释里"ImGuiKey"字样命中
  `ImGui[A-Z]` 正则，改措辞过（隔离断言工作正常的正面证据）。

## 发现与决策

- 模板/项目入口场景都是纯 UI+Flow（零 SpriteRenderer，玩法实体运行时生成）→
  game-smoke 以**点击驱动进局**为断言核心（hudShown + visible>0），反而把整条竖切
  （UI 点击→事件→C# 流程→prefab spawn→提取→直渲染）变成机器面端到端；
- RtUi/Cards 呈现是编辑器 ImGui 叠层特权——lemon-game 不携带（模板已迁 .rml）；
- D8 A 案（双 pass 直渲染 swapchain）实测可行：SpriteBatcher colorFormat 与
  UiSubsystem rtFormat 均显式传 `SwapchainFormat()`，`ui.Render` 在动态渲染块内
  sprite Record 之后、EndPass 之前（离屏块同款唯一合法插入点）；
- **装配顺序纪律（--validate 实抓）**：设备创建须先于 ScriptHost——CoreCLR 装载会
  改写进程 dyld 回退搜索行为，此后 vkCreateInstance 的验证层 dlopen 解析路径受扰
  （首版 GameEntry 按宿主先行的直觉排序即撞此坑；重排后 + 本机坑绕行验证干净。
  编辑器自始设备先行故无此症；ADR-016 M2 装配序原文即 Window → Device 在前）。

## --validate 实测与本机坑

- 绕行 `DYLD_FALLBACK_LIBRARY_PATH=/usr/local/lib` 后：300 帧 smoke + 验证层启用 +
  **零 VALIDATION-ERROR/WARN**——直渲染路径干净（ADR-016 风险 #1 关账）；
- 本机坑（机器级）：brew 验证层清单 `library_path` 为裸文件名，新版 dyld 默认回退表
  已不含 `/usr/local/lib` → lemon-game 的层 dlopen 稳定失败（`VK_ERROR_LAYER_NOT_PRESENT`，
  5 行最小复现件同症；lemon-editor 同机稳定可过、机制未完全定位）。产品路径（无验证层）
  零影响；已记 AGENTS.md 本机坑。

## 待真人验收

`lemon-game --project demo/svr-test` 四屏全流程零 C++ / 窗口 resize 渲染复原 /
`--validate` 抽查（带本机坑绕行变量）。

