// Lemon 编辑器 — Inspector 面板（M4-Editor-Plan §2.2；内核 #6 消费端）
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
#include "Components/CoreComponents.h"
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
    if (ImGui::BeginCombo("##v", label)) {
        for (uint32_t i = 0; i < ed.enumCount; ++i)
            if (ImGui::Selectable(ed.enumNames[i], (int64_t)i == cur)) {
                write(i);
                return true;
            }
        ImGui::EndCombo();
    }
    return false;
}

/// sprite 资产槽（FieldHint::AssetRef + UInt32 spriteId；M4.4 接通）。
/// 值 = AtlasRegistry spriteId；UI 反查资产（guid 名/缩略图）。拖 AssetBrowser
/// sprite 进来 = 设引用；下拉全列；右键清空。最后绘制的控件是 combo →
/// DrawComponent 的 IsItemDeactivated 属性轨照常捕获。
bool DrawSpriteSlot(EditorApp& app, uint8_t* p) {
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
                ctx.dirty = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem("slot_ctx")) {
        if (ImGui::MenuItem("清空引用")) {
            id = 0;
            ctx.dirty = true;
        }
        ImGui::EndPopup();
    }
    return false; // 写入已就地完成（combo 尾置 → 属性轨由 Deactivated 捕获）
}

/// 单字段控件（名字列已由调用方进入）。返回是否写入。
/// ID 纪律：所有控件标签恒 "##v"（不显示名字），唯一性靠 PushID(字段名)——
/// 同组件内字段名唯一；组件级再由 DrawComponent PushID(组件名) 兜底跨组件同名字段。
/// （M4.5 修复：曾经无作用域，SpriteRenderer 4 个输入同 ID 撞车 → ImGui 调试检查
/// 标冲突后整组控件失去交互。）
bool DrawField(EditorApp& app, const FieldMeta& f, const FieldEditorMeta& ed, void* comp,
               const char* compName) {
    EditorContext& ctx = app.Ctx();
    uint8_t* p = (uint8_t*)comp + f.offset;
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
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);

    bool changed = false;

    if (ecs::HasHint(ed.hints, FieldHint::Hide)) {
        ImGui::TextDisabled("(internal)");
    } else if (ecs::HasHint(ed.hints, FieldHint::AssetRef) && f.type == FieldType::UInt32) {
        DrawSpriteSlot(app, p); // 写入就地完成（combo 尾置 → 属性轨由 Deactivated 捕获）
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
    if (changed) ctx.dirty = true;
    return changed;
}

} // namespace

void InspectorPanel::OnGui(EditorApp& app) {
    if (!ImGui::Begin(Name(), nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    EditorContext& ctx = app.Ctx();
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

        // Prefab 头栏（M4-Editor-Plan §3.9 最小集：Apply/Revert/Break；逐字段
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

    // ---- ScriptBox 段（M4.4 装配通路 #7：不入注册表 → 这里手绘）----
    if (scripting::ScriptBox* sb = ctx.ActiveScene().TryGet<scripting::ScriptBox>(e)) {
        if (ImGui::CollapsingHeader("Script", ImGuiTreeNodeFlags_DefaultOpen)) {
            const auto& names = ctx.ScriptTypeNames();
            char cur[40];
            std::snprintf(cur, sizeof(cur), "%s%s", sb->className[0] ? "" : "(未选) ",
                          sb->className);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##script", cur)) {
                for (const std::string& n : names)
                    if (ImGui::Selectable(n.c_str(), n == sb->className))
                        ctx.AttachScript(e, sb->scriptGuid, n.c_str());
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("脚本类型（C# Behaviours 注册表；热重载 M4.5）\n"
                                  "资产 guid %s",
                                  sb->scriptGuid
                                      ? AssetDatabase::GuidToHex(sb->scriptGuid).c_str()
                                      : "(无 .cs 资产关联)");
            ImGui::TextDisabled("typeId %d  %s", sb->typeId,
                                sb->typeId >= 0 ? "已解析" : "未解析（Play 时按名装配）");
            if (ImGui::Button("移除脚本")) {
                const std::string before = ctx.SnapshotSceneJson();
                ctx.ActiveScene().Remove<scripting::ScriptBox>(e);
                ctx.dirty = true;
                if (!ctx.Playing()) ctx.PushStructuralUndo("移除脚本", before);
            }
        }
    }

    ImGui::Spacing();
    if (!ctx.ActiveScene().Has<scripting::ScriptBox>(e) && ImGui::Button("Add Script"))
        ImGui::OpenPopup("add_script");
    if (ImGui::BeginPopup("add_script")) {
        const auto& names = ctx.ScriptTypeNames();
        if (names.empty()) ImGui::TextDisabled("（无脚本宿主：--script <dll> 或项目 Game/）");
        for (const std::string& n : names) {
            if (!ImGui::MenuItem(n.c_str())) continue;
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
        if (meta.fieldCount > 0 && ImGui::BeginTable("fields", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed,
                                    ImGui::CalcTextSize("sortingLayer").x + 20);
            ImGui::TableSetupColumn("value");
            for (uint16_t i = 0; i < meta.fieldCount; ++i) {
                const FieldMeta& f = meta.fields[i];
                const FieldEditorMeta& ed = EdOf(meta, i);
                if (f.flags & ecs::kFieldRuntime) { // 运行时字段：只读灰显
                    ImGui::BeginDisabled();
                    DrawField(app, f, ed, comp, meta.name); // 禁用态控件 changed 恒 false，不会置 dirty
                    ImGui::EndDisabled();
                } else {
                    DrawField(app, f, ed, comp, meta.name);
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
        DrawArraySeg(app, meta, comp);
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

void InspectorPanel::DrawArraySeg(EditorApp& app, const ComponentMeta& meta, const void* comp) {
    if (!meta.arraySeg) return;
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
        const uint8_t* elem = (const uint8_t*)comp + seg.offset + (size_t)i * seg.elemSize;
        if (seg.elemFields) {
            for (uint16_t f = 0; f < seg.elemFieldCount; ++f) {
                ImGui::TableNextColumn();
                const FieldMeta& ef = seg.elemFields[f];
                switch (ef.type) {
                    case FieldType::UInt16:
                        ImGui::Text("%u", *(const uint16_t*)(elem + ef.offset)); break;
                    case FieldType::UInt32:
                        ImGui::Text("%u", *(const uint32_t*)(elem + ef.offset)); break;
                    case FieldType::Float:
                        ImGui::Text("%.2f", *(const float*)(elem + ef.offset)); break;
                    default: ImGui::TextUnformatted("?"); break;
                }
            }
        } else {
            ImGui::TableNextColumn();
            ImGui::Text("%u", *(const uint32_t*)elem);
        }
    }
    ImGui::EndTable();
    (void)app;
}

} // namespace lemon::editor
