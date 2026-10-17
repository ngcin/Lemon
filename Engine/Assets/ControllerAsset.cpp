// Lemon 引擎 — ControllerAsset 实现（M6a 批② T3d / M7a 批② 下沉；见 ControllerAsset.h 契约注记）
#include "Assets/ControllerAsset.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <unordered_map>

#include "Assets/AnimAsset.h" // JsonEscape（名字字段转义单源，review 2026-10-02 #5）
#include "nlohmann/json.hpp"

namespace lemon::assets {

namespace {

ControllerData Fail(const char* what, size_t idx) {
    ControllerData out;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s（条目 %zu）", what, idx);
    out.error = buf;
    return out;
}

int StateIdx(const ControllerData& c, const std::string& s) {
    for (size_t i = 0; i < c.states.size(); ++i)
        if (c.states[i] == s) return (int)i;
    return -1;
}

int KindOf(const std::string& s) {
    if (s == "float") return 0;
    if (s == "bool") return 1;
    if (s == "trigger") return 2;
    return -1;
}
const char* KindName(int k) {
    return k == 1 ? "bool" : k == 2 ? "trigger" : "float";
}

int OpOf(const std::string& s) {
    if (s == "<") return 0;
    if (s == "<=") return 1;
    if (s == ">") return 2;
    if (s == ">=") return 3;
    if (s == "==") return 4;
    if (s == "trigger") return 5;
    return -1;
}
const char* OpName(int o) {
    static const char* kNames[] = {"<", "<=", ">", ">=", "==", "trigger"};
    return kNames[o >= 0 && o <= 5 ? o : 2];
}

void AppendFloat(std::string& out, float v) {
    // #90：%g 最短有效表示（6 位）——原非整值恒 %.1f，过渡阈值 0.15 保存后变
    // 0.1/0.2（游戏逻辑值经保存-重载静默漂移）；整值路径不变（1 不写成 1.0）
    char buf[32];
    if (v == (float)(long long)v)
        std::snprintf(buf, sizeof(buf), "%lld", (long long)v);
    else
        std::snprintf(buf, sizeof(buf), "%g", (double)v);
    out += buf;
}

} // namespace

ControllerData ParseControllerJson(std::string_view text) {
    ControllerData out;
    const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object())
        return Fail("controller 解析失败：非 JSON 对象", 0);
    if (!doc.contains("states") || !doc.at("states").is_array() ||
        doc.at("states").empty())
        return Fail("controller 解析失败：缺 states[]（或为空）", 0);
    if (doc.contains("name") && doc.at("name").is_string())
        out.name = doc.at("name").get<std::string>();
    for (const nlohmann::json& st : doc.at("states")) {
        const size_t idx = out.states.size();
        if (!st.is_string() || st.get<std::string>().empty())
            return Fail("状态名非字符串/为空", idx);
        if (st.get<std::string>() == kControllerAnyFrom)
            return Fail("状态名不可为 \"*\"（any-state 哨兵保留字，批⑪）", idx);
        for (const std::string& prev : out.states)
            if (prev == st.get<std::string>())
                return Fail("状态重名（词表内必须唯一）", idx);
        out.states.push_back(st.get<std::string>());
    }
    if (doc.contains("entry")) {
        if (!doc.at("entry").is_string()) return Fail("entry 非字符串", 0);
        out.entry = doc.at("entry").get<std::string>();
        bool found = false;
        for (const std::string& s : out.states) found |= s == out.entry;
        if (!found) return Fail("entry 引用了未列出的状态", 0);
    }
    if (doc.contains("params")) {
        if (!doc.at("params").is_array()) return Fail("params 非 JSON 数组", 0);
        for (const nlohmann::json& pm : doc.at("params")) {
            const size_t idx = out.params.size();
            if (idx >= 8) return Fail("参数超过 8 个上限（ADR-013 D4 v1 词面）", idx);
            if (!pm.is_object() || !pm.contains("name") || !pm.at("name").is_string())
                return Fail("参数缺 name 字符串", idx);
            ControllerParamEdit p;
            p.name = pm.at("name").get<std::string>();
            if (p.name.empty()) return Fail("参数 name 为空", idx);
            if (pm.contains("kind")) {
                if (!pm.at("kind").is_string()) return Fail("参数 kind 非字符串", idx);
                p.kind = KindOf(pm.at("kind").get<std::string>());
                if (p.kind < 0) return Fail("参数 kind 未识别（float/bool/trigger）", idx);
            }
            if (pm.contains("def") && pm.at("def").is_number())
                p.def = pm.at("def").get<float>();
            for (const ControllerParamEdit& prev : out.params)
                if (prev.name == p.name) return Fail("参数重名", idx);
            out.params.push_back(p);
        }
    }
    // terminal（批⑮a）：顶层可选终态白名单——additive 块（旧引擎忽略/旧档缺省
    // 空）。条目须列于 states 且不重名；语义 = any 边在该状态不命中。
    if (doc.contains("terminal")) {
        if (!doc.at("terminal").is_array()) return Fail("terminal 非 JSON 数组", 0);
        for (const nlohmann::json& tm : doc.at("terminal")) {
            const size_t idx = out.terminal.size();
            if (!tm.is_string()) return Fail("terminal 条目非字符串", idx);
            const std::string& s = tm.get<std::string>();
            if (StateIdx(out, s) < 0) return Fail("terminal 引用了未列出的状态", idx);
            for (const std::string& prev : out.terminal)
                if (prev == s) return Fail("terminal 重名", idx);
            out.terminal.push_back(s);
        }
    }
    if (doc.contains("transitions")) {
        if (!doc.at("transitions").is_array()) return Fail("transitions 非 JSON 数组", 0);
        for (const nlohmann::json& tr : doc.at("transitions")) {
            const size_t idx = out.transitions.size();
            if (!tr.is_object() || !tr.contains("from") || !tr.contains("to") ||
                !tr.at("from").is_string() || !tr.at("to").is_string())
                return Fail("过渡缺 from/to 字符串", idx);
            ControllerTransitionEdit t;
            t.from = tr.at("from").get<std::string>();
            t.to = tr.at("to").get<std::string>();
            // any-state（批⑪）：from == "*" = 任意当前状态出发（免列校验）；to 仍须列出
            bool fromOk = t.from == kControllerAnyFrom, toOk = false;
            for (const std::string& s : out.states) {
                fromOk |= s == t.from;
                toOk |= s == t.to;
            }
            if (!fromOk) return Fail("from 引用了未列出的状态", idx);
            if (!toOk) return Fail("to 引用了未列出的状态", idx);
            if (t.from == t.to) // B16：图切换无重启语义（同段 no-op 只吃 trigger 遮出边）
                return Fail("自环过渡（from == to）——重启动画走脚本 Play，图不做", idx);
            if (tr.contains("on")) {
                if (!tr.at("on").is_string() || tr.at("on").get<std::string>() != "exitTime")
                    return Fail("on 未识别（仅支持 \"exitTime\"）", idx);
                t.exitTime = true;
            }
            if (tr.contains("when")) {
                if (!tr.at("when").is_array()) return Fail("when 非数组", idx);
                for (const nlohmann::json& cd : tr.at("when")) {
                    if (!cd.is_object()) return Fail("条件非对象", idx);
                    // 单键对象：{"param":"speed", "<":0.1} / {"trigger":"attack"}
                    ControllerCondEdit c;
                    bool got = false;
                    for (auto it = cd.begin(); it != cd.end(); ++it) {
                        if (it.key() == "param") {
                            if (!it.value().is_string()) return Fail("条件 param 非字符串", idx);
                            c.param = it.value().get<std::string>();
                        } else if (it.key() == "trigger") {
                            if (!it.value().is_string()) return Fail("条件 trigger 非字符串", idx);
                            c.param = it.value().get<std::string>();
                            c.op = 5;
                            got = true;
                        } else {
                            c.op = OpOf(it.key());
                            if (c.op < 0) return Fail("条件算子未识别（< <= > >= == trigger）", idx);
                            if (!it.value().is_number()) return Fail("条件阈值非数值", idx);
                            c.value = it.value().get<float>();
                            got = true;
                        }
                    }
                    if (c.param.empty()) return Fail("条件缺 param/trigger 名", idx);
                    if (!got) return Fail("条件缺算子键（如 \">\":0.1）", idx);
                    bool pOk = false;
                    for (const ControllerParamEdit& p : out.params) pOk |= p.name == c.param;
                    if (!pOk) return Fail("条件引用了未声明的参数", idx);
                    t.conds.push_back(c);
                }
            }
            if (t.conds.empty() && !t.exitTime)
                return Fail("空条件且非 exitTime = 无条件即切（作者错误）", idx);
            out.transitions.push_back(t);
        }
    }
    // editor.nodes（M8 批⑩a）：容错读取——未列状态键/坏坐标（非 2 元数值数组）
    // 忽略单项不拦解析；按 states 序归一（手写键序任意，序列化同序）。
    if (doc.contains("editor")) {
        const nlohmann::json& ed = doc.at("editor");
        if (ed.is_object()) {
            const auto ni = ed.find("nodes");
            if (ni != ed.end() && ni->is_object()) {
                for (const std::string& s : out.states) {
                    const auto it = ni->find(s);
                    if (it == ni->end() || !it->is_array() || it->size() != 2) continue;
                    if (!(*it)[0].is_number() || !(*it)[1].is_number()) continue;
                    out.layout.push_back(
                        {s, (int)std::lround((*it)[0].get<double>()),
                         (int)std::lround((*it)[1].get<double>())});
                }
                // Any 节点保留键 "*"（批⑪ 五轮：可拖/持久化；states 词面禁 "*"，
                // 键位无碰撞；规范序 = states 后置 any 尾——与序列化同构）
                if (const auto it = ni->find(kControllerAnyFrom);
                    it != ni->end() && it->is_array() && it->size() == 2 &&
                    (*it)[0].is_number() && (*it)[1].is_number()) {
                    out.layout.push_back(
                        {kControllerAnyFrom, (int)std::lround((*it)[0].get<double>()),
                         (int)std::lround((*it)[1].get<double>())});
                }
            }
        }
    }
    out.ok = true;
    return out;
}

