// Lemon 引擎 — 项目级帧事件注册表实现（M8 批⑬；头文件见契约）
#include "Assets/AnimEventRegistry.h"

#include <algorithm>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Assets/TableAsset.h" // ParseTableJson（.tab 轨道单源解析）
#include "Core/Log.h"

namespace lemon::assets {
namespace {

struct RegistryState {
    std::vector<AnimEventDef> defs;                    // 表行序（下拉稳定序）
    std::unordered_map<std::string, uint32_t> byName;  // 名字 → id
    std::unordered_map<uint32_t, size_t> byId;          // id → defs 下标
    std::unordered_map<std::string, uint32_t> prevIds; // 上份快照（id 漂移告警）
    std::unordered_map<std::string, char> driftWarned;  // (name|old|new) → 已告警
    // 批⑬b：订阅者声明面——AnimEventIdByName 桥查询名记录（首查询序，去重）。
    // next 重建不搬（SetAnimEventTable 注入即清，语义见头注释）
    std::vector<std::string> queried;
    std::unordered_set<std::string> queriedSet;
};

RegistryState& State() {
    static RegistryState s;
    return s;
}

/// ASCII 空白剥离（手写表格边杂空格——数值/名字格共用）
std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

} // namespace

/// 事件名 = 跨界窄串（桥 64B 缓冲）+ 下拉字面：空 / 控制字符 / 引号 / >64 字节拒
///（单源导出：引擎行检 + 编辑器「新建事件」预检）
bool ValidAnimEventName(std::string_view n) {
    if (n.empty() || n.size() > 64) return false;
    for (char c : n)
        if ((unsigned char)c < 0x20 || c == '"') return false;
    return true;
}

bool SetAnimEventTable(std::string_view tabJson, std::string* error) {
    RegistryState& st = State();
    if (error) error->clear();

    RegistryState next;
    next.prevIds = std::move(st.prevIds);   // 漂移检测链跨注入存活
    next.driftWarned = std::move(st.driftWarned);

    if (tabJson.empty()) { // 缺表/显式清空 = 裸 id 模式（合法保底）
        st = std::move(next);
        return true;
    }

    TableData t = ParseTableJson(tabJson);
    if (!t.ok) {
        if (error) *error = t.error;
        st = std::move(next); // 整表拒绝 = 空表（编辑器降级显 #N、C# 红字）
        return false;
    }
    if (t.rows.empty() || t.Cols() < 4 ||
        Trim(t.rows[0][0]) != "name" || Trim(t.rows[0][1]) != "id" ||
        Trim(t.rows[0][2]) != "payload" || Trim(t.rows[0][3]) != "desc") {
        if (error)
            *error = "列头不符列契约（应为 name/id/payload/desc——批⑬ anim-events.tab）";
        st = std::move(next);
        return false;
    }

    for (size_t r = 1; r < t.rows.size(); ++r) {
        const std::string_view name = Trim(t.rows[r][0]);
        const std::string_view idStr = Trim(t.rows[r][1]);
        if (name.empty() && idStr.empty()) continue; // 空行（尾换行宽容）

        uint32_t id = 0;
        if (!ValidAnimEventName(name)) {
            LEMON_WARN("anim-events.tab 第 %zu 行名字非法（'%.*s'）——跳过", r,
                       (int)name.size(), name.data());
            continue;
        }
        {
            char* end = nullptr;
            const long v = idStr.empty() ? -1 : std::strtol(idStr.data(), &end, 10);
            // strtol 需 NUL 终结的安全面：idStr 指向 std::string 内部（自带 NUL），
            // Trim 只动首尾——end 落点校验覆盖无 NUL 假设
            if (v < 1 || v > 65535 || end != idStr.data() + idStr.size()) {
                LEMON_WARN("anim-events.tab 第 %zu 行 id 非法（'%.*s'，应 1..65535）——"
                           "跳过",
                           r, (int)idStr.size(), idStr.data());
                continue;
            }
            id = (uint32_t)v;
        }

        if (next.byName.contains(std::string(name))) {
            LEMON_WARN("anim-events.tab 第 %zu 行名字重复（'%.*s'）——跳过（首行胜）",
                       r, (int)name.size(), name.data());
            continue;
        }
        if (next.byId.contains(id)) {
            LEMON_WARN("anim-events.tab 第 %zu 行 id %u 与先前行同名异 id 冲突——跳过"
                       "（同 id 异名，首行胜）",
                       r, id);
            continue;
        }

        AnimEventDef def;
        def.name.assign(name);
        def.id = id;
        def.payload.assign(Trim(t.rows[r][2]));
        def.desc.assign(Trim(t.rows[r][3]));
        next.byName.emplace(def.name, id);
        next.byId.emplace(id, next.defs.size());
        next.defs.push_back(std::move(def));
    }

    // id 漂移告警：同名异 id（手改既有行）——语义漂移风险面，提示新建行替代
    for (const auto& [name, id] : next.byName) {
        const auto prev = st.prevIds.find(name);
        if (prev == st.prevIds.end() || prev->second == id) continue;
        const std::string key = name + "|" + std::to_string(prev->second) + "|" +
                                std::to_string(id);
        if (next.driftWarned.emplace(std::move(key), 1).second)
            LEMON_WARN("anim-events.tab：事件 '%s' 的 id 已 %u → %u——旧 clip 打点语义"
                       "漂移风险（id 分配后永不变更；应恢复原 id 或新建事件行）",
                       name.c_str(), prev->second, id);
    }

    // 新快照的 prevIds = 本份（下轮注入的比较基线）
    st = std::move(next);
    st.prevIds = st.byName;
    LEMON_LOG("帧事件注册表装载：%zu 个事件（下一可用 id %u）", st.defs.size(),
              AnimEventNextId());
    return true;
}

uint32_t AnimEventIdByName(const char* name) {
    if (!name || !*name) return 0;
    RegistryState& st = State();
    // 批⑬b：订阅者声明面记录（含未注册名——③ 死订阅的候选域；去重保首查询序）
    if (st.queriedSet.insert(name).second) st.queried.emplace_back(name);
    const auto it = st.byName.find(name);
    return it != st.byName.end() ? it->second : 0;
}

const char* AnimEventNameById(uint32_t id) {
    const RegistryState& st = State();
    const auto it = st.byId.find(id);
    return it != st.byId.end() ? st.defs[it->second].name.c_str() : nullptr;
}

size_t AnimEventCount() { return State().defs.size(); }

bool AnimEventAt(size_t i, AnimEventDef& out) {
    const RegistryState& st = State();
    if (i >= st.defs.size()) return false;
    out = st.defs[i];
    return true;
}

uint32_t AnimEventNextId() {
    uint32_t maxId = 0;
    for (const AnimEventDef& d : State().defs) maxId = std::max(maxId, d.id);
    return maxId + 1; // 空表 = 1；满 65535 依赖行级 id 校验兜底（65536 行注被拒）
}

size_t AnimEventSubscriberCount() { return State().queried.size(); }

bool AnimEventSubscriberAt(size_t i, std::string& out) {
    const RegistryState& st = State();
    if (i >= st.queried.size()) return false;
    out = st.queried[i];
    return true;
}

std::vector<AnimEventLintItem> AnimEventLint(const std::vector<AnimEventUsage>& used) {
    const RegistryState& st = State();
    std::vector<AnimEventLintItem> out;
    // ① 打点 id 未注册（used 序，同 (clip,id) 去重；id 0 = 无事件语义跳过）
    std::unordered_set<std::string> seen;
    for (const AnimEventUsage& u : used) {
        if (u.id == 0 || st.byId.contains(u.id)) continue;
        if (!seen.insert(u.clip + '#' + std::to_string(u.id)).second) continue;
        AnimEventLintItem it;
        it.kind = 1;
        it.id = u.id;
        it.clip = u.clip;
        out.push_back(std::move(it));
    }
    if (st.queried.empty()) return out; // ②③ 门：订阅者面空 = 本会话未装载过脚本
    // ② 注册名无订阅者（表行序）
    for (const AnimEventDef& d : st.defs) {
        if (st.queriedSet.contains(d.name)) continue;
        AnimEventLintItem it;
        it.kind = 2;
        it.name = d.name;
        it.id = d.id;
        out.push_back(std::move(it));
    }
    // ③ 订阅名已注册但全项目无打点（首查询序；未注册订阅名归 ⑬a 启动红字不双报）
    std::unordered_set<uint32_t> usedIds;
    for (const AnimEventUsage& u : used) usedIds.insert(u.id);
    for (const std::string& n : st.queried) {
        const auto it = st.byName.find(n);
        if (it == st.byName.end() || usedIds.contains(it->second)) continue;
        AnimEventLintItem li;
        li.kind = 3;
        li.name = n;
        li.id = it->second;
        out.push_back(std::move(li));
    }
    return out;
}

} // namespace lemon::assets
