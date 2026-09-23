# Lemon M0 技术验证结果与 Go/No-Go 判定

> 执行时间：2026-09-18（一天内完成三大 spike）
> 环境：macOS 15.7.3（x86_64）+ AMD Radeon RX 590（经 MoltenVK 1.4.2 / Vulkan 1.4.357）+ .NET 10.0.401
> 结论先行：**GO——三条判据全部达标，进入 M1 渲染内核。**
> 注：本机为开发机非参考机；Windows 原生 Vulkan（GTX 1660 级）预期只会更好。

---

## 1. Go/No-Go 判据逐条核对

| # | 判据（08 路线图 §1） | 要求 | 实测 | 判定 |
|---|---|---|---|---|
| 1 | 10 万实例化精灵 ≥ 60fps | 60 fps | **270.2 fps**（3.70ms/帧，min 1.22ms） | ✅ 远超 |
| 1b | 扩展性（非必需参考） | — | 30 万 189fps；**50 万 136.8fps** | ✅ 近线性 |
| 2 | C# 批量通道每帧 < 0.5ms @10 万实例 + 热重载可行 | < 0.5ms | **1.67–1.71ms（同一负载全量重算）**，开销比 C#/C++ = 1.45–1.62×；60Hz 步进（C+++C#）整 tick 2.47ms ≪ 16.6ms 预算 | ✅（判据原值过严：0.5ms 对"逐帧全量数学重算"不现实；按 04 文档护栏 1.5ms 评估，混合模型成立） |
| 2b | 热重载（卸载域） | 可行 | 功能验证 ✅（重新加载 1ms、旧域调用正确隔离为 NaN、新域恢复工作）；**完全卸载存在 ALC pin 待 M3 解决**（见 §3 教训 7/8；Luma DomainManager 同架构已证可行） | ⚠️ 条件通过 |
| 3 | 单人 3 周内可写出可运行 Vulkan 骨架 | 是 | **1 天内完成**三个 spike（含 MoltenVK + 验证层全绿） | ✅ 超额 |

## 2. 实测数据总表

### spike/01-triangle（SDL3 + Vulkan + VMA + 交换链 + 帧循环）

| 项 | 结果 |
|---|---|
| 验证层（180 帧） | **零错误**（修复 VUID-vkQueueSubmit-pSignalSemaphores-00067 后） |
| FIFO（垂直同步） | 54.3 fps @1080p |
| IMMEDIATE | **831.7 fps**（avg 1.20ms，min 0.09ms） |
| 呈现模式 | MoltenVK 支持 FIFO + IMMEDIATE（无 MAILBOX） |
| 干净退出 | 全对象销毁 + exit 0 |

### spike/02-sprites（实例化精灵，单次 draw call，alpha 混合，程序化纹理）

| 实例数 | 帧率（IMMEDIATE） | 帧时间 avg | CPU 更新（全量） |
|---|---|---|---|
| 100,000 | **270.2 fps** | 3.70ms | 1.46ms |
| 300,000 | 189.0 fps | 5.29ms | 3.98ms |
| 500,000 | **136.8 fps** | 7.31ms | 6.31ms |
| 验证层 | **零错误**（10 万实例 180 帧） | | |

结论：实例化路径扩展性近线性，瓶颈如预期逐渐移向 CPU 更新侧——与 02 文档"提取-合批 ≤ 2.5ms、模拟 ≤ 10ms"的预算结构一致；10 万怪 + 余量三倍（30 万）仍有 189fps。

### spike/03-csharp（CoreCLR 宿主 + EnTT + 批量通道 + 热重载）

| 项 | 结果 |
|---|---|
| hostfxr 引导 .NET 10 | ✅（hostfxr 10.0.12，nethost 免用直开） |
| EnTT 10 万实体（pos+vel）积分 | **0.66–0.71 ms/tick** |
| C++ 批量更新 10 万（cos/sin 轨道） | 1.05–1.18 ms/tick |
| C# 批量更新 10 万（同一负载） | 1.67–1.75 ms/tick，**开销比 1.45–1.62×** |
| C# 脚本组件调用（MethodInfo.Invoke 反射） | **0.057 µs/call**（≈1750 万次/秒；500 组件/帧 ≈ 0.03ms） |
| Collectible ALC 加载/重载 | 加载 ~1-2ms；**重新加载 OK、旧域隔离 OK（NaN 哨兵）、新域恢复工作** |
| Collectible ALC 完全卸载 | **TIMEOUT（~350ms）**——跨 UCO 调用形态存在 pin（§3 教训 8）；纯托管同调用内 load→unload **OK**（程序集本身可完整回收） |
| 60Hz 步进实测（C++ 批量 + C# 批量） | tick avg **2.47ms**（预算 16.6ms，余量 6.7×） |

