// Lemon 编辑器 — ControllerEdit 实现（M6a 批② T3d；见 ControllerEdit.h 契约注记）
#include "Assets/ControllerEdit.h"

#include <cstdio>

#include "Assets/ClipEdit.h" // JsonEscape（名字字段转义单源，review 2026-10-02 #5）
#include "nlohmann/json.hpp"

namespace lemon::editor {

namespace {

ControllerData Fail(const char* what, size_t idx) {
    ControllerData out;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s（条目 %zu）", what, idx);
    out.error = buf;
    return out;
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
    char buf[24];
    if (v == (float)(long long)v)
        std::snprintf(buf, sizeof(buf), "%lld", (long long)v);
    else
        std::snprintf(buf, sizeof(buf), "%.1f", v);
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
            bool fromOk = false, toOk = false;
            for (const std::string& s : out.states) {
                fromOk |= s == t.from;
                toOk |= s == t.to;
            }
            if (!fromOk) return Fail("from 引用了未列出的状态", idx);
            if (!toOk) return Fail("to 引用了未列出的状态", idx);
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
    out += "],\n  \"transitions\": [";
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
    out += c.transitions.empty() ? "]\n}" : "\n  ]\n}";
    return out;
}

} // namespace lemon::editor
