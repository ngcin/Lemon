# Lemon

> 纯 2D 高性能游戏引擎：C++20 + Vulkan 1.3 内核 + C# 脚本 + Unity 风格 ECS 编辑器。
> 当前进度：**M0–M5 代码面完成**（M5 于 2026-09-23 收口，里程碑与验收数字见
> `docs/EngineDesign/08-Development-Roadmap.md`，事件流水见 `docs/DevLog/`）。

**纯 2D 高性能游戏引擎**：C++20 + Vulkan 渲染内核、C# 脚本（CoreCLR 宿主，混合模型）、
Unity 风格 ECS 编辑器。目标品类：ARPG / 塔防 / 吸血鬼幸存者 / 增量——海量怪物，割草爽感，
不做 3D、不做重物理。

文档：地图与规约见 [`docs/README.md`](./docs/README.md)；设计基准入口 `docs/EngineDesign/00-Executive-Summary.md`（00 总纲 → 01 架构 → 02–09 分册）。

## 里程碑：M0 技术验证 —— ✅ GO（2026-09-18；后续 M1–M5 见 08 路线图）

| Spike | 内容 | 结果 |
|---|---|---|
| `spike/01-triangle` | SDL3 + Vulkan + VMA + 交换链 + 三角形 | ✅ 验证层零错误；FIFO 54fps / IMMEDIATE 832fps |
| `spike/02-sprites` | 实例化精灵压测（单 draw call） | ✅ **10 万 270fps / 30 万 189fps / 50 万 137fps** |
| `spike/03-csharp` | CoreCLR 宿主 + Collectible ALC + EnTT | ✅ 批量开销比 1.5×；EnTT 0.66ms；热重载功能可用（ALC 完全卸载遗留 M3） |

实测数据与判定：`docs/Reports/2026-09-18-m0-go-no-go.md`。

## 构建（macOS / MoltenVK）

前置：`brew install molten-vk vulkan-loader vulkan-headers vulkan-tools glslang`

```bash
cmake --preset mac            # Release 配置（压测用）
cmake --build --preset mac    # 构建
./build/mac/spike/01-triangle/lemon-spike-triangle --frames 120   # 冒烟
./build/mac/spike/01-triangle/lemon-spike-triangle --immediate    # 极限帧率
```

Windows：**构建指南见 [`docs/EngineDesign/Windows-Build-Guide.md`](./docs/EngineDesign/Windows-Build-Guide.md)**（安装 → 构建 → 测试 → 无 GPU lavapipe → 打包 → 故障排查，全流程实测走通：MSVC 全量构建 + ctest 4/4 + 出包真人验收，2026-10-06）。速览：VS2022/2026（C++ 桌面开发）+ .NET SDK 10 + Vulkan SDK + git → `cmake --preset win && cmake --build --preset win`。

## 测试（方法与判读详见 `../docs/EngineDesign/09-Testing.md`）

```bash
./build/mac/tests/lemon-tests                                           # 单测 167 项
./build/mac/Samples/rhi-smoke/lemon-rhi-smoke --frames 300 --validate   # RHI 冒烟（含 mips/管线缓存）
./build/mac/Samples/bench-mow/lemon-bench-mow --immediate               # M1 验收场
./build/mac/Samples/bench-mow/lemon-bench-mow --resize-test --validate  # resize 压测
./build/mac/Samples/bench-mow/lemon-bench-mow --device-loss 300 --validate --frames 900  # 设备丢失
```

纪律：Vulkan 改动默认带 `--validate` 自测并确认零错误；新负载先小 N + FIFO 再放大，
正式数字才用 `--immediate`；bench 内置看门狗（EMA>250ms 自动中止）。

## 代码纪律（设计文档 01）

- Vulkan/VMA 类型只出现在 `.cpp`（零泄漏，头文件用自有句柄）——spike 阶段单文件天然满足。
- 依赖只能向下：编辑器 → 内核 → 平台层。
- 新增第三方库先登记 `THIRD_PARTY.md`。
