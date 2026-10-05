# CI win job 首跑热修⑤ —— 零长数组扩展面（ComponentCatalog）（2026-10-05，W7 迭代）

事件：热修④ 后重跑，ECS 面 TU 滚进，收在 `Engine/Components/ComponentCatalog.cpp`：

```
error C2466: cannot allocate an array of constant size 0        (:57)
error C2131: expression did not evaluate to a constant          (:57)
error C2070: 'const FieldMeta []': ill-formed sizeof operand    (:454)
```

根因：`constexpr FieldMeta kDestroyQueueTag[] = {};` ——**零长数组是 GCC/clang
扩展**，ISO C++ 数组尺寸须 > 0，MSVC 直接拒（定义处 C2466；REGISTER 宏的
`sizeof(fields)/sizeof(FieldMeta)` 处连锁 C2131/C2070）。mac clang 按扩展收下且
sizeof 商恰为 0 = 行为正确，纯属方言差。

修法：零字段标签组件改走新 `REGISTER_TAG(Name)` 宏（`fields=nullptr, count=0`，
`ComponentMeta::fields` 本就是 `const FieldMeta*`，类型面天然合法），
`kDestroyQueueTag` 空数组删除。消费端（Inspector/归档/状态哈希）一律按 count
循环，nullptr 不可达——注册后行为与 clang 扩展路径恒等（count 同为 0）。
全仓扫描（含 Editor/tests/tools/Samples）无其他零长数组残留。

mac 复验：构建绿 + ctest 4/4（engine-tests 含 DestroyQueueTag 注册路径）。

类别归档：至此 CI 首跑五连修覆盖三类方言差——环境面（①安装器换代 ②镜像 VS
换代）、libc++ 扩展面（③constexpr 数学 ④constexpr 链 MSVC 主动诊断）、
GCC/clang 数组扩展面（⑤零长数组）。
