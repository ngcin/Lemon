// Lemon 编辑器 — Inspector 面板（M4.md §2.2；内核 #6 消费端）
// 反射驱动：ComponentRegistry 字段表 + FieldEditorMeta 特性 → 控件；新增登记组件
// 零编辑器代码出现在此（验收点）。编辑直写组件 + ctx.dirty；
// Undo 属性轨 M4.2 接入（IsItemActivated/Deactivated 拖拽合并）。
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>

#include "App/EditorApp.h"
#include "Assets/AssetDatabase.h"
#include "Components/BehaviorComponents.h"
#include "Components/CoreComponents.h"
#include "Components/RenderComponents.h"
#include "Core/Log.h"
#include "ECS/ComponentRegistry.h"
#include "EditorContext.h"
#include "Panels/BuiltInPanels.h"
#include "Scripting/ScriptBox.h"
#include "Tooling/TestHooks.h"
#include "Tooling/Theme.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace lemon::editor {
namespace {

using ecs::ComponentMeta;
using ecs::FieldEditorMeta;
using ecs::FieldHint;
using ecs::FieldMeta;
using ecs::FieldType;

const ecs::FieldEditorMeta& EdOf(const ComponentMeta& meta, uint16_t i) {
    static const ecs::FieldEditorMeta kNone{};
    return meta.editorMeta ? meta.editorMeta[i] : kNone;
}

bool ProtectedComponent(const char* name) {
    return std::strcmp(name, "Transform2D") == 0 || std::strcmp(name, "Meta") == 0 ||
           std::strcmp(name, "DestroyQueueTag") == 0;
}

/// Team 下拉名（TeamTable 无名表——M4.4 teams.json 资产化后换真名）。
/// 只返语义名，序号前缀由调用方拼——未命名档位多行同文会撞 ImGui ID
/// （实测：4 个 "?" Selectable 同 ID → "4 visible items with conflicting ID"）。
const char* TeamName(uint32_t t) {
    switch (t) {
        case 0: return "player";
        case 1: return "monsters";
        case 2: return "neutral";
        case 3: return "playerBullets";
        default: return "?";
    }
}

float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---- label-scrub（M4.7d 回捞砍单候补首位）：拖字段名横向改值（Unity 拖 label 手感）----
// Text 无交互 ID（IsItemActive 恒假）→ 手动跟踪：hover+左键按下接管，按住期间
// 逐帧累计 dx。同一时刻至多一个 label 在拖，全局单份状态即够。
struct LabelScrubState {
    ImGuiID id = 0;     // 拖拽中的字段 label ID（0 = 无）
    float lastX = 0.0f; // 上帧鼠标 X（dx 差分）
    float acc = 0.0f;   // 整数步余数累计（慢拖不丢步）
} scrub_;
bool g_scrubActive = false; // 本帧拖拽进行中（DrawComponent 属性轨 → anyActive）
bool g_scrubEnded = false;  // 本帧拖拽刚结束（→ anyDeactivated 提交属性轨）
// 注：两标志兼作"控件后缀捕获"通道——末控件后还画 Text 后缀的分支（Degree 字段）
// 须在后缀前就地 |= 进来（后缀会把最后项换成恒不 active 的 Text）。

/// 在 label 文本项之后调用（hover 语义跟随上一项）。返回本帧 dx（0 = 未拖）。
float LabelScrubDelta(ImGuiID id) {
    ImGuiIO& io = ImGui::GetIO();
    if (scrub_.id == id) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            scrub_.id = 0;
            g_scrubEnded = true;
            return 0.0f;
        }
        const float dx = io.MousePos.x - scrub_.lastX;
        scrub_.lastX = io.MousePos.x;
        g_scrubActive = true;
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        return dx;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        scrub_.id = id;
        scrub_.lastX = io.MousePos.x;
        scrub_.acc = 0.0f;
        g_scrubActive = true;
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        return 0.0f;
    }
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    return 0.0f;
}

/// 浮点 dx → 整数步（余数跨帧累计，慢拖不丢步）
int ScrubSteps(float dx) {
    scrub_.acc += dx;
    const int s = (int)scrub_.acc;
    scrub_.acc -= (float)s;
    return s;
}

/// 整型字段 scrub 落值（钳类型域；返回是否写入）
template <typename T>
bool ScrubInt(float dx, void* p) {
    if (dx == 0.0f) return false;
    const int step = ScrubSteps(dx);
    if (step == 0) return false;
    if constexpr (std::is_unsigned_v<T> && sizeof(T) == 8) { // uint64 不入 int64 域
        uint64_t& cur = *(uint64_t*)p;
        if (step > 0) cur += (uint64_t)step;
        else if ((uint64_t)(-(int64_t)step) <= cur) cur -= (uint64_t)(-(int64_t)step);
        else cur = 0;
    } else {
        int64_t nv = (int64_t)*(T*)p + step;
        if (nv < (int64_t)std::numeric_limits<T>::min())
            nv = (int64_t)std::numeric_limits<T>::min();
        if (nv > (int64_t)std::numeric_limits<T>::max())
            nv = (int64_t)std::numeric_limits<T>::max();
        *(T*)p = (T)nv;
    }
    return true;
}

/// 枚举控件（整数字段 + kFieldEnum）。返回是否写入
bool DrawEnumControl(const FieldMeta& f, const FieldEditorMeta& ed, uint8_t* p) {
    auto read = [&]() -> int64_t {
        switch (f.type) {
            case FieldType::UInt8: return *(uint8_t*)p;
            case FieldType::UInt16: return *(uint16_t*)p;
            case FieldType::UInt32: return *(uint32_t*)p;
            case FieldType::Int32: return *(int32_t*)p;
            default: return -1;
        }
    };
    auto write = [&](uint32_t v) {
        switch (f.type) {
            case FieldType::UInt8: *(uint8_t*)p = (uint8_t)v; break;
            case FieldType::UInt16: *(uint16_t*)p = (uint16_t)v; break;
            case FieldType::UInt32: *(uint32_t*)p = v; break;
            case FieldType::Int32: *(int32_t*)p = (int32_t)v; break;
            default: break;
        }
    };
    const int64_t cur = read();
    const char* label = (uint32_t)cur < ed.enumCount ? ed.enumNames[cur] : "?";
    bool wrote = false;
    if (ImGui::BeginCombo("##v", label)) {
        for (uint32_t i = 0; i < ed.enumCount; ++i)
            if (ImGui::Selectable(ed.enumNames[i], (int64_t)i == cur)) {
                write(i);
                wrote = true;
            }
        // 命中不可提前 return：Begin/EndCombo 必须配对（原 early-return 跳过
        // EndCombo = ImGui ID/弹出栈错乱）
        ImGui::EndCombo();
    }
    return wrote;
}

