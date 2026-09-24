// Lemon 引擎 — 档① 脚本组件（04 §2.1）
// M4.4 从 ScriptHost.h 拆出并扩 guid/className：.scene 序列化需要轻量包含
// （SceneArchive 不得拖入 hostfxr 头）。仍是普通 entt 组件、不入 ComponentRegistry
// ——GCHandle/类型 id 属运行时桥状态，入注册表会进 StateHash/序列化，破坏回放
// 与 .scene 语义（原决策不变）。className 是持久键（typeId 注册序随代码变动不稳）。
// M6a 批⓪：单槽扩内嵌定长多槽（每实体多脚本）。同类型唯一由入口闸保证（Inspector
// 拒绝 / AddComponent 幂等 / Behaviours.Attach 断言），格式层 scripts[] 可存重复、
// 加载侧清洗（保序留首见）。destroyNotified 自槽级 flags 升实体级字段——多槽后
// 逐槽各发 = OnDestroy 重复通知（F-08.2 恰好一次语义按实体算）。
#pragma once

#include <cstdint>
#include <cstring>

namespace lemon::scripting {

/// 每实体脚本槽上限（M6a 批⓪ 决策 1：8 = 模板三拆 + 用户项目余量；溢出 = 编辑器
/// 拒绝追加 + 告警。ScriptBox 不入注册表无布局冻结约束，扩容一行改动即可）
constexpr uint32_t kMaxScriptsPerEntity = 8;

/// ScriptSlot.flags bit0：异常禁用（C# 侧 Disabled 列表的 C++ 镜像位）
constexpr uint32_t kScriptFlagDisabled = 1u << 0;

/// ScriptBox.notified 置位值：OnDestroy 通知已发（脚本命令路径与销毁提交路径共用
/// lemon_scripts_destroy——置位防双发；2026-09-24 审查 F-08.2；实体级非槽级）
constexpr uint32_t kScriptFlagDestroyNotified = 1u << 1;

struct ScriptSlot {
    int32_t typeId = -1;     // Behaviours 注册序（C# 侧；-1 = 未解析（按 className 找宿主映射））
    uint32_t flags = 0;      // bit0 disabled（异常禁用）
    uint64_t scriptGuid = 0; // 脚本资产 GUID（M4.4：.meta 持久引用；0 = 未关联资产）
    char className[24] = {}; // C# 类名（.scene 持久；宿主按名解析 typeId）
};

struct ScriptBox {
    uint32_t notified = 0; // 实体级 kScriptFlagDestroyNotified（见上）
    uint8_t count = 0;     // 活动槽数（紧凑无空洞；归零时调用方移除组件）
    uint8_t _pad[3] = {};
    ScriptSlot slots[kMaxScriptsPerEntity];
};

/// 尾部追加槽（满 = false；不查重——同类型唯一由入口闸保证，见头注）
inline bool AppendSlot(ScriptBox& sb, uint64_t scriptGuid, const char* className) {
    if (sb.count >= kMaxScriptsPerEntity) return false;
    ScriptSlot& s = sb.slots[sb.count++];
    s.typeId = -1;
    s.flags = 0;
    s.scriptGuid = scriptGuid;
    std::memset(s.className, 0, sizeof(s.className));
    if (className) {
        size_t n = std::strlen(className);
        if (n > 23) n = 23; // 末字节保 \0
        std::memcpy(s.className, className, n);
    }
    return true;
}

/// 按 className 找槽（-1 = 无；同类型唯一不变量下至多命中一个）
inline int FindSlot(const ScriptBox& sb, const char* className) {
    if (!className) return -1;
    for (uint32_t i = 0; i < sb.count; ++i)
        if (std::strcmp(sb.slots[i].className, className) == 0) return (int)i;
    return -1;
}

/// 移动删除保槽序（槽序 = .scene scripts[] 序；交换删除会乱序）
inline void RemoveSlot(ScriptBox& sb, uint32_t index) {
    if (index >= sb.count) return;
    std::memmove(sb.slots + index, sb.slots + index + 1,
                 sizeof(ScriptSlot) * (size_t)(sb.count - index - 1));
    --sb.count;
}

} // namespace lemon::scripting
