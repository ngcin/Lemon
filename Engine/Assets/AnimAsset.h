// Lemon 引擎 — .anim 帧动画资产解析/序列化（M6a 批② T3 AnimationPanel 的
// 纯逻辑半；06 §2.2 / M5.md §16.2 D2 schema；M7a 批② 自 Editor/Assets/ClipEdit
// 下沉引擎——运行时 BuildPlayClipCache 与编辑器面板共用同一宽容度）。
// 纯文本逻辑，零 ImGui/GPU 依赖（可单测）。
//   * schema（与运行时装载同一宽容度）：
//       { "schemaVersion": 1, "name": "hero-walk", "fps": 8, "loop": true,
//         "frames": [ { "sheet": "<guidHex16>", "cell": 0 }, ... ] }
//   * 帧引用存 精灵表资产 GUID + 切片序号（行优先），不存 spriteId（manifest
//     重排不断链）——spriteId 解析归装载期（SpriteRefs 归一），编辑器面只管 guid+cell。
//   * 序列化手写（非 nlohmann dump）：字段序/缩进与既有 .anim 文件逐字符同型
//     ——面板保存后 git diff 只见被改字段（验收② roundtrip 口径）。
//   * fps 整值输出整数、非整值一位小数（面板 DragInt 恒整；手写档 7.5 兼容）。
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lemon::assets {

/// 帧引用：sheet = 精灵表资产 GUID；cell = 切片序号（行优先，0 起）
struct ClipFrame {
    uint64_t sheetGuid = 0;
    uint32_t cell = 0;
    friend bool operator==(const ClipFrame&, const ClipFrame&) = default; // roundtrip 断言
};

/// T3d 批③：帧事件打点（id = 作者自定义 16 位标识；命名事件列 v1.1）。
/// 运行时语义：进入该帧（帧号跨入）→ AnimFrame 事件（user=id, userArg=clipId）。
struct ClipEventEdit {
    uint32_t frame = 0; // 帧号（编辑侧 u32 宽容；运行时 ClipEventDef u16 收窄）
    uint32_t id = 0;    // 事件 id（缺省 0；C# 按 (clipId,id) 分发）
    friend bool operator==(const ClipEventEdit&, const ClipEventEdit&) = default;
};

/// 解析产物（TableData 同款 ok/error 约定；坏档 ok=false 不炸调用方）
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
    /// T3d 批③：帧事件表（可选 "events":[{frame,id}]；空 = 无事件。序列化仅
    /// 非空时追加——旧档 no-edit 往返逐字节不变；帧号越帧表 = 解析期拒绝）
    std::vector<ClipEventEdit> events;
};

/// .anim JSON 文本 → ClipData。frames[]/fps 必需；loop/name 可缺省；空帧表合法
///（保存侧校验 ≥1 帧，解析侧不拦——新建空 clip 也要能进编辑态）。sheet 非 16 位
/// hex / cell 负数 / 类型不对 → ok=false + error（首个错误即返回）。
ClipData ParseClipJson(std::string_view text);

/// ClipData → .anim JSON 文本（手写定版格式，见文件头）。ok=false 输入 → 空串。
std::string ClipToJson(const ClipData& c);

// ---- T3c 动画集（.override；Unity AnimatorController 的壳 / Godot SpriteFrames 的
// 工作台单入口，见 docs/Plans/M6a T3c 决策记录）--------------------

/// 集段引用：name = 段名（集内唯一 = 运行时按名解析键，文件系统同名约束不跨集）；
/// clip = 段 .anim 资产 GUID（段身份 = 文件 GUID——改名/重建安全，集只存引用）
struct AnimSetSeg {
    std::string name;
    uint64_t clipGuid = 0;
    friend bool operator==(const AnimSetSeg&, const AnimSetSeg&) = default; // roundtrip 断言
};

/// .override 解析产物（ClipData 同款 ok/error 约定）
struct AnimSetData {
    bool ok = false;
    std::string error;
    std::string name; // 集名（缺省 ""；新建时落盘前补文件名）
    std::vector<AnimSetSeg> segments;
};

/// .override JSON 文本 → AnimSetData。segments[] 必需；name 可缺省；空集合法（新建
/// 空集也要能进工作台）。段缺 name/空 name / clip 非 16 位 hex → ok=false + error。
AnimSetData ParseAnimSetJson(std::string_view text);

/// AnimSetData → .override JSON 文本（ClipToJson 同款手写定版格式）。ok=false 输入 → 空串。
std::string AnimSetToJson(const AnimSetData& s);

/// JSON 字符串字面量转义（含首尾引号；nlohmann dump 同款规则，非 ASCII 原样保留）。
/// 手写序列化器嵌名字字段共用（clip/animset/controller）——名字含 `"`/`\`/控制
/// 字符时不转义会写出非法 JSON，下次解析失败且原档已被覆写 = 数据丢失
///（review 2026-10-02 #5）
std::string JsonEscape(std::string_view s);

/// 动画资产名（段名/集名）共用校验——改名/建段入口单源（M7a 批① D7 残余：
/// 校验硬化）。拒：空 / `/` / `\` / `..` / `"` / 控制字符 / >64 字节（名字 =
/// 文件名母体 + 集内按名解析键，怪字符推迟问题到运行时、超长撞文件系统上限）。
/// 返回 false 时 why 可带可直接上屏的原因（可空）。
bool ValidateAssetName(std::string_view n, std::string* why = nullptr);

/// 帧事件越界清理（M7a 批① M22）：删 `frame ≥ frames.size()` 的事件，返回删除
/// 数。事件无 UI 编辑入口（作者面 = 手写/表驱动），删帧后不清 → TrySave 的
/// roundtrip 预验必拒（解析侧对越界事件硬拒）→ 保存链自锁。
size_t SanitizeClipEvents(ClipData& c);

} // namespace lemon::assets
