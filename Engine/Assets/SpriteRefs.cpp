// Lemon 引擎 — SpriteRefs 实现（M7a 批②；语义自 EditorContext::ResolveSpriteRefs
// 逐行移植——纯函数化，查询面经 SpriteRefSource 注入）
#include "Assets/SpriteRefs.h"

namespace lemon::assets {

SpriteRefStats ResolveSpriteRefs(ecs::Scene& scene, const SpriteRefSource& src) {
    SpriteRefStats st;
    scene.Each([&](ecs::Entity e) {
        ecs::SpriteRenderer* sr = scene.TryGet<ecs::SpriteRenderer>(e);
        if (!sr) return;
        if (sr->spriteGuid != 0) {
            const SpriteEntryView* en = src.SpriteByGuid(sr->spriteGuid);
            if (en && en->alive && en->spriteId != 0) {
                // id 落在合法域内 = 已是当前进程真值，保号不覆写（guid 只锚资产，
                // cell 是层内偏移）：切片表 = cell 区间 [sliceBase, +count)，整图 =
                // 本体号（区间判定语义见 SpriteRefs.h 文件头）
                const uint32_t cur = sr->spriteId;
                const bool inRange =
                    (en->sliceCount == 0 && cur == en->spriteId) ||
                    (en->sliceCount > 0 && cur >= en->sliceBase &&
                     cur < en->sliceBase + en->sliceCount);
                if (!inRange)
                    sr->spriteId = en->sliceCount > 0 ? en->sliceBase : en->spriteId;
            } else {
                ++st.danglingGuid;
            }
        } else if (sr->spriteId >= src.SpriteIdBase()) {
            if (const SpriteEntryView* en = src.SpriteByWholeId(sr->spriteId);
                en && en->alive) {
                sr->spriteGuid = en->guid;
                ++st.backfilled;
            }
        }
    });
    return st;
}

} // namespace lemon::assets