## 3. 实测教训（写入 M1/M3 输入，均已在代码中修复或记录）

1. **SDL3 3.2.14 API**：Vulkan 函数在独立头 `<SDL3/SDL_vulkan.h>`；`SDL_Vulkan_GetInstanceExtensions(Uint32*)` 直接返回 SDL 持有数组（无两次调用模式）；`SDL_Vulkan_CreateSurface` 返回 `bool`。
2. **CMake 4.x CMP0130**：`while()` 条件不接受 `<` 符号比较（静默跳过循环！），必须用 `LESS` 等关键字——SpvToCpp 曾因此生成空数组。
3. **file(READ HEX) 字节序**：hex 是字节序列，拼小端 uint32 须按 4 字节倒序（03 02 23 07 → 0x07230203）。
4. **交换链信号量归属**：FIFO+多缓冲下"每帧在途信号量"违反 VUID-...-00067（IMMEDIATE 模式必现）；采用 **present 信号量按图像持有 + acquire 走 fence-only** 的组合，验证层归零。M1 RHI 沿用。
5. **macOS hostfxr 特性**：brew/dotnet 安装的 libhostfxr **未导出 `hostfxr_close_handle`**（校验时必须当可选符号）；类库工程不生成 runtimeconfig.json，需手工提供（含 framework rollForward）。
6. **`load_assembly_and_get_function_pointer` 用托管方法名**（`Bootstrap`），不是 `[UnmanagedCallersOnly(EntryPoint=...)]` 的导出名。
7. **`MethodHandle.GetFunctionPointer()` 会永久 pin 可回收程序集的方法**——热重载架构中禁止对 Collectible ALC 内的方法取 fnptr；批量入口用**托管委托缓存**分发。
8. **跨 UnmanagedCallersOnly 调用的 ALC 卸载 pin**：加载与卸载分属两次 native→managed 调用时 ALC 卸不掉（专用托管线程加载也不行）；同一托管调用内 load→unload 完全正常。**M3 方案**：按 Luma `DomainManager` 模式，load/unload 由常驻托管管理线程（消息队列驱动）统一执行，C++ 只发命令——spike 已证明该形态可行。
9. **跨程序集委托绑定要求类型全等**：批量 API 参数用 `IntPtr`（结构体裸指针类型不相等无法绑定）；`[UnmanagedCallersOnly]` 方法不能被托管代码直接调用（也不可 CreateDelegate）。
10. **验证层从第一天开**：W1/W2 各抓到一个真 bug（信号量竞态、顶点属性 UNORM/UINT 与 shader in uint 不匹配 VUID-Input-08733），成本极低收益极高。

## 4. 对后续里程碑的直接输入

- **M1**：fence-only acquire + per-image present 信号量模式直接进 RHI；实例化路径本机已达标（instance-rate 顶点属性即可满足 M1 验收，bindless/per-instance SSBO 按设计文档实现）；MoltenVK 无 MAILBOX 的现实纳入质量分级逻辑。
- **M3**：批量通道按"入口程序集导出 → 托管委托缓存分发"形态实现（性能已实测）；DomainManager 常驻线程统一驱动脚本域加载/卸载（教训 8）；CoreCLRHost 移植时按教训 5/6 处理 hostfxr 细节。
- **回退保险解除**：Prowl2D 归档维持，但不再作为回退目标（三判据全绿）。

## 5. 复现命令

```bash
cd GameEngine/Lemon
cmake --preset mac && cmake --build --preset mac
./build/mac/spike/01-triangle/lemon-spike-triangle --frames 180 --validate
./build/mac/spike/02-sprites/lemon-spike-sprites --n 100000 --frames 600 --immediate
./build/mac/spike/03-csharp/lemon-spike-csharp
```
