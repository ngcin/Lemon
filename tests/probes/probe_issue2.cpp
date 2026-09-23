// Lemon — 存档安全回归探针（ISSUE-2 / ISSUE-8，2026-09-19 修复轮固化）
// 独立编译运行，不接入 CMake、不改 Engine；三段检查（全部应为"干净"输出）：
//   ① Equipment 合法档读入：不应出现 "type mismatch skipped" 假告警
//      （ISSUE-8：relicIds 曾同时登记普通字段+数组段；修复 = 登记表删字段行）
//   ② StatusEffects 只写 count=200（无 active 键）：Load 应钳到容量 4（warn 一条/
//      实体），StatSystem tick 后 count=0、active[*].id=0、无内存污染
//      （ISSUE-2：修复前按 count 越界读写，8 实体落池内静默、1000 实体 ASan 实锤
//       heap-buffer-overflow @ StatSystem —— 见 docs/Reports/2026-09-19-m2-review-checklist.md §7.1/7.2）
//   ③ 1000 实体 count=511（截断 255）：同上，ASan 全程零报告
//
// 编译运行（macOS，ASan；依赖头在 CPM 缓存，路径以本机为准）：
//   cd Lemon && clang++ -std=c++20 -g -O0 -fsanitize=address -I Engine \
//     -I ~/.cache/Lemon-CPM/EnTT/f3a6/single_include \
//     -I ~/.cache/Lemon-CPM/nlohmann_json/798e/single_include \
//     tests/probes/probe_issue2.cpp \
//     Engine/Components/ComponentCatalog.cpp Engine/Core/JobSystem.cpp Engine/Core/Log.cpp \
//     Engine/ECS/Scene.cpp Engine/ECS/StateHash.cpp Engine/ECS/SystemPipeline.cpp \
//     Engine/ECS/World.cpp Engine/Physics2D/SpatialHash.cpp \
//     Engine/Serialization/SceneArchive.cpp Engine/Systems/Systems.cpp \
//     -o /tmp/probe_issue2 && /tmp/probe_issue2
#include <cstdio>
#include <string>

#include "ECS/Entity.h"
#include "Components/GameplayComponents.h"
#include "ECS/ComponentRegistry.h"
#include "ECS/Scene.h"
#include "ECS/World.h"
#include "Serialization/SceneArchive.h"
#include "Systems/Systems.h"

using namespace lemon::ecs;

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    RegisterAllComponents();
    World world;

    // ---- ① Equipment 合法档：期望无 warn、relicIds 读入正确 ----
    {
        Scene& s = world.CreateScene("equip");
        const char* j =
            R"({"schemaVersion":1,"entities":[{"components":{"Equipment":{"weaponId":7,"relicIds":[1,2,3]}}}]})";
        bool ok = SceneArchive::Load(s, j);
        std::printf("[probe1] load=%d\n", ok ? 1 : 0);
        s.Each([&](Entity e) {
            if (const Equipment* eq = s.TryGet<Equipment>(e))
                std::printf("[probe1] weaponId=%u relicIds=[%u,%u,%u]\n", eq->weaponId,
                            eq->relicIds[0], eq->relicIds[1], eq->relicIds[2]);
        });
    }

    // ---- ② StatusEffects count=200（无数组键）：期望钳 4 + tick 干净 ----
    {
        Scene& s = world.CreateScene("stat");
        std::string ents;
        for (int i = 0; i < 8; ++i) ents += R"({"components":{"StatusEffects":{"count":200}}},)";
        ents.pop_back();
        std::string j = R"({"schemaVersion":1,"entities":[)" + ents + "]}";
        bool ok = SceneArchive::Load(s, j);
        std::printf("[probe2] load=%d\n", ok ? 1 : 0);

        int n = 0, clamped = 0;
        s.Each([&](Entity e) {
            if (auto* st = s.TryGet<StatusEffects>(e)) {
                ++n;
                if (st->count == 4) ++clamped; // 200 → 容量 4
            }
        });
        std::printf("[probe2] entities=%d count==4: %d（期望 8/8）\n", n, clamped);

        StatSystem stat;
        stat.Tick(world, s, 1.0f / 60.0f);
        // 全零 active（remain=0）tick 后应全部到期：count=0、id=0；
        // stacks 默认 1 / remain 负值均为合法状态，不作污染判据
        int corrupted = 0;
        s.Each([&](Entity e) {
            if (auto* st = s.TryGet<StatusEffects>(e)) {
                bool dirty = st->count != 0;
                for (int k = 0; k < 4; ++k)
                    if (st->active[k].id != 0) dirty = true;
                if (dirty) ++corrupted;
            }
        });
        std::printf("[probe2] 异常实体数 = %d / %d（期望 0；>0 = 回归）\n", corrupted, n);
    }

    // ---- ③ 大规模（1000 实体 count=511→255）：期望 ASan 零报告 ----
    {
        Scene& s = world.CreateScene("big");
        std::string ents;
        for (int i = 0; i < 1000; ++i) ents += R"({"components":{"StatusEffects":{"count":511}}},)";
        ents.pop_back(); // 511 截断为 uint8 255
        std::string j = R"({"schemaVersion":1,"entities":[)" + ents + "]}";
        bool ok = SceneArchive::Load(s, j);
        std::printf("[probe3] load=%d\n", ok ? 1 : 0);
        StatSystem stat;
        stat.Tick(world, s, 1.0f / 60.0f);
        std::printf("[probe3] tick 完成，ASan 零报告（修复前此处 heap-buffer-overflow）\n");
    }
    return 0;
}
