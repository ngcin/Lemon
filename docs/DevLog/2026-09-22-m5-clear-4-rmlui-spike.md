# 2026-09-22 M5 清障④：RmlUi spike 三判据验收通过（ADR-008 落笔）

`spike/04-rmlui`：CPM 引入 RmlUi 6.3（tag 锁定，MIT，THIRD_PARTY 已登记），官方
SDL3+Vulkan 后端**拷贝改造**（官方推荐姿势；vendored glad loader 不可替换系统
vulkan.h——Renderer_VK 全建立在 gladLoader 函数指针体系上）。

**mac 兼容改造 12 处**（`backends/` 内 `[Lemon spike 改造N]` 注释可查）：
① 删 XCB 宏（官方 RmlUi_Include_Vulkan.h 对 UNIX 无条件定义，macOS 上 xcb.h 不存在）
② 非 debug 时 pNext 置空（原版无条件挂零值 validation features 结构）
③ portability flag：SDL3 给出 VK_KHR_portability_enumeration 扩展但原版不设 flag →
   loader 视 MoltenVK 未 opt-in → vkCreateInstance -9（**VK_ERROR_INCOMPATIBLE_DRIVER**
   而非 layer 错；此前误读 -9 为 LAYER_NOT_PRESENT 走了弯路）
④ 设备特性先查支持集再裁剪（原版硬报 wideLines/pipelineStatisticsQuery 等，MoltenVK
   不全支持 → vkCreateDevice FEATURE_NOT_PRESENT 静默失败 → 后续 Invalid device）
⑤ Backend::GetWindowPixelDensity（合成输入坐标换算）
⑥ 交换链 usage 加 TRANSFER_SRC（回读）
⑦ RequestScreenshot：render pass 后 blit/copy 到 staging、present 后等 fence 落盘 BMP
⑧ **黑屏根因①**：帧首 vkCmdSetScissor 钉默认全视口（管线启 DYNAMIC_SCISSOR 而原版
   只在 RmlUi 开/关裁剪时才设——无裁剪帧动态 scissor 未定义，MoltenVK 空矩形裁光
   全部 draw；validation VUID-vkCmdDrawIndexed-None-07832；Windows 样例驱动默认非空
   故上游未炸）
⑨ 设备扩展启用 VK_KHR_portability_subset（VUID-04451）
⑩ 内存池 HOST_ACCESS 标志（对齐上传管理器）
⑪ LEMON_SPIKE_TEST_VERT 外部 SPIR-V 替换钩子（二分排查资产）
⑫ **黑屏根因②**：内存池 EndFrame 提交前 vmaFlushAllocation——CPU_TO_GPU 在离散
   GPU（AMD dGPU+MoltenVK）选中 Managed 内存，CPU memcpy 的顶点/索引/uniform 对 GPU
   不可见（**GPU 侧拷贝探针实锤全零 vs CPU 侧有数据**）；Windows BAR 一致内存上无此
   问题故上游从未暴露。纹理 staging 同步补 flush。

**排障方法论沉淀**（spike 验收壳全部可复用，正式接入只换 RenderInterface）：
交换链回读做像素级验证（宿主无屏幕录制权限时 `screencapture -l` 拿不到窗口内容，
全屏截图也只有壁纸）；`LEMON_SPIKE_VALIDATION=1` 挂 Khronos validation（brew
vulkan-loader 自带 explicit layer，Release 下 assert 全关时 GPU 侧错误唯一可见渠道）；
合成 SDL 鼠标事件全链路自动化（坐标 = RmlUi dp ÷ pixelDensity）；clear 改色 + 半屏
vkCmdClearAttachments 隔离 pass/draw 层；红绿实验 + 探针二分定位到内存可见性。

**验收三判据 PASS**（像素级）：① 文档渲染（回读 BMP 背景色 #1a1c22 精确命中、面板/
标题/按钮全落帧）② 中文字体（PingFang 在 cryptex 不可直读 → Hiragino Sans GB 降级链，
中文零乱码）③ 事件回调（合成点击 → CLICK 'start'）。VERDICT 行自动化判定，900 帧
自退无人值守可回归。

**RCSS 侧两个坑**（test.rml 修正）：`font-family` 是 string 解析器**不支持逗号回退
列表**（整串当一个家族名）；块布局**不支持 margin:auto**（仅 flex 支持）——改用
`display:flex; align-items:center` 居中。另：RML 解析器把 `<body>` 内容直接挂文档根
（DOM 里无 body 元素，`body` 选择器仍匹配文档根做背景）。

**rbfx 调研**（GLM-5.3-Flash 三路并行浏览，报告卡片在案）：rbfx（Urho3D MIT 分支）
的 RmlRenderer = 传统接口 + 共享 DynamicVB/IB + DrawCommandQueue 命令缓冲 + 3 shader
变体 + 真 scissor rect，全引擎抽象零裸后端 API，唯一后端特判是 OpenGL 渲到纹理翻转；
其 vendored RmlUi 是 4.x 世代（CMake 版本号 3.3 已腐烂三年——vendored 目录必须本地
CHANGELOG 记录同步 commit）；编辑器自身用 ImGui、RmlUI 只被"游戏运行"复用；C# 暴露
走"组件子类化 + 手工 P/Invoke 数据模型桥"（维护成本高）。**结论进 ADR-008**：
正式接入自研 RenderInterface over Lemon RHI、6.3 编译几何优于 rbfx 老接口、C# 只暴露
窄 API 不透 DOM。

**文档批**：ADR-008 落笔（`docs/ADR/ADR-008-Runtime-UI-Strategy.md`：D1 v1 ImGui
不变 / D2 RmlUi 首选 + 回退条件 + #748 浅绑定对策 / D3 脚本暴露边界）；06 §8 表注
明 spike 已验收；07 矩阵 M5 行加 RmlUi/rbfx/FreeType、红线表加 rbfx（MIT，D 级对照）；
01 §239 引用漂移修正（06 §9→§8）+ ADR-008 状态更新。

**回归**：spike VERDICT PASS（清理诊断后复跑）；全树 `ninja` no work to do（spike
独立目标，引擎/测试零改）；清理临时诊断（红/绿 clear、池探针、一次性 dump），保留
验收资产（RequestScreenshot/validation 开关/吞吐计数/测试着色器钩子）。
