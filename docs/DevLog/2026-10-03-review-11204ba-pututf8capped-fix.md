# 批① 11204ba 提交后复审：行块编码回退误剪完整码点（#22/#60 回归）修复

2026-10-03 01:30 · 复审 `11204ba`（代码审核 2fce9f0 修复批①）发现并修复

## 事故：无条件回退把完整多字节尾字符剪成悬空导引字节

批① #22/#60 给 `GameUI.PutStr8`/`PutBytes` 加的"截断退码点边界"回退循环
**无条件执行**：

```csharp
while (n > 0 && (s_arena[at + n - 1] & 0xC0) == 0x80) n--;
```

UTF-8 多字节序列的末字节本身就是延续字节（0x80–0xBF）——只要串以非 ASCII
字符结尾（中文 key/label 的常态），该循环就把末字符的延续字节全部剪掉、停在
导引字节上（不再满足 `==0x80`），**留一个悬空导引字节 = 非法 UTF-8**。
dotnet 探针实证：`"生命值"`（9B，cap=55 远未触顶）→ 7B `'生命' + E5`。
批③冒烟夹具的中文值（`"移速+10%"` 等）全部以 ASCII 结尾，恰好躲过——这是
门格全绿仍带病的原因（"非 ASCII key 机器覆盖"在批① DevLog 明记遗留）。

## 副产发现：GetBytes 目标不足是抛异常，不是截断

`Encoding.UTF8.GetBytes(ReadOnlySpan<char>, Span<byte>)` 目标 span 装不下
整串编码时**抛 ArgumentException**（`ThrowBytesOverflow`），并无"写满即止"
语义——`PutStr8`/`PutBytes` 注释宣称的"越界截断"在超限路径上从未成立
（key>255B / 值>64KB 时域调用异常，非静默截断）。此为旧代码即有的行为
（批①未引入也未修复），本次一并修正语义。

## 修复（单一代码路径共享）

- `PutUtf8Capped(s, cap)` 新助手：常路径（`GetByteCount ≤ cap`）判容直写
  （零分配、ASCII 输出与旧版逐字节相同）；真截断路径整串编码后回退——先退
  延续字节、**停在导引字节上则连导引一并退**（留导引即非法 UTF-8）。回退
  仅在截断发生时做，完整串不再误剪。探针验证 300B→cap255 截到 252B/84 字符
  合法边界。
- `PutStr8`/`PutBytes` 改为薄委托；行块 u8/u16 长度语义（字节、边界对齐）
  不变，`UiBridge.h` 契约零变化。

## 机器覆盖（批①遗留项"非 ASCII 行块"本轮交付）

- `TestScript.UiRefill`（帧1 SetItems）：值改 CJK 结尾（`"移速加成"`/
  `"磁力提升"`，各 12B）、第二行 key 换 CJK（`"选项乙"`，9B）。`opt0`
  保留（smoke-uirml 点击注入/SetClass 寻址依赖）；行数/字段结构不变
  （`itemsN==2`、`contract==1` 零扰动）。字段名仍 ASCII（"label"）——
  CJK 字段名受模板字段集约束，须改 .rml 夹具才能覆盖，继续遗留。
- `tests/script/main.cpp` 行块解码收紧为**字节精确对拍**（key/值均
  `len==strlen && memcmp`；原仅查 `valLen∈(0,32)`，坏串漏网）。
- 阴性验证：只落测试侧先行跑 → `FAIL ui: 字段值 CJK 字节精确`（红，
  精确命中剪尾）；落修复后复绿。

## 门格

- `script-tests` **1775** checks OK（+4，原 1771）。
- `ctest` 3/3（engine-tests / imgui-isolation / script-tests）。
- `smoke-uirml --frames 500`（非脚本）`=> OK`；带 `--script` 三连
  `ev=c1r4 => OK`。**登记**：带脚本变体存在偶发 `ev=c0` 抖动——重建后
  首跑两次观察到（基线 stash 与修复版各一次，逐位同判），后续连跑全绿；
  属冷启动时序类既有问题（同 #28 会话环境族），非本轮改动引入。

## 排除项（复审其余 26 文件）

- `JsonEscape` 的 `dump()` 默认 strict 处理器对非法 UTF-8 会抛
  type_error.316——**不可达**：nlohmann v3.11.3 `parse` 本身拒绝非法
  UTF-8（探针实证 parse_error.101 "ill-formed UTF-8 byte"），名字只能经
  parse/SDL 输入进入，均保证合法。不改。
- 设备丢失链（#1/#24）、并行销毁分桶（#2，JobSystem 块界 grain 对齐已核）、
  ActiveScene 七函数（#4）、SaveEntityTree 剪除（#10）、TargetBoard 防御
  （#19）、烤制上限（#21）、roundtrip 预验（#5/#30）等：复审通过，无发现。

修复未提交（待用户指令）；涉及 `GameUI.cs` / `TestScript.cs` /
`tests/script/main.cpp` 三文件。
