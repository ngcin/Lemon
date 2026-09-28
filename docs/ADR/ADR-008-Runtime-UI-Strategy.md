# ADR-008：运行时 UI 方案——v1 ImGui HUD 不变，RmlUi 过 spike 验收为 v1.x 富 UI 首选

- 日期：2026-09-22
- 状态：D1 已采纳（执行中）；D2/D3 提前定形（spike-04 验收通过，正式接入时按回退条件复决）。**2026-09-28 修订**：v1.x 触发条件成立——D2 转正式执行（接入形态与回退条件不变），D3 窄 API 清单由 [ADR-014](./ADR-014-Game-UI-RmlUi-Integration.md) 的 L1 机制契约（M1–M8）重写；本 ADR 其余结论（datamodel 不依赖、字体策略、spike 验收资产）继续有效
- 影响：`06-Asset-Pipeline-Out-of-Box.md` §8、`07-Porting-Matrix.md`（RmlUi/rbfx/FreeType 行）、`08-Development-Roadmap.md` M5、`THIRD_PARTY.md`

## 背景

M5 需要游戏侧 HUD/UI。候选：①ImGui（编辑器栈内已有）；②RmlUi 6.3（RCSS/HTML 子集、MIT、纯 C++、官方带 Vulkan 社区后端）；③自研轻量保留模式 UI；④Prowl2D UGUI 式组件（对照候选，07 §1.7）。06 §8 已定分阶段策略：v1 用 ImGui HUD，富排版需求出现时升级。本 ADR 补齐 M5 决策点：v1 形态确认 + v1.x 评估结论 + 边界纪律。

2026-09-20~22 完成 `spike/04-rmlui`（官方 SDL3+Vulkan 后端拷贝改造，CPM 引入 RmlUi 6.3），三判据验收全部通过：

1. **文档渲染**——像素级验证：交换链回读 BMP（spike 自带 `Backend::RequestScreenshot`）确认深色面板/边框/文字全部落帧；
2. **中文字体**——本机 PingFang.ttc 已挪入 cryptex 不可直读，降级链 Hiragino Sans GB → STHeiti Medium → Songti 全通，中文零乱码零豆腐块；
3. **事件回调**——SDL 合成鼠标事件 → InputEventHandler → RmlUi DOM → 按钮监听器触发（自动化无人值守可回归）。

## 决策

### D1 v1（M5 发布线）：ImGui HUD，维持不变

游戏视口独立 ImGui 上下文 + 皮肤主题化；位图数字/血条走 sprite 管线；世界空间 HUD（血条/飘字）恒走 sprite 合批，不进 UI 框架（06 §8 恒定原则，yami printer/ui 分工教训）。理由：零新增依赖、栈已验证、v1 品类（VS 三选一卡片/TD 建造栏/菜单/暂停/结算）无富排版需求。

### D2 v1.x 富 UI：RmlUi 为首选，接入形态 = 自研 RenderInterface over Lemon RHI

**spike 关键事实**（详见 DevLog 2026-09-22）：

