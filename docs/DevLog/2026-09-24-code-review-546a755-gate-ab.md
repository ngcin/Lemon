# 2026-09-24 · 审查修复批：code-review-546a755 数据安全/运行时正确性五项（F-02/03/04/08.2/10）

来源：[全栈审查报告](../Reports/2026-09-24-code-review-546a755.md)（基线 `546a755`）逐条
对标当前 HEAD 复核后，按 Gate A/B 优先级修仍真实的五项。复核结论一并留档：17 条正式
发现中 11 条属实、4 条已在此前 Engine-Review 批次修复（F-07/08.1/09/16）、2 条经核算
不成立——F-06（粒子 SWAR 打包插值 `0x00FF00FF` 在 8-bit 间隙下逐通道精确，报告把
`0xBFFFFFFF` 的 Alpha=191 误读为 Blue 通道，中点恰是正确结果仅 floor/round 差 1 LSB）
与 F-05（`ImmediateSubmit` 的 `vkQueueWaitIdle` 等待整个队列含在途帧，与帧提交共用
同一 graphics queue，声称的读写竞争不成立；真实代价只是全队列停顿）。

## 修复清单

**F-10（High）PCG32 单点区间双端死循环**：`span==1` 时 `zone=(2^32/1)*1` 截 0、
`r>=zone` 恒真，`Range(x,x)` 挂死调用线程。`Random.h`/`Pcg32.cs` 双端同语义修复：
`lo>=hi` 直接返回 lo（单点合法、逆序为契约违背的确定值，不再挂死）。合法区间
序列逐位不变（回放零扰动由金回放证明，见验收）。

**F-03（Critical）坏档巨额分配**：`SaveChannel::Decode` 按不可信 `count`
`reserve`、按不可信 `valLen` 构造 vector——十余字节坏档可声明近 4 GiB 打死进程。
改为全长度字段先验再分配：条目上限 4096、key 255B（与 Set 契约一致）、单值 4 MiB、
`valLen<=剩余字节` 先于构造；另拒重复 key 与尾随垃圾（写侧 map 语义/精确落盘不产生，
出现即损坏信号）。加载端 `LoadSaveFile` 加 16 MiB 整档上限（超限不 slurp 直接走 .bak）。

**F-02（Critical）路径删除/越界风险**：三处守卫——
- `--gen-vs-template`：`remove_all` 前验目标含 `project.lemon`+`Game/` 标记
  （仅认领本生成器产物）；任意已有目录拒绝删除。重新生成入库模板目录不受影响；
- `ProjectWizard::Create`：项目名单段目录名校验（拒 `/` `\` `:` 控制字符与
  `.`/`..`，其余含中文/空格合法——`../x` 不再越出父目录）；
- `AssetDatabase::Rename/ImportFile`：relPath containment（拒绝对路径与 `..`/`.`
  段——落点必须留在项目根内）。

**F-04（High）保存链破坏性截断写**：`EditorContext` 新增 `WriteFileAtomic`
（同目录 .tmp 全量写 + flush 显式校验 + rename 替换，失败自清 .tmp），场景保存、
Prefab Apply、存档落盘三条链统一走此口——磁盘满/中断不再截断原文件。Prefab
Revert 销毁旧树前先整档预验 JSON（坏档直接拒绝，消除"旧树已毁、新树解析失败"
不可回滚中间态）。

**F-08.2（High）C++ 销毁绕过 OnDestroy**：`IScriptBackend` 新增
`NotifyPendingDestroys`，`DestroyCommitSystem` 在 `CommitDestroys` 前调用——待销毁
队列 ∩ ScriptBox 补发销毁通知（NativeApiWindow 就位）；脚本命令路径
`ApplyStructural` 置 `kScriptFlagDestroyNotified`（ScriptBox bit1）防双通知，两路
汇合恰好一次。战斗击杀/投射物到期/越界回收等 C++ 系统销毁的托管实例与实例级订阅
（M15 自动退订挂 OnDestroy）不再残留到换域。

## 验收

- **engine-tests 13211 checks OK**（+25：Range 单点/逆序；SaveChannel roundtrip +
  六类坏档拒绝；销毁通知接线（fake backend：带脚本实体恰通知一次、无脚本跳过、
  后续步不重复）；路径 containment（越界重命名/导入拒绝且文件不动、向导拒逃逸名））；
- **script-tests 1502 checks OK**（+8：`lemon_rng_range` 双端对拍单点/逆序；
  `TestCppDestroyNotify`——C++ 侧 `scene.Destroy` 后 OnDestroy 恰好一次、实体提交、
  后续步不重复；行为数断言 9→10）；
- **ctest 3/3**；**金回放**：1 万实体 3600 帧录制 → 5 线程回放
  `replay=PASS mismatches=0`（F-10 修复对合法区间零扰动、销毁通知无 RNG 副作用的
  机械证明；avg 3.83/3.81ms 与基线同档）；
- **editor-regression full 13/13**（smoke-ui 首轮即绿）；
- **F-02 行为级验收**：`--gen-vs-template` 指向含用户数据的非 lemon 目录 → 拒绝
  删除、数据完好；指向已有模板目录 → 正常重新生成（`OK → ...` 两连跑）。

## 未修留档（审查报告其余属实项）

F-01（独立运行时/发布链，M7 域）、F-11（Windows 编译闭环，M7 域）、F-12 部分
（Vulkan 1.1 设备 feature 链与交换链 usage/alpha 按能力选择）、F-13（CI/门禁基建）、
F-14（整局含 C# 回放域，产品限制口径）、F-15（FileWatcher 防抖吞事件）、F-17（模板
csproj 绝对 SDK 路径，M7 安装器域）；F-05/F-06 复核不成立，无需动作。
