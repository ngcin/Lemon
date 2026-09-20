// Lemon 引擎 — 档① 脚本组件（04 §2.1）
// M4.4 从 ScriptHost.h 拆出并扩 guid/className：.scene 序列化需要轻量包含
// （SceneArchive 不得拖入 hostfxr 头）。仍是普通 entt 组件、不入 ComponentRegistry
// ——GCHandle/类型 id 属运行时桥状态，入注册表会进 StateHash/序列化，破坏回放
// 与 .scene 语义（原决策不变）。className 是持久键（typeId 注册序随代码变动不稳）。
#pragma once

#include <cstdint>

namespace lemon::scripting {

struct ScriptBox {
    int32_t typeId = -1;     // Behaviours 注册序（C# 侧；-1 = 未解析（按 className 找宿主映射））
    uint32_t flags = 0;      // bit0 disabled（异常禁用）
    uint64_t scriptGuid = 0; // 脚本资产 GUID（M4.4：.meta 持久引用；0 = 未关联资产）
    char className[24] = {}; // C# 类名（.scene 持久；宿主按名解析 typeId）
};

} // namespace lemon::scripting
