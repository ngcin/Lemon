// Lemon 编辑器 — CSV 解析 + .tab 表格资产序列化实现（M6a 批② T1；ADR-012 D1）
// nlohmann/json 只进本 .cpp（同 AssetDatabase.cpp 口径）。
#include "Assets/Csv.h"

#include <algorithm>

#include <nlohmann/json.hpp>

namespace lemon::editor {
namespace {

using Json = nlohmann::json;

/// 码点数（UTF-8 续字节不计）——单元格上限按字符计而非字节（中文格 128 字 ≠ 384 字节）
uint32_t CountCodepoints(std::string_view s) {
    uint32_t n = 0;
    for (const unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

std::string NumLabel(uint32_t v) { return std::to_string(v); }

} // namespace

bool IsValidUtf8(std::string_view s) {
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x80) {
            ++i;
            continue;
        }
        int len;
        uint32_t cp;
        if ((c & 0xE0) == 0xC0) {
            len = 2;
            cp = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
            cp = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
            cp = c & 0x07;
        } else {
            return false; // 孤立续字节 / 0xF8+ 保留段
        }
        if (i + (size_t)len > n) return false; // 截断序列
        for (int k = 1; k < len; ++k) {
            const unsigned char cc = (unsigned char)s[i + (size_t)k];
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (len == 2 && cp < 0x80) return false; // 过长编码
        if (len == 3 && cp < 0x800) return false;
        if (len == 4 && cp < 0x10000) return false;
        if (cp >= 0xD800 && cp <= 0xDFFF) return false; // UTF-16 代理区
        if (cp > 0x10FFFF) return false;
        i += (size_t)len;
    }
    return true;
}

TableData ParseCsv(std::string_view text) {
    TableData out;
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF &&
        (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        text.remove_prefix(3); // Excel UTF-8 CSV 带 BOM（中文表头刚需）
    if (!IsValidUtf8(text)) {
        out.error = "非 UTF-8 编码（Excel 导出请选「UTF-8 CSV」）";
        return out;
    }

    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string cell;
    bool inQuotes = false;
    bool any = false; // 本记录是否消耗过字符（区分空行与全空数据行）
    auto endRecord = [&]() {
        if (any || !cell.empty() || !row.empty()) {
            row.push_back(cell);
            rows.push_back(std::move(row));
        }
        row.clear();
        cell.clear();
        any = false;
    };

    const size_t n = text.size();
    for (size_t i = 0; i < n; ++i) {
        const char ch = text[i];
        if (inQuotes) {
            if (ch == '"') {
                if (i + 1 < n && text[i + 1] == '"') { // "" 转义引号
                    cell += '"';
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                cell += ch; // 含逗号/换行原样入格
            }
            continue;
        }
        switch (ch) {
            case '"': inQuotes = true; any = true; break;
            case ',': row.push_back(cell); cell.clear(); any = true; break;
            case '\r':
                if (i + 1 < n && text[i + 1] == '\n') ++i; // CRLF 归一
                endRecord();
                break;
            case '\n': endRecord(); break;
            default: cell += ch; any = true; break;
        }
    }
    endRecord(); // 无尾换行的末行

    return NormalizeTable(std::move(rows));
}

TableData NormalizeTable(std::vector<std::vector<std::string>> rows) {
    TableData out;
    if (rows.empty()) {
        out.error = "空表（至少需要列头行）";
        return out;
    }
    size_t cols = 0;
    for (const auto& r : rows) cols = std::max(cols, r.size());
    if (cols == 0) {
        out.error = "空表（至少需要一列）";
        return out;
    }
    if (cols > kTableMaxCols) {
        out.error = "列数超限（" + NumLabel((uint32_t)cols) + " > " +
                    NumLabel(kTableMaxCols) + "）";
        return out;
    }
    if (rows.size() > kTableMaxRows) {
        out.error = "行数超限（" + NumLabel((uint32_t)rows.size()) + " > " +
                    NumLabel(kTableMaxRows) + "）";
        return out;
    }
    for (auto& r : rows) {
        r.resize(cols); // 参差行补空 → 矩形
        for (const auto& c : r) {
            const uint32_t cps = CountCodepoints(c);
            if (cps > kTableMaxCellChars) {
                out.error = "单元格超限（" + NumLabel(cps) + " 字符 > " +
                            NumLabel(kTableMaxCellChars) + "）";
                return out;
            }
        }
    }
    out.rows = std::move(rows);
    out.ok = true;
    return out;
}

std::string TableToJson(const std::string& name,
                        const std::vector<std::vector<std::string>>& rows) {
    TableData n = NormalizeTable(rows);
    if (!n.ok) return std::string();
    Json doc;
    doc["schemaVersion"] = 1;
    doc["name"] = name;
    doc["rows"] = n.rows; // STL 嵌套容器隐式转换（nlohmann 既有能力）
    return doc.dump(2);
}

TableData ParseTableJson(std::string_view text) {
    TableData out;
    Json doc = Json::parse(text, nullptr, false);
    if (doc.is_discarded()) {
        out.error = "JSON 语法错误";
        return out;
    }
    if (!doc.is_object()) {
        out.error = "顶层须为 JSON 对象";
        return out;
    }
    const auto sv = doc.find("schemaVersion");
    if (sv != doc.end() && (!sv->is_number_unsigned() || sv->get<uint64_t>() != 1)) {
        out.error = "未知 schemaVersion（仅支持 1）";
        return out;
    }
    const auto rowsIt = doc.find("rows");
    if (rowsIt == doc.end() || !rowsIt->is_array()) {
        out.error = "缺 rows 数组";
        return out;
    }
    std::vector<std::vector<std::string>> rows;
    rows.reserve(rowsIt->size());
    for (const Json& r : *rowsIt) {
        if (!r.is_array()) {
            out.error = "rows 内行须为数组";
            return out;
        }
        std::vector<std::string> row;
        row.reserve(r.size());
        for (const Json& c : r) {
            if (c.is_string()) row.push_back(c.get<std::string>());
            else if (c.is_number() || c.is_boolean())
                row.push_back(c.dump()); // ADR-012 示例含裸数值格 → 归一为字符串
            else if (c.is_null()) row.push_back({});
            else {
                out.error = "单元格须为字符串/数值/布尔";
                return out;
            }
        }
        rows.push_back(std::move(row));
    }
    return NormalizeTable(std::move(rows));
}

} // namespace lemon::editor
