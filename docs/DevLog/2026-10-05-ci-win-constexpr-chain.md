# CI win job 首跑热修④ —— constexpr 链断裂（Random.h）+ clang 杂值守卫（RHI.cpp）（2026-10-05，W7 迭代）

事件：热修③ 后重跑，MSVC 首编再进一大步（资产/音频/脚本/ECS 面 TU 滚过），收在
`Engine/Core/Random.h`：

```
error C3615: constexpr function 'lemon::Rng::Seed' cannot result in a constant
expression … see usage of 'lemon::Rng::Next'
```

根因（与③ 同科不同型）：`Rng(seed,stream)` ctor/`Seed` 标 constexpr，但空转调用的
`Next()` 不是——**MSVC 对 constexpr 函数体主动诊断**（不等到真常量求值），clang
仅实际常量求值时才查 → mac 一路绿。`Next` 体为纯算术（LCG + XSH-RR），本就够格。

修法（提级而非降级）：`Next()` 补 constexpr——`constexpr Rng` 常量构造语义保真
（金回放确定性位面零触碰）；全仓扫描 Math.h 等核心头其余 constexpr 函数均纯算术，
无同型残留。

顺带（同一诊断面）：RHI.cpp 的 `#pragma clang diagnostic …`（vulkan.h/VMA 告警
屏蔽）在 MSVC = C4068 未知杂注警告 ×6——`#if defined(__clang__)` 守卫（miniaudio
vendored 件自带守卫不用动）。警告非阻断，属首编清账卫生项。

mac 复验：构建绿 + ctest 4/4。

观察：CS9196 ×2（BenchScript `in` vs `ref readonly`）本跑仍在，非阻断维持前条
登记不动。