std::string ControllerToJson(const ControllerData& c) {
    if (!c.ok) return {};
    // 名字字段（name/entry/状态名/param 名）一律经 JsonEscape，from/to 拼接弃用
    // 定长 char[192]（>约 163 字符静默截断；review 2026-10-02 #5）
    std::string out = "{\n  \"schemaVersion\": 1,\n  \"name\": ";
    out += JsonEscape(c.name);
    out += ",\n  \"params\": [";
    for (size_t i = 0; i < c.params.size(); ++i) {
        const ControllerParamEdit& p = c.params[i];
        out += i ? ",\n    " : "\n    ";
        out += "{ \"name\": ";
        out += JsonEscape(p.name);
        out += ", \"kind\": \"";
        out += KindName(p.kind);
        if (p.def != 0.0f) {
            out += "\", \"def\": ";
            AppendFloat(out, p.def);
            out += " }";
        } else {
            out += "\" }";
        }
    }
    out += c.params.empty() ? "]" : "\n  ]";
    out += ",\n  \"entry\": ";
    out += JsonEscape(c.entry.empty() ? (c.states.empty() ? std::string() : c.states[0])
                                      : c.entry);
    out += ",\n  \"states\": [";
    for (size_t i = 0; i < c.states.size(); ++i) {
        out += i ? ", " : "";
        out += JsonEscape(c.states[i]);
    }
    out += "]";
    // terminal（批⑮a）：仅非空写出——空档不落该键，旧格式逐位不变（零重录前提）
    if (!c.terminal.empty()) {
        out += ",\n  \"terminal\": [";
        for (size_t i = 0; i < c.terminal.size(); ++i) {
            out += i ? ", " : "";
            out += JsonEscape(c.terminal[i]);
        }
        out += "]";
    }
    out += ",\n  \"transitions\": [";
    for (size_t i = 0; i < c.transitions.size(); ++i) {
        const ControllerTransitionEdit& t = c.transitions[i];
        out += i ? ",\n    { \"from\": " : "\n    { \"from\": ";
        out += JsonEscape(t.from);
        out += ", \"to\": ";
        out += JsonEscape(t.to);
        if (t.exitTime) out += ", \"on\": \"exitTime\"";
        if (!t.conds.empty()) {
            out += ", \"when\": [";
            for (size_t j = 0; j < t.conds.size(); ++j) {
                const ControllerCondEdit& cd = t.conds[j];
                out += j ? ", " : "";
                if (cd.op == 5) {
                    out += "{ \"trigger\": ";
                    out += JsonEscape(cd.param);
                    out += " }";
                } else {
                    out += "{ \"param\": ";
                    out += JsonEscape(cd.param);
                    out += ", \"";
                    out += OpName(cd.op);
                    out += "\": ";
                    AppendFloat(out, cd.value);
                    out += " }";
                }
            }
            out += "]";
        }
        out += " }";
    }
    out += c.transitions.empty() ? "]" : "\n  ]";
    // editor 块（仅当有仍列于 states 的布局项时写出；悬空键丢弃 = 保存即清洗；
    // states 序——与解析归一序同构，roundtrip 等值的前提；保留键 "*" = Any 节点位
    // 置（批⑪ 五轮），规范序恒居尾）
    std::string edNodes;
    for (const ControllerNodePos& np : c.layout) {
        if (np.state != kControllerAnyFrom && StateIdx(c, np.state) < 0) continue;
        edNodes += edNodes.empty() ? "\n      " : ",\n      ";
        edNodes += JsonEscape(np.state);
        edNodes += ": [";
        edNodes += std::to_string(np.x);
        edNodes += ", ";
        edNodes += std::to_string(np.y);
        edNodes += "]";
    }
    if (!edNodes.empty()) {
        out += ",\n  \"editor\": {\n    \"nodes\": {";
        out += edNodes;
        out += "\n    }\n  }";
    }
    out += "\n}";
    return out;
}

