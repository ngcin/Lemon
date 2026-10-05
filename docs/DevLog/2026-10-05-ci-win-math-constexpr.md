# CI win job 首跑热修③ —— Math.h constexpr 数学函数的 libc++ 扩展面（2026-10-05，W7 迭代）

事件：VS2026 生成器热修后重跑，MSVC 首编大规模推进（lemon-engine 大部 TU、
lemon-imgui、lemon-packager-selftest、C# SDK 均过），收尾在 `Engine/Core/Math.h`：

```
error C3615: constexpr function 'lemon::math::Damp' cannot result in a
constant expression … see usage of 'exp'   （GameFx.cpp / SceneExtractor.cpp）
error C3615: constexpr function 'lemon::math::SnapTo' … see usage of 'round'
```

根因：`std::exp`/`std::round` 的 constexpr 是 **libc++ 单家扩展**（mac clang 侧
绿了一路），C++23 标准未覆盖、MSVC STL 与 libstdc++ 均不标——`Damp`/`SnapTo`
两函数（含 Vec2 重载，经 float 版间接调用）标 constexpr 在 MSVC 必炸。

修法：`constexpr` → `inline`（`Length()` 的 std::sqrt 既有同款口径）。全仓扫描
（同正则扫 Engine/Editor/tests/tools/Samples 的 .h/.cpp）确认 constexpr×std 数学
仅此两处；无 static_assert/常量上下文消费方，降级零行为变化。mac 复验：构建绿 +
ctest 4/4。

顺带观察（非阻塞，未动）：`Samples/bench-script/script/GameMain.cs` CS9196 ×2
（override 参数 `in Chunk` vs 基类 `ref readonly Chunk`）——与平台无关的既有
警告，dotnet 侧 Mac/Win 同现，归后续批顺手对齐修饰符。

观察项状态：SDK 面全绿；本条为 07 §3.6 批⑦ 增补表该记的 MSVC 实证第一条
（"libc++ 扩展面"类别——mock 门编不到，因 mock 也用 libc++）。