/// sprite 资产槽（FieldHint::AssetRef + UInt32 spriteId；M4.4 接通）。
/// 值 = AtlasRegistry spriteId；UI 反查资产（guid 名/缩略图）。拖 AssetBrowser
/// sprite 进来 = 设引用；下拉全列；右键清空。最后绘制的控件是 combo →
/// DrawComponent 的 IsItemDeactivated 属性轨照常捕获。
/// 赋值同时置 flags.enabled——「指定了图片 = 要显示」（也救旧档 flags=0 的禁用实例；
/// 2026-09-21：默认禁用 + 提取静默跳过曾使 Add Component 路径完全不显示）。
bool DrawSpriteSlot(EditorApp& app, uint8_t* p, ecs::SpriteRenderer& sr) {
    EditorContext& ctx = app.Ctx();
    AssetDatabase& db = ctx.Assets();
    uint32_t& id = *(uint32_t*)p;
    const AssetEntry* entry = db.FindBySpriteId(id);

    // 缩略图（悬空 = 红框占位）
    if (void* thumb = entry && !entry->missing ? app.AssetGpu().Thumbnail(entry->guid) : nullptr) {
        ImGui::Image(thumb, ImVec2(28, 28));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, theme::kPlayStop); // 悬空槽红框占位
        ImGui::Button(entry ? "×" : "·", ImVec2(28, 28));
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();

    char label[64];
    if (entry)
        std::snprintf(label, sizeof(label), "%s%s", entry->missing ? "⚠ " : "",
                      entry->FileName().c_str());
    else if (id != 0)
        std::snprintf(label, sizeof(label), "内置 #%u", id);
    else
        std::snprintf(label, sizeof(label), "(无)");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##v", label)) {
        for (const auto& e : db.Entries()) {
            if (e.type != AssetType::Sprite) continue;
            // 列表项用 relPath（唯一）：不同子目录同名文件用 FileName 会撞 ImGui ID
            char item[160];
            std::snprintf(item, sizeof(item), "%s%s%s",
                          e.spriteId == id ? "√ " : "", e.missing ? "⚠ " : "",
                          e.relPath.c_str());
            if (ImGui::Selectable(item, e.spriteId == id)) {
                id = e.spriteId;
                sr.spriteGuid = e.guid; // M6a 批⓪ T2：guid/id 双写（guid 真源）
                sr.flags |= ecs::kSrEnabled;
                ctx.dirty = true;
            }
        }
        ImGui::EndCombo();
    }
    // 资产拖入（AssetBrowser sprite；kind 0）
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pay = ImGui::AcceptDragDropPayload("LemonAsset")) {
            AssetDragPayload d{};
            std::memcpy(&d, pay->Data, sizeof(d));
            if (d.kind == 0 && d.spriteId != 0) {
                id = d.spriteId;
                sr.spriteGuid = d.guid; // M6a 批⓪ T2：拖入载荷带 guid（AssetBrowser）
                sr.flags |= ecs::kSrEnabled;
                ctx.dirty = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem("slot_ctx")) {
        if (ImGui::MenuItem("清空引用")) {
            id = 0;
            sr.spriteGuid = 0; // M6a 批⓪ T2：双清（防残留 guid 在下轮装载复活旧引用）
            ctx.dirty = true;
        }
        ImGui::EndPopup();
    }
    return false; // 写入已就地完成（combo 尾置 → 属性轨由 Deactivated 捕获）
}

