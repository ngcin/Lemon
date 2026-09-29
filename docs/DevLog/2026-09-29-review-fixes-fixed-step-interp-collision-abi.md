# 2026-09-29 复审修复批：固定步长累加器 / 插值接线 / 低 32 位碰撞告警 / API 表尺寸握手

**触发**：用户提交的 21 条代码审核复审（18 条属实、3 条部分属实）后拍板"按推荐修复"。
本批落四件现行缺陷；排期项（打包运行时 M7 / 导航 M6 / 音频 M6c / 输入抽象）不动。

## 修复 1（复审 2a）：交互 Play 模拟速率绑渲染帧率

- **根因**：`EditorApp.cpp` Play 分支每渲染帧调一次 `TickPlay(1/60)`，编辑器 vsync Fifo
  呈现 → 帧率 = 显示刷新率：144Hz 屏游戏 2.4 倍速、30fps 掉半速。全库无累加器。
- **修法**：交互 Play 改墙钟进账（`ImGui::GetIO().DeltaTime`）→ N × 1/60 出账的固定
  步长累加器；追帧上限 5 步（停顿后不快进）；空转帧仍跑 `TickPlay(0)` 保 Essential
  （销毁提交）——与暂停态既有语义同款。单步调试直接呈现步后状态（alpha=1；
  若按余账取 0 会"慢一拍"渲染步前 prev）。成员 `playAcc_`/`playAlpha_`，出 Play 清账。
- **自动化链零扰动**：`smoke*/bench*/--frames/--final/playDiag` 命中任一即走旧路径
  （每渲染帧恰一步 + alpha=1）——帧号 = tick 号，像素断言/回放口径逐位不变。
  01 §2 的独立模拟线程（真双线程 + 双缓冲提取）仍按原文档归 M4+ 计划，本修不抢跑。

## 修复 2（复审 2b）：插值机制空转（alpha 恒字面量 1.0）

- **根因**：`ViewportRenderer::RenderViewport` 调 `rm_.Extract(registry, 1.0f)`——
  RenderableManager 的 prev/cur 双缓冲 + Lerp 机制在位但恒取 cur。
- **修法**：`Render(cl, ctx, simAlpha)` 透传 `playAlpha_ = 余账/步长`（修复 1 的副产
  品）；双视口同 alpha 维持 Extract 帧内缓存跨视口共享的既有前提。非 Play 恒 1.0。
- **配套**：`RenderableManager::SnapPrev(id)`（prev←cur）——ExtractScene 新建/换代槽
  紧随 SetAll 调用，防插值开启下新实体从 Create 缺省原点 (0,0) 拉丝一帧
  （Spawner/弹幕密集场每帧都有新槽）。已知边界：编辑器提取是每渲染帧整帧推送，
  prev/cur 跨度 = 渲染帧而非 tick（追帧帧插值跨度变大但仍平滑）；模拟侧逐 tick
  写 transform 的引擎自驱路径归 01 §2 原计划。

## 修复 3（复审 3b/3c）：低 32 位键三表静默覆盖 + 注释谎言

- **根因**：ClipTable/ControllerTable/TableStore 的 `Add` 均 insert_or_assign 静默后者
  胜，仅 prefab 映射有告警（EditorContext.cpp:515）；ClipTable.h:35 注释宣称"编辑器
  建表另有去重告警"实际不存在。模板资产 GUID 为手写计数式低位（7e57 谱系）绕过
  GenerateGuid，复制 .meta 即可稳定复现同类型碰撞。
- **修法**：三个建表循环（BuildPlayClipCache/ControllerCache/TableCache）各加
  `seen` 集合，命中即 `LEMON_WARN`（新路径 + guid + id + "后者胜"语义），与 prefab
  告警同款纪律。ClipTable.h 注释随修复变为真。dense id 烘焙仍归 M7 原计划。

## 修复 4（复审 4b）：NativeApi 表无 ABI 握手

- **根因**：`lemon_api_register(NativeApi*)` + C# 侧 `Api = *api` 整拷。注释宣称"旧
  宿主 = null 判空"，但"新 SDK 配旧宿主"方向 C# 按自表宽 288B 越界读宿主 const 表
  尾部（.rodata 相邻字节）——!=null 守卫反去调垃圾指针。
- **修法**：新导出 `lemon_api_register2(NativeApi*, uint bytes)`：宿主传
  `sizeof(NativeApiVtable)`，SDK 侧 `min(bytes, sizeof) 拷贝 + 尾零`
  （`Native.RegisterSized`）。旧单参导出冻结为 36 槽表宽拷贝（`LegacyFrozenBytes
  = 288`，表再增长只走 register2）——对本仓同期旧宿主安全，跨大版本混布仍需两侧
  同更（宿主侧回落兼容已按导出名探测实现）。
- **回归**：`lemon_api_handshake_selftest` 导出（SDK 内存/截短注册/尾部槽 null 断言/
  还原全表，测试线程串行独占域）+ script-tests `TestApiHandshake` 消费。

## 验收

- 构建（mac preset）零错误；ctest 3/3（engine-tests / imgui-isolation /
  script-tests——含新增 TestApiHandshake）。
- 编辑器回归 full 15/15 PASS（`tools/editor-regression.sh full`：ctest 3/3 + 基础/
  关闭/drag/ui/资产/动画/模板/GUID/uirml/脚本/终验/场景 roundtrip）——本批最关键门：
  script-chain 的 `--play` 往返**逐字节断言**与 template-chain 3000+ 帧死亡链、
  uirml 像素断言全过，即自动化路径（每帧一步 + alpha=1）逐位不变成立。
- 复审时对审核报告的三处定性修正（1a 钩子≠违反铁律、3b 日志非误导、3a 为假设性
  风险）不改代码，仅记录于会话结论。

## 遗留与去向

- 01 §2 模拟/渲染双线程时序图、FrameArena（文档在代码无）、打包 player 目标与
  运行时宿主钩子（M7）、Navigation FlowField（M6）、音频（M6c）、输入抽象/虚拟轴
  （后续）——均按路线图原排期，不在本批。
- Events 16 格钳位（4c）与 C++/C# 三处手抄布局（4）的生成器方案未动：现有
  script-tests 布局护栏仍为唯一闸门，扩容前再议。
