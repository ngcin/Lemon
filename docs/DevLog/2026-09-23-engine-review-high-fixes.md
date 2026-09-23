# 2026-09-23 Engine-Review 三 High 核实与修复（H1/H2 修，H3 证伪）

**核实**：外部 review 报告（`../Reports/2026-09-23-engine-review.md`）3 个 High 逐条对源码复核——H1/H2
成立，**H3 证伪**。H3（`SceneSetParent` 持 `Hierarchy&` 跨同池 Emplace 悬垂）前提错误：
EnTT 3.15 `basic_storage` 组件负载为分页存储（`ENTT_PACKED_PAGE=1024`，`assure_at_least`
增长只追加新页不搬旧页，`try_emplace` 只追加/复用洞），**emplace 不失效同池既有引用**；
`Scene::Emplace` 是 `registry_` 薄封装。代码模式依赖实现细节，升级 EnTT 时需复查，但
不构成 bug，从 High 撤下。

**H1 渲染分批顺序**：Extract 桶按首遇序排布，桶内 `std::sort` 修不了跨桶，且
`SpriteBatcher::Bake` 按连续同键段录制无重排 → 先创建的高 `sortingLayer` 实体整桶画到
低层下面（同图集/混合/过滤、不同层即可触发，约一半创建序组合命中），违背
`Renderable.h` 声明的「SortingLayer → 批键 → order → seq」契约。修复：两处 Extract
（`Renderable.cpp`/`Particles.cpp`）偏移分配前按（layer, hash）排桶——`slots` 原地
不动故 `slotOf_` 槽下标仍有效，搬运与桶内排序段零改动。

**H2 粒子 spriteId**：`EmitterConfig` 默认 `spriteId=0`，粒子提取路径缺精灵路径的合法性
过滤 → `GetSprite(0)` 必断言 abort（`LEMON_ASSERT` 无 NDEBUG 门控，报告所述"release 越界
读垃圾进 bindless"路径不存在；现有 4 处调用点均显式设置，属潜伏 API 陷阱）。修复：
`Particles.cpp` Extract 循环加 `atlas.IsValidSprite` 过滤（顺带覆盖空洞退役号）。

**回归**：新增 `TestRenderableExtractOrder`（跨桶层单调 + 同层跨图集按批键 hash 排段）；
`TestParticles` 补两条（无效 spriteId 跳过不渲染、高层发射器先发低层段仍排前）。咬合
验证：新测试叠旧代码 → 恰在 `Atlas.cpp:112 bad spriteId` abort（H2 崩溃点）；叠修复
后 lemon-tests 全绿 **13158 checks**（Release）。未 commit。