/// clip 资产槽（FieldHint::ClipRef；M5 批③）。值 = .anim 资产 GUID 低 32 位
/// （Animator2D.clipId；映射约定同 prefabId）。下拉全列 / AssetBrowser 拖入（kind 4）/
/// 右键清空。写入就地完成（combo 尾置 → 属性轨由 Deactivated 捕获）。
bool DrawClipSlot(EditorApp& app, uint8_t* p) {
    EditorContext& ctx = app.Ctx();
    AssetDatabase& db = ctx.Assets();
    uint32_t& id = *(uint32_t*)p;
    const AssetEntry* entry = db.FindClipByLowId(id);

    char label[96];
    if (entry)
        std::snprintf(label, sizeof(label), "%s%s", entry->missing ? "⚠ " : "",
                      entry->relPath.c_str());
    else if (id != 0)
        std::snprintf(label, sizeof(label), "悬空 clip %08x", id);
    else
        std::snprintf(label, sizeof(label), "(无 clip · M2 纯计时)");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##clipv", label)) {
        for (const auto& e : db.Entries()) {
            if (e.type != AssetType::Clip) continue;
            char item[160];
            std::snprintf(item, sizeof(item), "%s%s", (uint32_t)e.guid == id ? "√ " : "",
                          e.relPath.c_str());
            if (ImGui::Selectable(item, (uint32_t)e.guid == id)) {
                id = (uint32_t)e.guid;
                ctx.dirty = true;
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pay = ImGui::AcceptDragDropPayload("LemonAsset")) {
            AssetDragPayload d{};
            std::memcpy(&d, pay->Data, sizeof(d));
            if (d.kind == 4) { // clip（AssetBrowser KindOf）
                id = (uint32_t)d.guid;
                ctx.dirty = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem("clip_ctx")) {
        if (ImGui::MenuItem("在动画工作台打开") && entry)
            app.OpenAnimationEditor(entry->guid); // T3c：归属集解析在 EditorApp 汇聚
        if (ImGui::MenuItem("清空引用")) {
            id = 0;
            ctx.dirty = true;
        }
        ImGui::EndPopup();
    }
    return false;
}

/// GUID 资产槽（FieldHint::AnimSetRef/ControllerRef/RmlRef + UInt64；T3d 批①、
/// 批③d 前置）。值 = 资产 GUID 全量（u64，非 clipId 的低 32 位截断——绑定是配置面）。
/// 下拉全列同类型 / AssetBrowser 拖入（kind 6=集 / 7=controller / 8=rml）/ 右键清空。
/// 写入就地完成（combo 尾置 → 属性轨由 Deactivated 捕获，DrawClipSlot 同款）。
bool DrawGuidSlot(EditorApp& app, uint8_t* p, AssetType type, uint8_t dragKind,
                  const char* noneLabel) {
    EditorContext& ctx = app.Ctx();
    AssetDatabase& db = ctx.Assets();
    uint64_t& guid = *(uint64_t*)p;
    const AssetEntry* entry = guid != 0 ? db.FindByGuid(guid) : nullptr;

    char label[96];
    if (entry)
        std::snprintf(label, sizeof(label), "%s%s", entry->missing ? "⚠ " : "",
                      entry->relPath.c_str());
    else if (guid != 0)
        std::snprintf(label, sizeof(label), "悬空 %016llx", (unsigned long long)guid);
    else
        std::snprintf(label, sizeof(label), "%s", noneLabel);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##guidv", label)) {
        for (const auto& e : db.Entries()) {
            if (e.type != type) continue;
            char item[160];
            std::snprintf(item, sizeof(item), "%s%s", e.guid == guid ? "√ " : "",
                          e.relPath.c_str());
            if (ImGui::Selectable(item, e.guid == guid)) {
                guid = e.guid;
                ctx.dirty = true;
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pay = ImGui::AcceptDragDropPayload("LemonAsset")) {
            AssetDragPayload d{};
            std::memcpy(&d, pay->Data, sizeof(d));
            if (d.kind == dragKind && d.guid != 0) {
                guid = d.guid;
                ctx.dirty = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem("guid_ctx")) {
        if (ImGui::MenuItem("清空引用")) {
            guid = 0;
            ctx.dirty = true;
        }
        ImGui::EndPopup();
    }
    return false;
}

/// 字段字节尺寸（FieldMeta 无 size 位，按 FieldType 定长）
size_t FieldSizeOf(ecs::FieldType t) {
    using ecs::FieldType;
    switch (t) {
        case FieldType::Float:
        case FieldType::Int32:
        case FieldType::UInt32:
        case FieldType::TeamRef: return 4;
        case FieldType::Double:
        case FieldType::UInt64:
        case FieldType::EntityRef:
        case FieldType::Vec2: return 8;
        case FieldType::Int16:
        case FieldType::UInt16: return 2;
        case FieldType::Int8:
        case FieldType::UInt8:
        case FieldType::Bool: return 1;
        case FieldType::Blob24: return 24;
    }
    return 0;
}

// 字段级重置默认值栈缓冲上限（超限组件不提供该按钮；Transform2D = 20B）
constexpr uint32_t kResetBuf = 128;

/// 名称列尾（紧贴值列输入框左缘）的重置图标按钮：ImDrawList 自绘 ↺（字体范围
/// 是中文常用集，箭头区 U+2190-21FF 未编入，用字形会显示 '?'）。悬停浅底衬 +
/// 主题色高亮。不占输入框宽度（图标落在定宽名称列的空隙里）。
bool ResetIconButton(float size) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##reset", ImVec2(size, size));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool hovered = ImGui::IsItemHovered();
    if (hovered)
        dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size),
                          ImGui::GetColorU32(ImGuiCol_ButtonHovered, 0.30f), 3.0f);
    const ImU32 col = hovered ? ImGui::ColorConvertFloat4ToU32(theme::kAccent)
                              : ImGui::GetColorU32(ImGuiCol_Text);
    const ImVec2 c{pos.x + size * 0.5f, pos.y + size * 0.52f};
    const float r = size * 0.32f;
    constexpr float kPi = 3.14159265f;
    const float gap = 0.62f; // 弧口半角（开口朝右）
    dl->PathArcTo(c, r, gap, 2.0f * kPi - gap, 24);
    dl->PathStroke(col, 0, 1.6f);
    // 箭头在弧起点（右下），切向逆时针（屏幕 y 向下 → 视觉朝上偏右）
    const float a0 = gap;
    const ImVec2 tip{c.x + r * std::cos(a0), c.y + r * std::sin(a0)};
    const ImVec2 dir{std::sin(a0), -std::cos(a0)};
    const ImVec2 perp{-dir.y, dir.x};
    dl->AddTriangleFilled(ImVec2{tip.x + dir.x * 2.2f, tip.y + dir.y * 2.2f},
                          ImVec2{tip.x - dir.x * 2.0f + perp.x * 3.2f,
                                 tip.y - dir.y * 2.0f + perp.y * 3.2f},
                          ImVec2{tip.x - dir.x * 2.0f - perp.x * 3.2f,
                                 tip.y - dir.y * 2.0f - perp.y * 3.2f}, col);
    return pressed;
}

/// 单字段控件（名字列已由调用方进入）。返回 Unchanged/Edited/ResetToDefault
///（ResetToDefault = 重置按钮已直推属性轨 Undo，调用方须失效空闲快照防双入栈）。
/// ID 纪律：所有控件标签恒 "##v"（不显示名字），唯一性靠 PushID(字段名)——
/// 同组件内字段名唯一；组件级再由 DrawComponent PushID(组件名) 兜底跨组件同名字段。
/// （M4.5 修复：曾经无作用域，SpriteRenderer 4 个输入同 ID 撞车 → ImGui 调试检查
/// 标冲突后整组控件失去交互。）
enum class FieldResult { Unchanged, Edited, ResetToDefault };
FieldResult DrawField(EditorApp& app, const ecs::ComponentMeta& meta, ecs::Entity e,
                      uint64_t guid, const FieldMeta& f, const FieldEditorMeta& ed, void* comp) {
    EditorContext& ctx = app.Ctx();
    uint8_t* p = (uint8_t*)comp + f.offset;
    const char* compName = meta.name;
    // PushID 覆盖整字段（label + 控件共用字段名种子；控件 "##v" 的最终 ID 与
    // 旧版逐字一致 = hash(种子栈+f.name, "##v")）
    ImGui::PushID(f.name);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(f.name);
    if (ed.tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ed.tooltip);
    // label-scrub（M4.7d）：数值字段的名字可拖（hover 下划线 + 双向箭头光标）；
    // dx 由下方数值分支消费，Shift = 浮点 ×0.1 微调。开始/结束沿经
    // g_scrubActive/g_scrubEnded 汇入 DrawComponent 属性轨（拖拽天然合并）。
    const bool scrubbable =
        !ecs::HasHint(ed.hints, FieldHint::Hide) &&
        !ecs::HasHint(ed.hints, FieldHint::AssetRef) &&
        !ecs::HasHint(ed.hints, FieldHint::ClipRef) &&
        !ecs::HasHint(ed.hints, FieldHint::AnimSetRef) &&
        !ecs::HasHint(ed.hints, FieldHint::ControllerRef) &&
        !ecs::HasHint(ed.hints, FieldHint::RmlRef) &&
        !ecs::HasHint(ed.hints, FieldHint::Enum) &&
        !ecs::HasHint(ed.hints, FieldHint::ColorHex) &&
        (f.type == FieldType::Float || f.type == FieldType::Double ||
         f.type == FieldType::Int32 || f.type == FieldType::UInt32 ||
         f.type == FieldType::UInt64 || f.type == FieldType::Int16 ||
         f.type == FieldType::UInt16 ||
         ((f.type == FieldType::Int8 || f.type == FieldType::UInt8) &&
          !ecs::HasHint(ed.hints, FieldHint::Bool8)));
    float scrubDx = 0.0f;
    if (scrubbable) {
        // --smoke-ui 定位：登记 label 矩形（键 "组件.字段"，如 Transform2D.rot）
        testhooks::Stash((std::string(compName) + "." + f.name).c_str(),
                         ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        const ImGuiID sid = ImGui::GetID("##scrub");
        const bool active = scrub_.id == sid;
        const bool hover = ImGui::IsItemHovered();
        scrubDx = LabelScrubDelta(sid);
        if (active || hover) { // 可拖提示：名字下加主题色下划线
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(a.x, b.y - 1.0f), ImVec2(b.x, b.y),
                ImGui::ColorConvertFloat4ToU32(theme::kAccent));
        }
    }
    // 字段级重置（FieldHint::Reset，M4.8）：图标右对齐名称列尾 = 紧贴值列输入框
    // 左缘（Unity/Godot 图标位），值列保持满宽。默认值口径 = constructFn 默认
    // 构造后按 offset 拷字段字节（与组件级重置同源）。Undo 直推属性轨；返回
    // ResetToDefault 由 DrawComponent 失效空闲快照，防交互帧双入栈。
    bool resetDone = false;
    if (ecs::HasHint(ed.hints, ecs::FieldHint::Reset) && meta.constructFn &&
        meta.sizeOf <= kResetBuf && !ecs::HasHint(ed.hints, ecs::FieldHint::Hide) &&
        !ecs::HasHint(ed.hints, ecs::FieldHint::AssetRef) &&
        !ecs::HasHint(ed.hints, ecs::FieldHint::ClipRef) &&
        !ecs::HasHint(ed.hints, ecs::FieldHint::AnimSetRef) &&
        !ecs::HasHint(ed.hints, ecs::FieldHint::ControllerRef) &&
        !ecs::HasHint(ed.hints, ecs::FieldHint::RmlRef)) {
        const float iconSz = ImGui::GetFrameHeight() - 4.0f;
        ImGui::SameLine();
        // slack 必须在 SameLine 之后取：文本绘制后光标已换行到列首，之前取到的
        // 是整列宽 → 右对齐越出列右缘被单元格裁剪 = 图标不可见（实抓）
        const float slack = ImGui::GetContentRegionAvail().x; // 名称列剩余（自文本尾起）
        if (slack > iconSz + 6.0f) {                          // 名字占满列时不画（防叠字）
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + slack - iconSz);
            if (ResetIconButton(iconSz)) {
                alignas(16) uint8_t def[kResetBuf];
                meta.constructFn(def);
                const std::vector<uint8_t> before = ctx.SnapshotComponent(e, meta.id);
                std::memcpy(p, def + f.offset, FieldSizeOf(f.type));
                ctx.dirty = true;
                if (!ctx.Playing() && guid)
                    ctx.PushPropertyUndo(
                        ("重置 " + std::string(meta.name) + "." + f.name).c_str(), guid,
                        meta.id, before, ctx.SnapshotComponent(e, meta.id));
                resetDone = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s.%s 恢复默认值", meta.name, f.name);
        }
    }
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);

    bool changed = false;

    if (ecs::HasHint(ed.hints, FieldHint::Hide)) {
        ImGui::TextDisabled("(internal)");
    } else if (ecs::HasHint(ed.hints, FieldHint::AssetRef) && f.type == FieldType::UInt32) {
        // AssetRef 槽当前仅 SpriteRenderer.spriteId（ED_ASSET 全目录唯一）→ 可取整组件
        // 写入就地完成（combo 尾置 → 属性轨由 Deactivated 捕获）
        DrawSpriteSlot(app, p, *static_cast<ecs::SpriteRenderer*>(comp));
    } else if (ecs::HasHint(ed.hints, FieldHint::ClipRef) && f.type == FieldType::UInt32) {
        DrawClipSlot(app, p); // M5 批③：Animator2D.clipId（写入就地完成）
    } else if (ecs::HasHint(ed.hints, FieldHint::AnimSetRef) && f.type == FieldType::UInt64) {
        DrawGuidSlot(app, p, AssetType::AnimSet, 6, "(无集 · 按名回退当前段所属集)");
    } else if (ecs::HasHint(ed.hints, FieldHint::ControllerRef) && f.type == FieldType::UInt64) {
        DrawGuidSlot(app, p, AssetType::Controller, 7, "(无状态机 · 仅集绑定)");
    } else if (ecs::HasHint(ed.hints, FieldHint::RmlRef) && f.type == FieldType::UInt64) {
        // 批③d 前置：UIDocument.sourceAssetGuid（0 = 未挂，进 Play 不装载该屏）
        DrawGuidSlot(app, p, AssetType::Rml, 8, "(未挂 .rml · 进 Play 不装载)");
    } else if (ecs::HasHint(ed.hints, FieldHint::Enum)) {
        changed = DrawEnumControl(f, ed, p);
    } else if (ecs::HasHint(ed.hints, FieldHint::ColorHex) && f.type == FieldType::UInt32) {
        uint32_t c = *(uint32_t*)p;
        float col[4] = {((c >> 0) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f,
                        ((c >> 16) & 0xFF) / 255.0f, ((c >> 24) & 0xFF) / 255.0f};
        if (ImGui::ColorEdit4("##v", col,
                              ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
            *(uint32_t*)p = (uint32_t)(col[0] * 255) | ((uint32_t)(col[1] * 255) << 8) |
                            ((uint32_t)(col[2] * 255) << 16) | ((uint32_t)(col[3] * 255) << 24);
            changed = true;
        }
    } else {
        switch (f.type) {
            case FieldType::Float: {
                if (scrubDx != 0.0f) { // label-scrub（度字段走 0.5°/px 档与 DragFloat 对齐）
                    const float k = ImGui::GetIO().KeyShift ? 0.1f : 1.0f;
                    float sv = *(float*)p +
                               scrubDx * k *
                                   (ecs::HasHint(ed.hints, FieldHint::Degree)
                                        ? 0.5f / 57.29577951f : 1.0f);
                    if (ecs::HasHint(ed.hints, FieldHint::Range))
                        sv = Clampf(sv, ed.rangeMin, ed.rangeMax);
                    *(float*)p = sv;
                    changed = true;
                }
                float v = *(float*)p;
                if (ecs::HasHint(ed.hints, FieldHint::Degree)) {
                    float deg = v * 57.29577951f;
                    if (ImGui::DragFloat("##v", &deg, 0.5f)) {
                        *(float*)p = deg / 57.29577951f;
                        changed = true;
                    }
                    // 属性轨捕获点：后面 TextDisabled 后缀会把"最后项"换成 Text
                    //（恒不 active/deactivated）→ 拖拽态须就地汇入 scrub 通道，
                    // 否则度数拖拽全程属性轨失明（快照持续刷新、松手不入 Undo）
                    g_scrubActive |= ImGui::IsItemActive();
                    g_scrubEnded |= ImGui::IsItemDeactivated();
                    ImGui::SameLine();
                    ImGui::TextDisabled("deg");
                } else if (ImGui::DragFloat("##v", &v, 1.0f)) {
                    if (ecs::HasHint(ed.hints, FieldHint::Range))
                        v = Clampf(v, ed.rangeMin, ed.rangeMax);
                    *(float*)p = v;
                    changed = true;
                }
                break;
            }
            case FieldType::Double: {
                if (scrubDx != 0.0f) {
                    *(double*)p += scrubDx * (ImGui::GetIO().KeyShift ? 0.01 : 0.1);
                    changed = true;
                }
                double v = *(double*)p;
                if (ImGui::DragScalar("##v", ImGuiDataType_Double, &v, 0.1, nullptr, nullptr,
                                      "%.3f")) {
                    *(double*)p = v;
                    changed = true;
                }
                break;
            }
            case FieldType::Int32:
                if (ScrubInt<int32_t>(scrubDx, p)) changed = true;
                changed |= ImGui::DragScalar("##v", ImGuiDataType_S32, p, 1);
                break;
            case FieldType::UInt32:
                if (ScrubInt<uint32_t>(scrubDx, p)) changed = true;
                changed |= ImGui::DragScalar("##v", ImGuiDataType_U32, p, 1, nullptr, nullptr, "%u");
                break;
            case FieldType::UInt64:
                if (ScrubInt<uint64_t>(scrubDx, p)) changed = true;
                changed |= ImGui::DragScalar("##v", ImGuiDataType_U64, p, 1, nullptr, nullptr, "%llu");
                break;
            case FieldType::Int16: {
                if (ScrubInt<int16_t>(scrubDx, p)) changed = true;
                int v = *(int16_t*)p;
                if (ImGui::DragInt("##v", &v, 1)) { *(int16_t*)p = (int16_t)v; changed = true; }
                break;
            }
            case FieldType::UInt16: {
                if (ScrubInt<uint16_t>(scrubDx, p)) changed = true;
                int v = *(uint16_t*)p;
                if (ImGui::DragInt("##v", &v, 1, 0, 65535)) { *(uint16_t*)p = (uint16_t)v; changed = true; }
                break;
            }
            case FieldType::Int8:
            case FieldType::UInt8: {
                if (ecs::HasHint(ed.hints, FieldHint::Bool8)) {
                    bool b = *(uint8_t*)p != 0;
                    if (ImGui::Checkbox("##v", &b)) { *(uint8_t*)p = b ? 1 : 0; changed = true; }
                } else {
                    if (f.type == FieldType::Int8 ? ScrubInt<int8_t>(scrubDx, p)
                                                  : ScrubInt<uint8_t>(scrubDx, p))
                        changed = true;
                    int v = *(uint8_t*)p;
                    if (ImGui::DragInt("##v", &v, 1, 0, 255)) { *(uint8_t*)p = (uint8_t)v; changed = true; }
                }
                break;
            }
            case FieldType::Bool: {
                bool b = *(bool*)p;
                if (ImGui::Checkbox("##v", &b)) { *(bool*)p = b; changed = true; }
                break;
            }
            case FieldType::Vec2: {
                float v[2] = {((lemon::Vec2*)p)->x, ((lemon::Vec2*)p)->y};
                if (ImGui::DragFloat2("##v", v, 1.0f)) {
                    ((lemon::Vec2*)p)->x = v[0];
                    ((lemon::Vec2*)p)->y = v[1];
                    changed = true;
                }
                break;
            }
            case FieldType::EntityRef: {
                uint64_t ref = *(uint64_t*)p;
                ImGui::Text("%s%llu", ref ? "" : "(null) ", (unsigned long long)ref);
                if (ref && ImGui::BeginPopupContextItem("ref_ctx")) {
                    if (ImGui::MenuItem("选中该实体")) {
                        ecs::Entity found{};
                        ctx.ActiveScene().Each([&](ecs::Entity en) {
                            if ((en.id & 0xFFFFFFFFull) == (ref & 0xFFFFFFFFull)) found = en;
                        });
                        if (!found.IsNull()) ctx.Select(found, false);
                        else LEMON_WARN("EntityRef 目标不存在（已销毁？）%llu", (unsigned long long)ref);
                    }
                    ImGui::EndPopup();
                }
                break;
            }
            case FieldType::TeamRef: {
                uint32_t t = *(uint32_t*)p;
                char preview[32];
                std::snprintf(preview, sizeof(preview), "%u %s", t, TeamName(t));
                if (ImGui::BeginCombo("##v", preview)) {
                    for (uint32_t i = 0; i < 8; ++i) {
                        char item[32]; // 恒带序号前缀：未命名档位 "?" 多行必须互异（ID 纪律）
                        std::snprintf(item, sizeof(item), "%u %s", i, TeamName(i));
                        if (ImGui::Selectable(item, i == t)) { *(uint32_t*)p = i; changed = true; }
                    }
                    ImGui::EndCombo();
                }
                break;
            }
            case FieldType::Blob24: {
                char buf[25];
                std::memcpy(buf, p, 24);
                buf[24] = 0;
                std::string s(buf);
                if (ImGui::InputText("##v", &s)) {
                    std::memset(p, 0, 24);
                    std::memcpy(p, s.c_str(), std::min<size_t>(s.size(), 23));
                    changed = true;
                }
                break;
            }
        }
    }
    ImGui::PopID();
    if (resetDone) return FieldResult::ResetToDefault;
    if (changed) {
        ctx.dirty = true;
        return FieldResult::Edited;
    }
    return FieldResult::Unchanged;
}

} // namespace

void InspectorPanel::OnGui(EditorApp& app) {
    bool winOpen = true;
    if (!ImGui::Begin(Name(), &winOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        if (!winOpen) app.ClosePanel(Name()); // × 关闭（T3b-8；Window 菜单可重开）
        return;
    }
    if (!winOpen) app.ClosePanel(Name());
    EditorContext& ctx = app.Ctx();
    // Play 横幅（M5 批④ / ADR-011）：显式告知调参改动随 Stop 丢弃——正路 =
    // 编辑态改 → Play 验证（波次表/数值全是编辑态可改的字段）
    if (ctx.Playing()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextWarn);
        ImGui::TextUnformatted("▶ Play 模式：改动随 Stop 丢弃（ADR-011）");
        ImGui::PopStyleColor();
        ImGui::Separator();
    }
    ctx.PruneSelection();
    ecs::Entity e = ctx.Primary();
    if (e.IsNull()) {
        ImGui::TextDisabled("未选中实体（在 Hierarchy 点击选择）");
        ImGui::End();
        return;
    }

    // 实体头：名字（Meta.tag 就地编辑）+ id/guid
    if (ecs::Meta* m = ctx.ActiveScene().TryGet<ecs::Meta>(e)) {
        char buf[25];
        std::memcpy(buf, m->tag, 24);
        buf[24] = 0;
        std::string s(buf);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##tag", &s)) {
            std::memset(m->tag, 0, 24);
            std::memcpy(m->tag, s.c_str(), std::min<size_t>(s.size(), 23));
            ctx.dirty = true;
        }
        ImGui::TextDisabled("id %llu  guid %016llx", (unsigned long long)e.id,
                            (unsigned long long)m->guid);

        // Prefab 头栏（M4.md §3.9 最小集：Apply/Revert/Break；逐字段
        // override 高亮 = 砍单候补 #1，M5）
        if (m->prefabId) {
            const AssetEntry* pf = ctx.Assets().FindByGuid(m->prefabId);
            ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextWarn);
            ImGui::Text("Prefab 实例：%s", pf ? pf->FileName().c_str() : "⚠ 源资产缺失");
            ImGui::PopStyleColor();
            if (ImGui::Button("Apply")) { // 实例写回源（含子树）
                ctx.ApplyPrefabInstance(e);
                ctx.dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Revert")) { // 整体回到源资产态（destroy + 重建）
                const std::string before = ctx.SnapshotSceneJson();
                const bool ok = ctx.RevertPrefabInstance(e);
                if (!ok) LEMON_WARN("Revert 失败（源资产缺失？）");
                else if (!ctx.Playing()) ctx.PushStructuralUndo("Prefab Revert", before);
                // 实体已重建，旧句柄失效 → 本帧不再绘制其余控件
                ImGui::Separator();
                ImGui::End();
                return;
            }
            ImGui::SameLine();
            if (ImGui::Button("Break")) { // 断链成普通实体
                const std::string before = ctx.SnapshotSceneJson();
                ctx.BreakPrefabInstance(e);
                if (!ctx.Playing()) ctx.PushStructuralUndo("Prefab Break", before);
            }
            ImGui::Separator();
        }
    }
    ImGui::Separator();

    auto& reg = ecs::ComponentRegistry::Instance();
    for (uint16_t id = 0; id < reg.Count(); ++id) {
        const ComponentMeta& meta = reg.At(id);
        if (!meta.hasFn || !meta.hasFn(ctx.ActiveScene(), e)) continue;
        DrawComponent(app, meta, e);
    }

    // ---- ScriptBox 段（M4.4 装配通路 #7：不入注册表 → 手绘；M6a 批⓪ 列表化，
    // 每槽一 combo + 移除；同类型唯一——已挂类型菜单置灰）----
    scripting::ScriptBox* sb = ctx.ActiveScene().TryGet<scripting::ScriptBox>(e);
    if (sb) {
        if (ImGui::CollapsingHeader("Script", ImGuiTreeNodeFlags_DefaultOpen)) {
            const auto& names = ctx.ScriptTypeNames();
            for (uint32_t i = 0; i < sb->count; ++i) {
                scripting::ScriptSlot& s = sb->slots[i];
                ImGui::PushID((int)i);
                char cur[40];
                std::snprintf(cur, sizeof(cur), "%s%s", s.className[0] ? "" : "(未选) ",
                              s.className);
                ImGui::SetNextItemWidth(-52);
                if (ImGui::BeginCombo("##script", cur)) {
                    for (const std::string& n : names) {
                        const bool taken = scripting::FindSlot(*sb, n.c_str()) >= 0 &&
                                           n != s.className;
                        char item[72];
                        std::snprintf(item, sizeof(item), "%s%s", taken ? "已挂 " : "",
                                      n.c_str());
                        if (!ImGui::Selectable(item, n == s.className,
                                               taken ? ImGuiSelectableFlags_Disabled : 0))
                            continue;
                        // 关联同名 .cs 资产（stem == 类名；找不到 = guid 0 仅类名装配）
                        uint64_t guid = 0;
                        for (const auto& a : ctx.Assets().Entries()) {
                            if (a.type != AssetType::Script || a.missing) continue;
                            std::string stem = a.FileName();
                            if (size_t dot = stem.find_last_of('.'); dot != std::string::npos)
                                stem.resize(dot);
                            if (stem == n) {
                                guid = a.guid;
                                break;
                            }
                        }
                        ctx.SetSlotScript(e, i, guid, n.c_str());
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("脚本类型（C# Behaviours 注册表；热重载 M4.5）\n"
                                      "资产 guid %s\ntypeId %d  %s",
                                      s.scriptGuid
                                          ? AssetDatabase::GuidToHex(s.scriptGuid).c_str()
                                          : "(无 .cs 资产关联)",
                                      s.typeId,
                                      s.typeId >= 0 ? "已解析" : "未解析（Play 时按名装配）");
                ImGui::SameLine();
                if (ImGui::SmallButton("×")) {
                    const std::string before = ctx.SnapshotSceneJson();
                    ctx.RemoveScriptSlot(e, i);
                    if (!ctx.Playing()) ctx.PushStructuralUndo("移除脚本", before);
                }
                ImGui::PopID();
            }
        }
    }

    ImGui::Spacing();
    const bool canAdd = !ctx.ActiveScene().Has<scripting::ScriptBox>(e) ||
                        ctx.ActiveScene().Get<scripting::ScriptBox>(e).count <
                            scripting::kMaxScriptsPerEntity;
    if (!canAdd) ImGui::BeginDisabled();
    if (ImGui::Button("Add Script")) ImGui::OpenPopup("add_script");
    if (!canAdd) ImGui::EndDisabled();
    if (canAdd && ImGui::IsItemHovered())
        ImGui::SetTooltip("挂 LemonBehaviour（每实体 ≤ %u，同类型唯一）",
                          scripting::kMaxScriptsPerEntity);
    if (ImGui::BeginPopup("add_script")) {
        const auto& names = ctx.ScriptTypeNames();
        if (names.empty()) ImGui::TextDisabled("（无脚本宿主：--script <dll> 或项目 Game/）");
        for (const std::string& n : names) {
            // 同类型唯一：已挂类型置灰（入口闸的 UI 面；AttachScript 内再兜底）
            const scripting::ScriptBox* box =
                ctx.ActiveScene().TryGet<scripting::ScriptBox>(e);
            const bool taken = box && scripting::FindSlot(*box, n.c_str()) >= 0;
            char item[72];
            std::snprintf(item, sizeof(item), "%s%s", taken ? "已挂 " : "", n.c_str());
            if (!ImGui::MenuItem(item, nullptr, false, !taken)) continue;
            // 关联同名 .cs 资产（文件 stem == 类名；找不到 = guid 0，仅类名装配）
            uint64_t guid = 0;
            for (const auto& a : ctx.Assets().Entries()) {
                if (a.type != AssetType::Script || a.missing) continue;
                std::string stem = a.FileName();
                if (size_t dot = stem.find_last_of('.'); dot != std::string::npos)
                    stem.resize(dot);
                if (stem == n) {
                    guid = a.guid;
                    break;
                }
            }
            const std::string before = ctx.SnapshotSceneJson();
            ctx.AttachScript(e, guid, n.c_str());
            if (!ctx.Playing()) ctx.PushStructuralUndo("挂脚本", before);
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();
    if (ImGui::Button("Add Component", ImVec2(-1, 0))) ImGui::OpenPopup("add_comp");
    if (ImGui::BeginPopup("add_comp")) {
        for (uint16_t id = 0; id < reg.Count(); ++id) {
            const ComponentMeta& meta = reg.At(id);
            if (!meta.emplaceFn || !meta.hasFn) continue;
            if (ProtectedComponent(meta.name)) continue;
            if (meta.hasFn(ctx.ActiveScene(), e)) continue;
            if (ImGui::MenuItem(meta.name)) {
                const std::string before = ctx.SnapshotSceneJson();
                meta.emplaceFn(ctx.ActiveScene(), e);
                ctx.dirty = true; // 新登记组件零编辑器代码即出现在此（验收点）
                if (!ctx.Playing())
                    ctx.PushStructuralUndo("添加组件", before);
            }
        }
        ImGui::EndPopup();
    }
    ImGui::End();
}

void InspectorPanel::DrawComponent(EditorApp& app, const ComponentMeta& meta, ecs::Entity e) {
    EditorContext& ctx = app.Ctx();
    void* comp = meta.getFn ? meta.getFn(ctx.ActiveScene(), e) : nullptr;
    if (!comp && meta.readFn) comp = const_cast<void*>(meta.readFn(ctx.ActiveScene(), e));
    if (!comp) return; // 空 tag 组件无数据体

    // 属性轨（§3.5）：空闲帧缓存组件字节快照；任一控件结束交互（拖拽天然合并）
    // → 提交 before/after。guid 定位 Undo 找回。
    bool anyActive = false, anyDeactivated = false;
    const uint64_t guid = [&] {
        const ecs::Meta* m = ctx.ActiveScene().TryGet<ecs::Meta>(e);
        return m ? m->guid : 0;
    }();

    bool open = true;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
    // 组件级 ID 作用域：字段表/控件 ID = 窗口/组件/字段——跨组件同名字段不撞
    // （Inspector ID 唯一性的外层保证；内层 = DrawField 的 PushID(f.name)）
    ImGui::PushID(meta.name);
    if (ImGui::CollapsingHeader(meta.name, &open, ImGuiTreeNodeFlags_DefaultOpen)) {
        // M4.8-a 组件级重置：右键组件头 → removeFn+emplaceFn 恢复默认值（Unity
        // Reset Component 同语义）。Meta 除外（guid = 身份，重置会断场景引用）；
        // Transform2D 可重置 = 归零（Unity 同）。弹窗 ID 在 PushID(meta.name)
        // 作用域内 = 每组件唯一（node_ctx 共享 ID 教训）
        if (ImGui::BeginPopupContextItem("comp_reset_ctx")) {
            const bool canReset = std::strcmp(meta.name, "Meta") != 0 &&
                                  meta.removeFn && meta.emplaceFn && !ctx.Playing();
            if (ImGui::MenuItem("重置组件（恢复默认值）", nullptr, false, canReset)) {
                const std::vector<uint8_t> before = ctx.SnapshotComponent(e, meta.id);
                meta.removeFn(ctx.ActiveScene(), e);
                meta.emplaceFn(ctx.ActiveScene(), e);
                ctx.dirty = true;
                if (guid && meta.sizeOf > 0)
                    ctx.PushPropertyUndo("重置组件", guid, meta.id, before,
                                         ctx.SnapshotComponent(e, meta.id));
                idleSnaps_.erase(guid ^ ((uint64_t)meta.id << 48)); // 空闲快照失效
                LEMON_LOG("重置组件 %s（实体 %llu）", meta.name,
                          (unsigned long long)e.id);
            }
            ImGui::EndPopup();
            ImGui::PopStyleVar();
            ImGui::PopID();
            return; // 池可能重排：comp 指针已失效，本帧不画字段表（下帧重取）
        }
        if (meta.fieldCount > 0 && ImGui::BeginTable("fields", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed,
                                    ImGui::CalcTextSize("sortingLayer").x + 20);
            ImGui::TableSetupColumn("value");
            for (uint16_t i = 0; i < meta.fieldCount; ++i) {
                const FieldMeta& f = meta.fields[i];
                const FieldEditorMeta& ed = EdOf(meta, i);
                if (f.flags & ecs::kFieldRuntime) { // 运行时字段：只读灰显
                    ImGui::BeginDisabled();
                    DrawField(app, meta, e, guid, f, ed, comp); // 禁用态控件 changed 恒 false，不会置 dirty
                    ImGui::EndDisabled();
                } else if (DrawField(app, meta, e, guid, f, ed, comp) ==
                           FieldResult::ResetToDefault) {
                    // 重置已直推属性轨；失效空闲快照防按钮 Deactivated 双入栈
                    idleSnaps_.erase(guid ^ ((uint64_t)meta.id << 48));
                }
                anyActive |= ImGui::IsItemActive();
                anyDeactivated |= ImGui::IsItemDeactivated();
            }
            // label-scrub 汇入属性轨（M4.7d）：进行中 = active（冻结空闲快照刷新），
            // 结束帧 = deactivated（提交 before/after，与控件拖拽同合并语义）。
            // 读毕即清——拖拽中字段所在组件本帧消化，不泄漏到后续组件。
            anyActive |= g_scrubActive;
            anyDeactivated |= g_scrubEnded;
            g_scrubActive = g_scrubEnded = false;
            ImGui::EndTable();
        }
        DrawArraySeg(app, meta, comp, anyActive, anyDeactivated);
        // M5 清障②：Spawner.prefabId / Shooter.projectileId 反查（uint32 = prefab 资产
        // GUID 低 32 位约定；EnterPlay 时同口径建映射）。拖拽绑定进 M5 编辑器批次
        // （与 tag 资产化同族，等 prefab 字段级元数据）。
        if (std::strcmp(meta.name, "Spawner") == 0 || std::strcmp(meta.name, "Shooter") == 0) {
            const bool isSpawner = meta.name[1] == 'p'; // Spawner vs Shooter
            const uint32_t pid = isSpawner ? ((const ecs::Spawner*)comp)->prefabId
                                           : ((const ecs::Shooter*)comp)->projectileId;
            const AssetEntry* pf = nullptr;
            for (const AssetEntry& a : ctx.Assets().Entries())
                if (a.type == AssetType::Prefab && !a.missing && (uint32_t)a.guid == pid) {
                    pf = &a;
                    break;
                }
            if (pf)
                ImGui::TextDisabled("prefab: %s (guid %016llx)", pf->relPath.c_str(),
                                    (unsigned long long)pf->guid);
            else
                ImGui::TextDisabled("prefab: %s（填 prefab 资产 GUID 低 32 位）",
                                    pid == 0 ? "未绑定" : "无效 id");
        }
        // 属性轨提交：交互结束帧 = before(空闲缓存) vs after(现状)；空闲帧刷新缓存。
        // 顺序纪律：提交判定必须在前——ImGui 释放帧 IsItemActive 已翻 false 而
        // IsItemDeactivated 为 true，若先判 !anyActive 会把空闲缓存刷成改后值，
        // 提交分支永远走不到（M4.2 潜伏：Inspector 控件编辑从不进 Undo，仅
        // Gizmo 直推路径可用；M4.7d 修）。
        if (!ctx.Playing() && guid && meta.sizeOf > 0) {
            const uint64_t key = guid ^ ((uint64_t)meta.id << 48);
            if (anyDeactivated) {
                auto it = idleSnaps_.find(key);
                if (it != idleSnaps_.end()) {
                    const std::vector<uint8_t> after = ctx.SnapshotComponent(e, meta.id);
                    if (after != it->second) // 空交互（点了没改值）不入栈
                        ctx.PushPropertyUndo(meta.name, guid, meta.id, it->second, after);
                    idleSnaps_.erase(it);
                }
            } else if (!anyActive) {
                if (idleSnaps_.size() > 64) idleSnaps_.clear(); // 换实体/长会话护栏
                idleSnaps_[key] = ctx.SnapshotComponent(e, meta.id);
            }
        }
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    if (!open && !ProtectedComponent(meta.name) && meta.removeFn) {
        const std::string before = ctx.SnapshotSceneJson();
        meta.removeFn(ctx.ActiveScene(), e);
        ctx.dirty = true;
        LEMON_LOG("移除组件 %s（实体 %llu）", meta.name, (unsigned long long)e.id);
        if (!ctx.Playing() && guid) ctx.PushStructuralUndo("移除组件", before);
    }
}

// M5 批②：可编辑数组段（WaveDirector 波次表的作者路径；StatusEffects/Inventory
// 调试编辑同受益）。元素级无 FieldEditorMeta（ArraySegMeta 不带编辑器元数据）——
// 控件按 FieldType 裸派发，Range/Enum/prefab 反查等精细化归 M6 波次表编辑器。
// 控件 active/deactivated 汇入 DrawComponent 属性轨（M4.7d 顺序纪律：调用方提交
// 判定在本调用之后，Undo 快照为组件级字节——seg 字节天然覆盖）。定长标量数组
// （Equipment.relicIds，elemFields=nullptr）维持只读。
void InspectorPanel::DrawArraySeg(EditorApp& app, const ComponentMeta& meta, void* comp,
                                  bool& anyActive, bool& anyDeactivated) {
    if (!meta.arraySeg) return;
    EditorContext& ctx = app.Ctx();
    const ecs::ArraySegMeta& seg = *meta.arraySeg;
    uint8_t count = seg.countOffset == 0xFFFF
                        ? (uint8_t)seg.maxCount
                        : *(const uint8_t*)((const uint8_t*)comp + seg.countOffset);
    ImGui::TextDisabled("%s[%u]", seg.field, count);
    if (!ImGui::BeginTable(seg.field, seg.elemFieldCount ? seg.elemFieldCount : 1,
                           ImGuiTableFlags_SizingStretchProp))
        return;
    for (uint8_t i = 0; i < count && i < seg.maxCount; ++i) {
        ImGui::TableNextRow();
        ImGui::PushID(i);
        uint8_t* elem = (uint8_t*)comp + seg.offset + (size_t)i * seg.elemSize;
        if (seg.elemFields) {
            for (uint16_t f = 0; f < seg.elemFieldCount; ++f) {
                ImGui::TableNextColumn();
                const FieldMeta& ef = seg.elemFields[f];
                ImGui::PushID(ef.name);
                uint8_t* p = elem + ef.offset;
                bool changed = false;
                switch (ef.type) {
                    case FieldType::Float:
                        changed = ImGui::DragFloat("##v", (float*)p, 0.05f, 0.0f, 0.0f,
                                                   "%.3f"); // Ctrl+点击可键入（ImGui 惯例）
                        break;
                    case FieldType::Int32:
                        changed = ImGui::InputScalar("##v", ImGuiDataType_S32, p);
                        break;
                    case FieldType::UInt32:
                        changed = ImGui::InputScalar("##v", ImGuiDataType_U32, p);
                        break;
                    case FieldType::UInt16:
                        changed = ImGui::InputScalar("##v", ImGuiDataType_U16, p);
                        break;
                    case FieldType::UInt8:
                        changed = ImGui::InputScalar("##v", ImGuiDataType_U8, p);
                        break;
                    case FieldType::Bool:
                        changed = ImGui::Checkbox("##v", (bool*)p);
                        break;
                    default:
                        ImGui::TextUnformatted("?"); // Vec2/Double 等暂只读（现无 seg 用到）
                        break;
                }
                if (changed) ctx.dirty = true;
                anyActive |= ImGui::IsItemActive();
                anyDeactivated |= ImGui::IsItemDeactivated();
                ImGui::PopID();
            }
        } else {
            ImGui::TableNextColumn();
            ImGui::Text("%u", *(const uint32_t*)elem); // 定长标量数组：只读
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

} // namespace lemon::editor
