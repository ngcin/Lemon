# Lemon

> 纯 2D 高性能游戏引擎：C++20 + Vulkan 1.3 内核 + C# 脚本 + Unity 风格 ECS 编辑器。
> 当前进度：**M1 渲染内核完成**（RHI/合批/图集/粒子/位图文本/相机/质量分级，验收数字见
> `../docs/DevLog.md`）。

**纯 2D 高性能游戏引擎**：C++20 + Vulkan 渲染内核、C# 脚本（CoreCLR 宿主，混合模型）、
Unity 风格 ECS 编辑器。目标品类：ARPG / 塔防 / 吸血鬼幸存者 / 增量——海量怪物，割草爽感，
不做 3D、不做重物理。

设计文档：`../docs/EngineDesign/`（00 执行总纲是入口）。

## 当前阶段：M0 技术验证 —— ✅ GO（2026-09-18）

| Spike | 内容 | 结果 |
|---|---|---|
| `spike/01-triangle` | SDL3 + Vulkan + VMA + 交换链 + 三角形 | ✅ 验证层零错误；FIFO 54fps / IMMEDIATE 832fps |
| `spike/02-sprites` | 实例化精灵压测（单 draw call） | ✅ **10 万 270fps / 30 万 189fps / 50 万 137fps** |
| `spike/03-csharp` | CoreCLR 宿主 + Collectible ALC + EnTT | ✅ 批量开销比 1.5×；EnTT 0.66ms；热重载功能可用（ALC 完全卸载遗留 M3） |

实测数据与判定：`../docs/EngineDesign/M0-Go-NoGo.md`。下一步：M1 渲染内核（RHI 完整化 + 合批 + 图集 + 粒子）。

## 构建（macOS / MoltenVK）

前置：`brew install molten-vk vulkan-loader vulkan-headers vulkan-tools glslang`

```bash
cmake --preset mac            # Release 配置（压测用）
cmake --build --preset mac    # 构建
./build/mac/spike/01-triangle/lemon-spike-triangle --frames 120   # 冒烟
./build/mac/spike/01-triangle/lemon-spike-triangle --immediate    # 极限帧率
```

Windows：安装 LunarG Vulkan SDK + Visual Studio 2022，预设 `win` 待 M1 添加。

## 代码纪律（设计文档 01）

- Vulkan/VMA 类型只出现在 `.cpp`（零泄漏，头文件用自有句柄）——spike 阶段单文件天然满足。
- 依赖只能向下：编辑器 → 内核 → 平台层。
- 新增第三方库先登记 `THIRD_PARTY.md`。
