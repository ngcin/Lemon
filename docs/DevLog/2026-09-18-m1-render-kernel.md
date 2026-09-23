# 2026-09-18 · M1 渲染内核完成（S0–S8，8 commit）

**环境**：macOS 24.6 / AMD Radeon RX 590 / MoltenVK api 1.3.357 / Vulkan 验证层全程开启

## M1 验收（08 §3 / 02 §9）

| 判据 | 结果 | 实测 |
|---|---|---|
| bench-mow ≥60fps（10 万精灵+5 万粒子） | ✅ | **107.0 fps**（IMMEDIATE，全可见，GPU 1.52ms）；FIFO 贴 vsync 61–62fps |
| CPU 渲染线程 ≤4ms（02 §9 = 压测 A 语境） | ✅ | **3.05ms**（1 万精灵+10 万粒子全可见：extract 1.96 + bake 1.08 + record 0.008）|
| 粒子 10 万 ≤4ms GPU | ✅ | **0.74ms**（bench-particles 存活 8.7 万时 222fps）|
| 图集切换不闪帧 | ✅ | 两图集（精灵槽0/字体槽1）三段合批，**批数恒定 4**（精灵1+粒子2+文本1），帧间零波动 |
| 设备丢失模拟自动恢复 | ✅ | 全规模注入后帧计数保持、画面恢复、**验证层零错误** |

## 分场景实测（bench-mow，IMMEDIATE）

| 场景 | fps | CPU 渲染 | GPU | 批数 |
|---|---|---|---|---|
| 10 万精灵 + 5 万粒子（全可见） | 107.0 | 6.32ms（extract 4.07 + bake 2.24 + record 0.01）| 1.52ms | 4 |
| 1 万精灵 + 10 万粒子（压测 A 构成） | 217.0 | **3.05ms** | 0.77ms | 4 |
| 10 万精灵 + 5 万粒子 + zoom 2.58（可见 15%）| 122.9 | 5.17ms | 1.35ms | 4 |

> 全可见 15 万实例 CPU 6.3ms 超出的"4ms"是压测 A（1 万实体基数）预算，非 bench-mow 判据；
> 剔除遍历本身 O(全实体)，10 万实体光遍历+插值 ≈1.9ms 起步。M2 若需要可上分块剔除。

## 单项 bench

| 程序 | 负载 | 结果 |
|---|---|---|
| rhi-smoke | 2000 实例全链路 | FIFO 60fps，GPU 0.054ms，验证层 0 错误 |
| bench-sprites | 10 万精灵完整流水线 | **152.3fps**（IMMEDIATE），渲染 CPU 3.76ms，1 批 |
| bench-particles | 10 万预算粒子 | **222.5fps**，GPU 0.741ms，2 批 |

M0 基线对照：spike-02 裸实例化 270fps（仅写 24B/实例）；新流水线 152fps = 多付提取-双缓冲-
插值-剔除-排序-合批全链路代价，15 万实例总 CPU（模拟+渲染）9.3ms 仍余 40% 帧预算。

## 性能优化记录（保留过程，数字为优化前后实测）

1. **批分组摘要碰撞**（粒子 5k 切 672 批）：排序键只放批键哈希 16 位摘要，两混合模式
   摘要碰撞 → 相邻不同键反复切批 → 改完整 64 位 key.hash 分组 → 2 批，record 2.07→0.13ms。
2. **粒子提取桶化**：std::sort O(n log n) 4.9ms → 计数桶 O(n) 1.6ms（粒子层内 order 恒 0）。
3. **sin/cos 查找表**（Core/Math FastSin/FastCos，4096 项 + 线性插值，误差 <1e-3）：
   bake 3.12→2.07ms（15 万实例仿射是热路径）。
4. **单遍提取 + 搬运分桶**：两遍遍历（重算剔除/插值）→ 单遍生成 + 槽缓存 + 56B 纯搬运。
5. **精灵排序免除**：键桶化后 order 全零时桶内池序即稳定序（bench 场景免 std::sort）。

## 事故与修复（验证层/看门狗战果）

| # | 事故 | 根因 | 修复 |
|---|---|---|---|
| 1 | **整机卡死**（bench-sprites 首跑 GPU 877ms/帧，WindowServer 拿不到交换链图像）| bench scale 语义错：传了像素直径 4–14 作"精灵尺寸倍率"→ 每精灵 256–896px → 5000× 过采样 | 尺寸语义对齐（÷64）+ **全 bench 帧时间看门狗**（EMA>250ms 自动中止）+ 探路纪律（小 N→FIFO→放大）|
| 2 | 设备丢失后验证层报 invalid VkBuffer 写描述符 | 恢复回调里旧句柄"看似有效"跳过重建 | 恢复回调先作废全部句柄再按需重建 |
| 3 | 时间戳池未重置 / UPDATE_AFTER_BIND 布局标志缺失 / 提交缺 vkEndCommandBuffer 等 | — | 验证层逐条抓出修复；两条新教训见下 |

**新增本机坑（供后续里程碑）**：
- UPDATE_AFTER_BIND 绑定要求 set layout 挂 `UPDATE_AFTER_BIND_POOL` 位 + SSBO 绑定需
  `descriptorBindingStorageBufferUpdateAfterBind` 特性（采样器无独立 update-after-bind 位）。
- 动态渲染下交换链获取屏障 oldLayout 必须写 UNDEFINED（PRESENT_SRC 只在首帧为真）。
- 时间戳池创建后必须 `vkResetQueryPool` 全量重置一次才能用。

## 产出清单（commit 829003b..HEAD）

- `Engine/`：Core(Math/Log) · Platform(Window) · Renderer(RHI/Atlas/Renderable/SpriteBatcher/
  Particles/BitmapFont/Camera2D/Quality + Shaders)
- `Samples/`：rhi-smoke · bench-sprites · bench-particles · bench-mow（验收场常驻回归）
- `tests/`：167 项纯逻辑断言（数学/批键/UV/相机/质量/粒子池/字体）
- 设计文档：02 分册新增 §11 M1 实测节；本日志

## 遗留（不阻塞 M2）

- bench-mow 全可见 CPU 6.3ms 的进一步压缩（静态 UV 尾巴跨帧复用、SoA 化提取）按需在 M2 性能
  周期做；当前预算语境已达标。
- 路径 B（顶点展开/chunk 烘焙）按计划 M6 Tilemap 时实现。
- TTF→位图图集离线生成器随 M5 资产管线；M1 用内置 5×7 像素字模。

---
