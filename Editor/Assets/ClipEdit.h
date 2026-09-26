// Lemon 编辑器 — .clip 帧动画资产解析/序列化（M6a 批② T3 AnimationPanel 的
// 纯逻辑半；06 §2.2 / M5.md §16.2 D2 schema）。
// 纯文本逻辑，零 ImGui/GPU 依赖（lemon-editor-core，可单测——Csv.h 同款形态）。
//   * schema（与运行时 BuildPlayClipCache 同一宽容度）：
//       { "schemaVersion": 1, "name": "hero-walk", "fps": 8, "loop": true,
//         "frames": [ { "sheet": "<guidHex16>", "cell": 0 }, ... ] }
//   * 帧引用存 精灵表资产 GUID + 切片序号（行优先），不存 spriteId（manifest
//     重排不断链）——spriteId 解析归 EnterPlay 快照，编辑器面只管 guid+cell。
//   * 序列化手写（非 nlohmann dump）：字段序/缩进与既有 .clip 文件逐字符同型
//     ——AnimationPanel 保存后 git diff 只见被改字段（验收② roundtrip 口径）。
//   * fps 整值输出整数、非整值一位小数（面板 DragInt 恒整；手写档 7.5 兼容）。
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lemon::editor {

/// 帧引用：sheet = 精灵表资产 GUID；cell = 切片序号（行优先，0 起）
struct ClipFrame {
    uint64_t sheetGuid = 0;
    uint32_t cell = 0;
    friend bool operator==(const ClipFrame&, const ClipFrame&) = default; // roundtrip 断言
};

/// 解析产物（TableData 同款 ok/error 约定；坏档 ok=false 不炸编辑器）
struct ClipData {
    bool ok = false;
    std::string error;
    std::string name;          // 缺省 ""（新建 clip 落盘前补文件名）
    float fps = 8.0f;
    /// T3b-2：循环模式（0=Once / 1=Loop / 2=PingPong；Animator2D.loop 同值域）。
    /// 序列化：legacy "loop" bool 恒写（=mode!=0）；仅 PingPong 追加 "loopMode":2
    /// ——旧档 no-edit 往返逐字节不变。档面值 = 创建默认，运行时权威在实体。
    int loopMode = 1;
    std::vector<ClipFrame> frames;
};

/// .clip JSON 文本 → ClipData。frames[]/fps 必需；loop/name 可缺省；空帧表合法
///（保存侧校验 ≥1 帧，解析侧不拦——新建空 clip 也要能进编辑态）。sheet 非 16 位
/// hex / cell 负数 / 类型不对 → ok=false + error（首个错误即返回）。
ClipData ParseClipJson(std::string_view text);

/// ClipData → .clip JSON 文本（手写定版格式，见文件头）。ok=false 输入 → 空串。
std::string ClipToJson(const ClipData& c);

} // namespace lemon::editor
