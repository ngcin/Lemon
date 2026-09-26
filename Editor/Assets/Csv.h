// Lemon 编辑器 — CSV 解析 + .tab 表格资产序列化（M6a 批② T1；ADR-012 D1）
// 纯文本逻辑，零 ImGui/GPU 依赖（lemon-editor-core，可单测）。
//   * .tab = JSON 全字符串格网格（第 0 行 = 列头；后缀 .tab 为 2026-09-26 用户定名，
//     ADR-012 原文 .table 按修订注记读取）：
//       { "schemaVersion": 1, "name": "weapons", "rows": [["id","label"], ["a","甲"]] }
//   * 防呆上限（ADR-012 D1）：64 列 × 1024 行 × 单元格 128 字符（码点计），超限红字拒入。
//   * GBK 等误编码不支持（v1 中文单语 + "Excel 导出选 UTF-8 CSV"）：非 UTF-8 输入拒入。
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lemon::editor {

inline constexpr uint32_t kTableMaxCols = 64;
inline constexpr uint32_t kTableMaxRows = 1024;
inline constexpr uint32_t kTableMaxCellChars = 128;

/// 解析产物：rows[r][c] 字符串网格（矩形——参差行补空到最大列数；rows[0] = 列头）
struct TableData {
    bool ok = false;
    std::string error;                          // 失败原因（Console 红字 / 测试断言）
    std::vector<std::vector<std::string>> rows;
    uint32_t Cols() const { return rows.empty() ? 0 : (uint32_t)rows[0].size(); }
};

/// CSV 文本 → 网格。规则：逗号分隔；"..." 包裹格内可含逗号/换行，"" 转义引号；
/// \r\n / \n / \r 换行等价；剥 UTF-8 BOM；空行跳过（文件尾换行不产生幽灵行）。
TableData ParseCsv(std::string_view text);

/// 网格 → .tab JSON 文本（schemaVersion/name/rows；dump(2) 缩进可读）。
/// 输入先经 NormalizeTable 校验，非法（超限/空表）返回空串。
std::string TableToJson(const std::string& name,
                        const std::vector<std::vector<std::string>>& rows);

/// .tab JSON 文本 → 网格（AssetBrowser 表格区渲染 + roundtrip 校验）。
/// 宽松归一：数值/布尔格转字符串（ADR-012 示例含裸数值）；坏 JSON/非矩形/超限 ok=false。
TableData ParseTableJson(std::string_view text);

/// 网格矩形化 + 上限校验（ParseCsv/ParseTableJson/表格区编辑写回共用）。
/// 参差行补空；列 >64 / 行 >1024 / 单元格 >128 码点 / 空表 → ok=false + error。
TableData NormalizeTable(std::vector<std::vector<std::string>> rows);

/// UTF-8 合法性（过长编码/代理区/截断续字节全拒；ParseCsv 的 GBK 拒入依据）
bool IsValidUtf8(std::string_view s);

} // namespace lemon::editor
