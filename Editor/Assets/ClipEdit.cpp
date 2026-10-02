// Lemon 编辑器 — ClipEdit 实现（M6a 批② T3；见 ClipEdit.h 契约注记）
#include "Assets/ClipEdit.h"

#include <cstdio>

#include "Assets/AssetDatabase.h"
#include "nlohmann/json.hpp"

namespace lemon::editor {

namespace {
ClipData Fail(const char* what, size_t frame) {
    ClipData out;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s（帧 %zu）", what, frame);
    out.error = buf;
    return out;
}
} // namespace

ClipData ParseClipJson(std::string_view text) {
    ClipData out;
    const nlohmann::json doc =
        nlohmann::json::parse(text, nullptr, false); // 不抛（discarded = 坏档）
    if (doc.is_discarded() || !doc.is_object())
        return Fail("clip 解析失败：非 JSON 对象", 0);
    if (!doc.contains("frames") || !doc.at("frames").is_array())
        return Fail("clip 解析失败：缺 frames[] 数组", 0);
    if (!doc.contains("fps") || !doc.at("fps").is_number())
        return Fail("clip 解析失败：缺 fps 数值", 0);
    if (doc.contains("name") && doc.at("name").is_string())
        out.name = doc.at("name").get<std::string>();
    out.fps = doc.at("fps").get<float>();
    // T3b-2：loopMode 可选字段优先；缺省从 legacy "loop" bool 派生（缺 loop = true）
    if (doc.contains("loopMode") && doc.at("loopMode").is_number_integer()) {
        const int64_t m = doc.at("loopMode").get<int64_t>();
        out.loopMode = m >= 0 && m <= 2 ? (int)m : 1; // 越界防御 = Loop
    } else {
        // legacy "loop" 需类型预检（review 2026-10-02 #30）：手写/外部工具常见的
        // "loop": 1 直接 get<bool>() 会抛 nlohmann type_error 穿透调用链（调用点
        // 均 bare 调用无 try/catch）= std::terminate——本函数契约"坏档不炸编辑器"
        if (doc.contains("loop") && !doc.at("loop").is_boolean())
            return Fail("clip 解析失败：loop 非布尔（手写档请用 true/false）", 0);
        out.loopMode = !doc.contains("loop") || doc.at("loop").get<bool>() ? 1 : 0;
    }
    for (const nlohmann::json& fr : doc.at("frames")) {
        const size_t idx = out.frames.size();
        if (!fr.is_object() || !fr.contains("sheet") || !fr.contains("cell"))
            return Fail("帧缺 sheet/cell 字段", idx);
        if (!fr.at("sheet").is_string() || !fr.at("cell").is_number())
            return Fail("帧 sheet/cell 类型不对", idx);
        const std::string hex = fr.at("sheet").get<std::string>();
        const uint64_t guid = AssetDatabase::HexToGuid(hex.c_str());
        if (guid == 0) return Fail("sheet 非 16 位 hex GUID", idx);
        const nlohmann::json& cell = fr.at("cell");
        if (cell.is_number_unsigned()) {
            out.frames.push_back({guid, cell.get<uint32_t>()});
        } else if (cell.is_number_integer() && cell.get<int64_t>() >= 0) {
            out.frames.push_back({guid, (uint32_t)cell.get<int64_t>()});
        } else {
            return Fail("cell 负数/超界", idx);
        }
    }
    // T3d 批③：帧事件表（可选；帧号越帧表 = 解析期拒绝——打点必须落在实帧上）
    if (doc.contains("events")) {
        if (!doc.at("events").is_array()) return Fail("events 非数组", out.frames.size());
        for (const nlohmann::json& ev : doc.at("events")) {
            const size_t idx = out.events.size();
            if (!ev.is_object() || !ev.contains("frame") || !ev.at("frame").is_number())
                return Fail("事件缺 frame 数值", idx);
            const nlohmann::json& fr = ev.at("frame");
            uint32_t frame;
            if (fr.is_number_unsigned())
                frame = fr.get<uint32_t>();
            else if (fr.is_number_integer() && fr.get<int64_t>() >= 0)
                frame = (uint32_t)fr.get<int64_t>();
            else
                return Fail("事件 frame 负数/超界", idx);
            if (frame >= out.frames.size()) return Fail("事件帧号越帧表", idx);
            uint32_t id = 0;
            if (ev.contains("id")) {
                if (!ev.at("id").is_number()) return Fail("事件 id 非数值", idx);
                const nlohmann::json& j = ev.at("id");
                if (j.is_number_unsigned())
                    id = j.get<uint32_t>();
                else if (j.is_number_integer() && j.get<int64_t>() >= 0)
                    id = (uint32_t)j.get<int64_t>();
                else
                    return Fail("事件 id 负数/超界", idx);
            }
            out.events.push_back({frame, id});
        }
    }
    out.ok = true;
    return out;
}

std::string ClipToJson(const ClipData& c) {
    if (!c.ok) return {};
    char fpsBuf[24];
    if (c.fps == (float)(long long)c.fps)
        std::snprintf(fpsBuf, sizeof(fpsBuf), "%lld", (long long)c.fps);
    else
        std::snprintf(fpsBuf, sizeof(fpsBuf), "%.1f", c.fps);
    // 名字字段必须经 JsonEscape（review 2026-10-02 #5）——名字含引号/反斜杠时
    // 原样拼接写出非法 JSON，TrySave 覆写原档即数据丢失
    std::string out = "{\n  \"schemaVersion\": 1,\n  \"name\": ";
    out += JsonEscape(c.name);
    out += ",\n  \"fps\": ";
    out += fpsBuf;
    out += ",\n  \"loop\": ";
    out += c.loopMode != 0 ? "true" : "false";
    if (c.loopMode == 2) out += ",\n  \"loopMode\": 2"; // T3b-2：仅 PingPong 落盘
    out += ",\n  \"frames\": [";
    // 帧行 = Samples/yami 既有 .anim 同款多行对象（规范格式：面板保存后旧档 diff
    // 只见被改字段；生成器单行档一旦经面板保存也归一到本格式）
    for (size_t i = 0; i < c.frames.size(); ++i) {
        char fr[112];
        std::snprintf(fr, sizeof(fr),
                      "%s\n    {\n      \"sheet\": \"%s\",\n      \"cell\": %u\n    }",
                      i ? "," : "", AssetDatabase::GuidToHex(c.frames[i].sheetGuid).c_str(),
                      c.frames[i].cell);
        out += fr;
    }
    out += c.frames.empty() ? "]" : "\n  ]";
    // T3d 批③：帧事件表（仅非空时追加——旧档 no-edit 往返逐字节不变）
    if (!c.events.empty()) {
        out += ",\n  \"events\": [";
        for (size_t i = 0; i < c.events.size(); ++i) {
            char ev[96];
            if (c.events[i].id != 0)
                std::snprintf(ev, sizeof(ev), "%s\n    { \"frame\": %u, \"id\": %u }",
                              i ? "," : "", c.events[i].frame, c.events[i].id);
            else
                std::snprintf(ev, sizeof(ev), "%s\n    { \"frame\": %u }", i ? "," : "",
                              c.events[i].frame);
            out += ev;
        }
        out += "\n  ]";
    }
    out += "\n}";
    return out;
}

// ---- T3c 动画集（.override）----------------------------------------------------

namespace {
AnimSetData FailSet(const char* what, size_t seg) {
    AnimSetData out;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s（段 %zu）", what, seg);
    out.error = buf;
    return out;
}
} // namespace

AnimSetData ParseAnimSetJson(std::string_view text) {
    AnimSetData out;
    const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object())
        return FailSet("animset 解析失败：非 JSON 对象", 0);
    if (!doc.contains("segments") || !doc.at("segments").is_array())
        return FailSet("animset 解析失败：缺 segments[] 数组", 0);
    if (doc.contains("name") && doc.at("name").is_string())
        out.name = doc.at("name").get<std::string>();
    for (const nlohmann::json& sg : doc.at("segments")) {
        const size_t idx = out.segments.size();
        if (!sg.is_object() || !sg.contains("name") || !sg.contains("clip"))
            return FailSet("段缺 name/clip 字段", idx);
        if (!sg.at("name").is_string() || !sg.at("clip").is_string())
            return FailSet("段 name/clip 类型不对", idx);
        out.segments.push_back({sg.at("name").get<std::string>(),
                                AssetDatabase::HexToGuid(sg.at("clip").get<std::string>().c_str())});
        if (out.segments.back().name.empty())
            return FailSet("段 name 为空", idx);
        if (out.segments.back().clipGuid == 0)
            return FailSet("clip 非 16 位 hex GUID", idx);
    }
    out.ok = true;
    return out;
}

std::string AnimSetToJson(const AnimSetData& s) {
    if (!s.ok) return {};
    std::string out = "{\n  \"schemaVersion\": 1,\n  \"name\": ";
    out += JsonEscape(s.name);
    out += ",\n  \"segments\": [";
    // 定长 char[128] snprintf 换直接拼接（review 2026-10-02 #5）：段名 >约 63 字符
    // 被静默截断——下次解析名不符/长度失真；名字同时走 JsonEscape
    for (size_t i = 0; i < s.segments.size(); ++i) {
        out += i ? ",\n    {\n      \"name\": " : "\n    {\n      \"name\": ";
        out += JsonEscape(s.segments[i].name);
        out += ",\n      \"clip\": \"";
        out += AssetDatabase::GuidToHex(s.segments[i].clipGuid);
        out += "\"\n    }";
    }
    out += s.segments.empty() ? "]\n}" : "\n  ]\n}";
    return out;
}

std::string JsonEscape(std::string_view s) {
    // 转义规则单源：nlohmann dump（RFC 8259；ensure_ascii=false——中文原样可读）。
    // 输出含首尾引号，直接作为 JSON 字符串字面量嵌入
    return nlohmann::json(s).dump();
}

} // namespace lemon::editor
