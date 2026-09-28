# M6a 批③a：RmlUi 渲染地基落地（Engine/Ui + RenderInterface over RHI）

- 日期：2026-09-28
- 批文件：[Plans/M6a/2026-09-28-b3a-rmlui-renderer.md](../Plans/M6a/2026-09-28-b3a-rmlui-renderer.md)；决策：[ADR-014](../ADR/ADR-014-Game-UI-RmlUi-Integration.md)（ADR-008 D2 接入形态执行、D3 由 M1–M8 契约重写）
- 性质：代码面完成（验收 1/2/3 ✅；真人截图走查待用户；T7 文本输入微 spike 留 ③a 收尾项、③c 开工前必须过）

## 交付

| 件 | 内容 |
|---|---|
| `Engine/Renderer/RmlUiBackend.{h,cpp}` | 自研 `Rml::RenderInterface` over 引擎 Vulkan 设备：动态渲染管线（`VkPipelineRenderingCreateInfo`，RGBA8Unorm=gameRT）、Rml::Vertex 零转换三属性顶点输入、预乘混合、自有 push constant 72B（mvp+translate，SetTransform CPU 折叠）、8MB 单池 + VmaVirtualBlock 子分配（CompileGeometry 常驻/3 帧延迟释放）、纹理 = VMA GPU-only + staging 一次性提交 + 每纹理懒建 combined-sampler 描述符集（128 上限）+ 像素备份应对设备丢失、帧协议 BeginFrame（投影 + 钉全幅 scissor，改造⑧）/EndFrame（全池 vmaFlushAllocation，改造⑫） |
| `Engine/Ui/UiSubsystem.{h,cpp}` | RmlUi Context 生命周期 + SystemInterface（日志转引擎 LEMON_* / steady_clock）+ 内存文档加载/显隐 + `Render(cl,w,h)`（尺寸变化→SetDimensions）+ 设备丢失回调（ReleaseFontResources + 按底稿重载文档→几何重编译）；系统字体链 Hiragino→STHeiti→Songti（③b 换 Noto） |
| `Engine/Renderer/Shaders/rmlui.{vert,_color.frag,_texture.frag}` | 2 frag 变体（6.3 GenerateTexture 恒 RGBA 预乘，无 ALPHAMAP——ADR-008"3 变体"按实测收敛） |
| RHI 内部桥 | `Device::GetInternalBridge()`（void* 形态：device/physical/allocator/queue）+ `InternalImmediateSubmit`（C 回调）——Renderer 兄弟后端专用通道（`NativeCommandBuffer` 豁免口同族，注释已扩消费者） |
| 接线 | ViewportRenderer：`GameUiLayerFn` 挂接点 = gameRT 块内 `batcher.Record` 之后 `EndPass` 之前（**唯一合法插入点**——离屏块 loadOp 恒 CLEAR）+ `GameRenderTarget()` 回读口；EditorApp：生命周期（Init/Update（World 步进后）/Shutdown）+ `--smoke-uirml` 独立进 Play 分支 + VERDICT 裁决 |
| 冒烟 | `--smoke-uirml`：内存文档三要素（面板/标题/正文）→ gameRT 像素断言 + 字体/文档探针；独立裁决链不并 editor-smoke 门（两模式捕获 RT 不同） |

## 实测数字

- 构建：`cmake --build --preset mac` 全目标绿（RmlUi 转正式链入 lemon-engine；RMLUI_SHELL OFF——spike 用本地后端拷贝不受影响）。
- `--smoke-uirml --frames 180 --validate`：**panel=102652(>3000) title=791(>20) body=374(>20) font=Hiragino Sans GB => OK**，**验证层零错误**，exit 0。
- 回归 `tools/editor-regression.sh` full：**14/14 PASS**（首跑 13/14——`--save-scene` 早退路径触发 UiSubsystem 析构断言崩溃；修复后复跑全绿）。
- 截图：`--screenshot` 1600×900 已出（真人走查待用户）。

## 实现期发现（已折入批文件"实现期发现"节）

1. **RmlUi 6.3 RCSS font-family 不支持逗号回退列表**——整串当一个族名（日志实锤 `font face 'Hiragino Sans GB, Heiti SC, Songti SC' ... not found`）；冒烟文档按 `LoadedFontFamily()` 单值注入，③b Noto 唯一正字后消失。
2. **Pimpl 内联 `= default` 陷阱**——构造函数 `= default` 留在头文件会在使用方 TU 实例化 `~unique_ptr<Impl>`（构造异常路径析构已构成员），Impl 不完整即编译错；五件套声明在头、定义在 cpp。
3. **playTest 进 Play 与 smoke 门绑定**（`EditorApp.cpp` `(playTest||finalTest)&&smoke` 块）——smoke-uirml 独立裁决链需独立进 Play（同 PlayBlockedByScripts 判据）。
4. **描述符池须 `FREE_DESCRIPTOR_SET_BIT`**（纹理逐集释放；验证层 VUID-00312 实抓——验证层第一天就开纪律再次回本）。
5. **Run() 早退路径多出口**（--save-scene/--smoke-guid/看门狗）——UiSubsystem 析构改防御性收尾（警告 + 就地 Shutdown；成员声明序保证此时 device 仍存活）。

## 遗余

- T7 文本输入微 spike（spike/04 扩 `<input>` 三判据）：③c 开工前必须过（ADR-014 D4）。
- LoadTexture（文档内 `<img>`）暂返回 0 + 一次性告警——③b 资产桥接上。
- clip-mask/层合成/filter：v1 红线外（一次性告警 no-op）。
- smoke-uirml 尚未入回归脚本第 15 步——随 ③c 契约断言一起接（批文件已登记）。
