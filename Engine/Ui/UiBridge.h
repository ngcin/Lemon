// Lemon 引擎 — UI 桥线格式（M6b 批③c，ADR-014 D2 M2/M3）
// C#（Lemon.SDK/UI.cs staging→Apply 序列化）与 C++（ScriptHost 拉取 → UiSubsystem
// 应用）之间的字节级契约：ops 命令缓冲（SceneOps 先例，单一提交口）+ UiEvent
// 事件队列（无回调跨边界）。两侧改动 = 破协议，必须同步。
// 纪律：本头零 RmlUi/SDL 类型（Engine/Scripting、Editor、Engine/Ui 三方共享）。
// UI 状态不入 StateHash、UI 交互不入输入快照——基准场零调用零漂移（ADR-014）。
#pragma once

#include <cstdint>
#include <cstring>

namespace lemon::ui {

enum class UiOpType : uint8_t {
    Show = 0,     // s0=doc；flags bit0 = modal（M1 模态标记：事件携带 + 键盘让出面扩大）
    Hide,         // s0=doc
    SetText,      // s0=doc s1=key s2=text
    SetAttr,      // s0=doc s1=key s2=attr s3=value
    SetClass,     // s0=doc s1=key s2=class；flags bit0 = 加（1）/删（0）
    SetStyle,     // s0=doc s1=key s2=prop s3=value
    SetInnerRml,  // s0=doc s1=key s2=rml
    SetItems,     // s0=doc s1=容器id s2=模板名 s3=行块；i0=行数（见行块格式）
};

/// 单条提交命令（32B）。文本字符串（doc/key/attr/class/prop/value/字段名值）一律
/// **NUL 终止**写入 arena，op 只记起始偏移（UI 字符串语义上不含 NUL）；SetItems 的
/// 行块（s3 指向）为自描述二进制（内含显式长度，见下）——解码零歧义、偏移可任意序。
struct UiOpC {
    uint8_t type;    // UiOpType
    uint8_t flags;   // 类型相关（Show/SetClass 见上）
    uint8_t strCount;// s0..s(strCount-1) 有效
    uint8_t reserved;
    uint32_t s0, s1, s2, s3; // arena 字节偏移（依类型定序，见 UiOpType 注）
    uint32_t i0, i1;         // SetItems: i0=行数
    uint32_t reserved2;       // 凑 32B 定长（无 u64——对齐即尺寸）
};
static_assert(sizeof(UiOpC) == 32, "UiOpC 布局固定（桥侧 blittable）");

/// 行块格式（SetItems 的 s3 指向 arena 内一段）：
///   i0 行 × [ u8 keyLen | key 字节 | u16 fieldCount | fieldCount × (u8 名长 | u16 值长 | 字节) ]
/// 字段序即文档模板字段集子集；未在模板集 = 响亮失败（M2 契约）。
/// 引擎侧按大端无关的本机字节序读定长头（两侧同编译约定 = x86/ARM LE，blittable 先例
/// BatchBlock 同款）；C# 侧 Lemon.SDK/UI.cs 独立编码，字节必须与此处解码一致。

enum class UiEventKind : uint8_t {
    Click = 0,          // data-event 元素点击（ev = 语义名；key = 元素 id 或 容器/条目key）
    Change,             // 表单控件值落定（payload = 值；input 的每次提交边界 + 失焦）
    Submit,             // form 原生提交（ev="submit"）
    Hover,              // 留位不派发（M5 波2 tooltip 归属）
    DocumentReloaded,   // 热重载后（M2 契约：C# 重灌数据；key 空）
};

/// UI 事件（172B 固定，低频点击级）。doc/key/ev/payload 截到容量-1（snprintf 静默——
/// UI id/语义名/表单值设计期短，超长属文档作者笔误，不入契约错误计数）；生产侧队列
/// 上限 256、超量静默丢弃防洪水（UiSubsystem OnRmlEvent），每帧派发批量见
/// kUiEventsPerDrain——与 ops 侧"超容量响亮 -1"不同档：事件是表现层回执，丢得起。
struct UiEventC {
    uint8_t kind;       // UiEventKind
    uint8_t modal;      // 事件时源文档模态标记
    uint16_t reserved;
    char doc[32];       // 文档名（资产 relPath 惯例）
    char key[48];       // 元素 id 或 容器id/条目key（SetItems 稳定 key 寻址）
    char ev[16];        // data-event 语义名（"pick"/"tab"/"build"…；非 data-event 元素 = 空）
    char payload[64];   // Change = 控件值；Click/Submit 可空
    float wx, wy;       // 世界坐标位（M4 拖放预留；波1 恒 0）
};
static_assert(sizeof(UiEventC) == 172, "UiEventC 布局固定（桥侧 blittable）");

/// 每帧拉取容量（ScriptHost 侧缓冲；溢出 = 红字截断 + 丢余量）。一屏富 UI 实测量级 < 8KB；
/// 图鉴 500 条实耗 ≈ 50KB（≈ 77% 占用贴边，2026-09-29 审核复核——C# EstimateBytes 的 ×3
/// 是 UTF-8 上界仅用于预扩容（500 条上界 ≈ 119KB），本上限检查的是实写字节。③e 判据加
/// 实耗断言；再涨 = 分帧提交 / 抬本值 / 字段去重三选）
inline constexpr uint32_t kUiOpsPerFrame = 256;
inline constexpr uint32_t kUiArenaBytesPerFrame = 64 * 1024;
inline constexpr uint32_t kUiEventsPerDrain = 64;

} // namespace lemon::ui
