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
    std::string out = "{\n  \"schemaVersion\": 1,\n  \"name\": \"";
    out += c.name;
    out += "\",\n  \"fps\": ";
    out += fpsBuf;
    out += ",\n  \"loop\": ";
    out += c.loopMode != 0 ? "true" : "false";
    if (c.loopMode == 2) out += ",\n  \"loopMode\": 2"; // T3b-2：仅 PingPong 落盘
    out += ",\n  \"frames\": [";
    // 帧行 = Samples/yami 既有 .clip 同款多行对象（规范格式：面板保存后旧档 diff
    // 只见被改字段；生成器单行档一旦经面板保存也归一到本格式）
    for (size_t i = 0; i < c.frames.size(); ++i) {
        char fr[112];
        std::snprintf(fr, sizeof(fr),
                      "%s\n    {\n      \"sheet\": \"%s\",\n      \"cell\": %u\n    }",
                      i ? "," : "", AssetDatabase::GuidToHex(c.frames[i].sheetGuid).c_str(),
                      c.frames[i].cell);
        out += fr;
    }
    out += c.frames.empty() ? "]\n}" : "\n  ]\n}";
    return out;
}

} // namespace lemon::editor