std::vector<uint8_t> ControllerReachability(const ControllerData& c) {
    std::vector<uint8_t> reach(c.states.size(), 0);
    if (!c.ok || c.states.empty()) return reach;
    const int entry = StateIdx(c, c.entry.empty() ? c.states.front() : c.entry);
    if (entry < 0) return reach; // 悬空 entry 归解析器管（不可达词面不重复报）
    reach[(size_t)entry] = 1;
    std::vector<int> queue{entry};
    for (size_t qi = 0; qi < queue.size(); ++qi) {
        for (const ControllerTransitionEdit& t : c.transitions) {
            // any 边（from="*"）= 每个已访状态都有此出边（批⑪）——目标恒可达
            if (t.from != kControllerAnyFrom && t.from != c.states[(size_t)queue[qi]])
                continue;
            const int to = StateIdx(c, t.to);
            if (to >= 0 && !reach[(size_t)to]) {
                reach[(size_t)to] = 1;
                queue.push_back(to);
            }
        }
    }
    return reach;
}

std::vector<std::string> ControllerStructuralWarnings(const ControllerData& c) {
    std::vector<std::string> out;
    if (!c.ok || c.states.empty()) return out;
    if (StateIdx(c, c.entry.empty() ? c.states.front() : c.entry) < 0) return out;
    const std::vector<uint8_t> reach = ControllerReachability(c);
    for (size_t i = 0; i < c.states.size(); ++i) {
        if (!reach[i])
            out.push_back(c.states[i] + "：从 entry 不可达（无入边可达——死状态）");
    }
    // 批⑮a：终态挂条件出边（when/trigger）→ info 级提示——显式 from 边在终态照常
    // 求值，条件出边会打断"驻留到局末"的终态语义（复活流合法但请确认）；无条件
    // exitTime 出边 = 合法自动续，不告警。
    for (const std::string& term : c.terminal) {
        for (const ControllerTransitionEdit& t : c.transitions) {
            if (t.from != term || t.conds.empty()) continue;
            out.push_back(term + " 为终态但挂条件出边 → " + t.to +
                          "（终态出边将打断驻留语义，复活流请确认）");
        }
    }
    return out;
}

