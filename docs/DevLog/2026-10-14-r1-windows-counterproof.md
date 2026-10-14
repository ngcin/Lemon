# M8 批⑥ #R1 —— Windows 同构对照(反证)完成:曲线平,mac 特有性坐实

- 日期:2026-10-14 凌晨收表(10-13 深夜开工)
- 输入:[#R1 定位报告](./2026-10-13-r1-soak-leak-diagnosis.md)(引擎自有内存面全干净;增长 = 64KB VM 块在 macOS SkyLight 侧)——本条目 = 该报告 §5 选项 C 的执行结果
- 结论先行:**同款完整 Grass 场景在 Windows 上跑满 61 分钟,私有内存斜率 +1.5~+2.7MB/h(R²<0.2,噪声级零斜率)vs mac 同场景 +44.5MB/h(R²=1.0000 完美线性)——差 15~30 倍且 Windows 无任何线性形态。mac 特有的系统侧行为坐实,#R1 按环境豁免收口的证据链闭合(待用户最终确认)。**

## 1. 实验链路(全程 Parallels VM 自动化)

- **环境**:Parallels "Windows 11" VM(M7a 批⑦ 遗留工程机:VS2026 + VulkanSDK + dotnet + cpm-cache 全在);操作通道 = `prlctl exec`(VM 无 SSH);文件经宿主临时 HTTP 传输。
- **构建**:HEAD `eab87ea` 源码 tar 传输 + `win-share` preset 全新构建;**顺手修复一处 HEAD 上的 MSVC 编译阻断**——`CoreCLRHost.cpp:116` `FreeLibrary(api.lib)` 补 `static_cast<HMODULE>`(mac clang 放行、MSVC 拒绝;mac 侧工作树同步修复,随下次提交入库)。
- **测试条件对齐 mac 复现**:svr-min 项目(与 mac 复现场同构)+ Grass 完整场景(双全功能脚本)+ **ai 用户交互会话启动**(schtasks 以 INTERACTIVE 主体注册,窗口有焦点)+ `--vsync --frames 0` @fps 51-82(lavapipe 软渲染,FIFO 节奏与 mac 不同,不影响内存判读)。
- **采样**:SYSTEM 会话 PowerShell 每 60s 记 `WorkingSet64/PrivateMemorySize64` → CSV。**VM 内自治脚本**(墙钟间隔 >95s 判污染弃轮重开)+ mac 侧守望(挂起自动 resume)——应对实验期间 mac 重启/VM 四连挂起的现实(挂起免疫架构,详见 §3 教训)。

## 2. 结果(58 点、61.1 分钟全窗;中段一次 5 分钟挂起空洞,分两段各 28 分钟干净窗)

| 指标 | 段1(t=0-28min) | 段2(t=33-61min) | mac 同场景参照 |
|---|---|---|---|
| 私有内存 PM(判读主指标)| **+1.55MB/h(R²=0.18 噪声)** | **+2.69MB/h(R²=0.19 噪声)** | **+44.5MB/h(R²=1.0000)** |
| 工作集 WS | 系统置换修剪波动(20-360MB),不构成信号 | 同左 | 单调线性 |

首末 PM:624.1 → 625.3MB(61 分钟 +1.2MB)。**Windows 上无泄漏形态;两段斜率均与零不可区分。** 原始曲线归档 `docs/Reports/win-mem.csv`。

## 3. 过程教训(VM 实验防复踩)

1. **Parallels 关窗 = 挂起 VM**,挂起即实验污染(挂起恢复后墙钟跳变,Windows 时钟停走可检测);实验要么人守窗口最小化,要么用"墙钟跳变检测 + 自治重开"的免疫架构(本条目定型)。
2. **mac 重启清 /tmp**:实验期 mac 被重启一次,/tmp 全清(10-12 soak 首例同款)——跨小时实验的关键产物别只放 /tmp。
3. **SYSTEM 会话无法跑 GUI 焦点应用**(present 直接失败,RHI OTHER(-5))——交互测试用 `schtasks /ru INTERACTIVE /it` 注册到登录会话;计划任务 XML 的 `LogonType` 元素在 schtasks 导入时校验苛刻,命令行 `/ru INTERACTIVE` 更省事。
4. `prlctl exec` 传大文件:base64 过长会被截断且静默失败——走宿主 HTTP 下载最稳。

## 4. 收口(2026-10-14 用户拍板)

证据链(#R1 全案):引擎 malloc 堆两拍零增长 → GC 托管对象 +2.8KB/s 运行时内部 → 增长实体 = 64KB VM 块(lldb 断点证实分配现场在 SkyLight 私有框架内、`leaks`=0 有引用)→ 触发 = 双全功能脚本组合 × 窗口焦点/可见性(场景级二分矩阵)→ **Windows 同构 61 分钟零斜率(本条目)**。**判定:macOS 系统侧行为(疑似 SkyLight 缺陷被特定调用模式触发),引擎侧无泄漏——#R1 按"环境豁免"结案(08 债表已勾销),M8 出口判据"soak 2h 内存曲线平"按引擎侧口径 ✅(win 轮佐证);Apple 侧根因归因(Instruments,需完整 Xcode)留作可选项不阻塞。** 附带产出:CoreCLRHost.cpp MSVC cast 修复(win 构建阻断,工作树随下次提交入库);VM 实验防挂起四教训见 §3。