- 官方 SDL_VK 后端**不覆盖 macOS**（XCB 宏无条件定义、无条件 pNext 零值结构、未设 portability flag）。拷贝改造共 12 处（改造①~⑫，见 `spike/04-rmlui/backends/` 内注释），全部本机验证。
- **黑屏根因 ×2（上游 bug，MoltenVK/AMD dGPU 特有）**：其一，管线启用 `VK_DYNAMIC_STATE_SCISSOR` 但帧内从不调 `vkCmdSetScissor`（无裁剪帧整个命令缓冲未设）→ MoltenVK 上空矩形裁掉全部 draw（Windows 样例驱动默认非空故上游未炸；validation VUID-vkCmdDrawIndexed-None-07832）；其二，内存池 `CPU_TO_GPU` 在离散 GPU 上选中 Managed（HOST_VISIBLE 非一致）内存而全程无 `vmaFlushAllocation` → CPU 写入的顶点/索引/uniform 对 GPU 不可见（GPU 侧拷贝探针实锤全零，CPU 侧有数据）。**若正式接入走自家 RHI 则两处天然绕开**（Lemon RHI 有既定的 scissor 纪律与显式 flush/一致内存纪律）；若沿用后端拷贝路线则必须带上这两修。
- **正式接入形态**（spike + rbfx 调研综合，rbfx 报告见工作流产物）：`Rml::RenderInterface` 适配 Lemon RHI——像素空间手工正交投影 + 每帧 Discard/Commit 的共享动态 VB/IB（一次上传多次绘制）+ 真 scissor rect（相邻去重）+ 3 个 shader 变体（无纹理/ALPHAMAP/DIFFMAP + 顶点色）+ 纹理句柄包引擎对象指针并留像素备份应对设备丢失（MoltenVK surface 失效同样适用）；RmlUi 6.3 的编译几何（CompileGeometry/RenderCompiledGeometry）比 rbfx 所用 3.x/4.x 传统接口更优，静态几何可常驻 GPU 缓冲。
- **#748 浅绑定对策**：RmlUi datamodel 要求变量在文档加载前注册、无 late binding（上游 issue #748，#913 仍在跟踪）。Lemon **不依赖 RmlUi 数据绑定做状态同步**——C# 侧状态走引擎自己的 SceneOps/事件双通道（ADR-004），UI 更新用显式窄 API（见 D3），等待上游演进也不阻塞。
- **字体策略**：正式版随引擎/模板带 Noto Sans CJK（思源黑体，OFL）并以 `LoadFontFace(path, /*fallback=*/true)` 注册，保证跨平台一致；系统字体只作编辑器预览兜底。macOS 实测：PingFang.ttc 在 cryptex 不可直读，可用 Hiragino Sans GB / STHeiti Medium / Songti。
- **回退条件**（任一触发即退回自研轻量保留模式 UI，v1.x 另一既有候选）：RenderInterface 适配超 2 周未收敛；复杂 UI 帧预算超标（10k 元素级文档更新+渲染 >3ms）；上游关键 macOS 路径无修复且绕开成本失控。

### D3 脚本暴露边界（M6 定形）：不暴露 DOM，走"组件 + 窄 API"

rbfx 教训：SWIG directors 暴露整个 RmlUI 子系统 + 手工 P/Invoke 数据模型桥，维护成本高（其生成绑定甚至与 C++ 侧已不同步）。Lemon 对 C# 只暴露：`UI.LoadDocument/Unload`、元素级 `SetText/SetAttribute/GetElementById`、事件经 Events 队列回 C#（OnClick → 游戏事件）。DOM 树、RCSS、数据绑定不跨边界。

## 验收与回归资产

- `spike/04-rmlui` 保留：三判据自动化验收（合成点击 + 帧上限自退 + VERDICT 行）、`Backend::RequestScreenshot`（交换链回读 BMP，无屏幕录制权限下做像素级验证）、`LEMON_SPIKE_VALIDATION=1`（挂 Khronos validation 层，brew vulkan-loader 自带）、`LEMON_SPIKE_TEST_VERT=<spv>`（外部 SPIR-V 顶点着色器替换，管线二分排查）、几何吞吐计数（每 120 帧一行）。
- 正式接入（M6 前）复用 spike 的验收壳，只换 RenderInterface 实现。

## 后续动作（本 ADR 生效时完成）

- [x] 06 §8 v1.x 行注明 spike 已验收与接入形态
- [x] 07 矩阵：RmlUi（B 级改造源）+ rbfx（D 级对照）+ FreeType（系统依赖）登记
- [x] THIRD_PARTY.md：RmlUi 6.3（MIT）与 FreeType 2.14.3（系统 brew）行（spike 期已登记）
- [x] 01 §239 引用漂移修正（06 §9 → 06 §8）