std::vector<ControllerNodePos> ControllerAutoLayoutNodes(const ControllerData& c) {
    std::vector<ControllerNodePos> out;
    const int n = (int)c.states.size();
    if (n <= 0) return out;
    std::vector<int> layer((size_t)n, -1), order((size_t)n, 0);
    int visit = 0, maxLayer = 0;
    std::vector<int> queue;
    // BFS 分层：入队/访问序 = transitions 文件序（确定性——无随机、无哈希序）
    auto bfsFrom = [&](int root, int baseLayer) {
        if (root < 0 || layer[(size_t)root] >= 0) return;
        layer[(size_t)root] = baseLayer;
        order[(size_t)root] = visit++;
        queue.push_back(root);
        for (size_t qi = 0; qi < queue.size(); ++qi) {
            const int cur = queue[qi];
            for (const ControllerTransitionEdit& t : c.transitions) {
                // any 边跟随（批⑪）：目标经任意已访状态可达 → 层 = 当前层 + 1
                if (t.from != kControllerAnyFrom && t.from != c.states[(size_t)cur])
                    continue;
                const int to = StateIdx(c, t.to);
                if (to >= 0 && layer[(size_t)to] < 0) {
                    layer[(size_t)to] = layer[(size_t)cur] + 1;
                    order[(size_t)to] = visit++;
                    queue.push_back(to);
                }
            }
        }
        for (const int i : queue) maxLayer = std::max(maxLayer, layer[(size_t)i]);
        queue.clear();
    };
    const int entry = StateIdx(c, c.entry.empty() ? c.states.front() : c.entry);
    bfsFrom(entry >= 0 ? entry : 0, 0);
    // 不可达状态归尾列（各起新列 BFS，states 序——与 lint 灰化同一集合）
    for (int i = 0; i < n; ++i)
        if (layer[(size_t)i] < 0) bfsFrom(i, maxLayer + 1);
    // 同列行号（BFS 访问序）
    std::vector<int> byOrder((size_t)n), row((size_t)n, 0);
    std::iota(byOrder.begin(), byOrder.end(), 0);
    std::sort(byOrder.begin(), byOrder.end(), [&](int a, int b) {
        if (layer[(size_t)a] != layer[(size_t)b]) return layer[(size_t)a] < layer[(size_t)b];
        return order[(size_t)a] < order[(size_t)b];
    });
    std::unordered_map<int, int> rowsInLayer;
    for (const int i : byOrder) row[(size_t)i] = rowsInLayer[layer[(size_t)i]]++;
    out.reserve((size_t)n);
    for (int i = 0; i < n; ++i)
        out.push_back({c.states[(size_t)i], layer[(size_t)i] * 192, row[(size_t)i] * 96});
    return out;
}

int ControllerSnapGrid(int v) {
    return (int)std::lround((double)v / 8.0) * 8;
}

} // namespace lemon::assets
