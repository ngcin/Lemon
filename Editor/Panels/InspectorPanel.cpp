// Lemon 编辑器 — Inspector 面板（M4-Editor-Plan §2.2；内核 #6 消费端）
// 反射驱动：ComponentRegistry 字段表 + FieldEditorMeta 特性 → 控件；新增登记组件
// 零编辑器代码出现在此（验收点）。编辑直写组件 + ctx.dirty；
// Undo 属性轨 M4.2 接入（IsItemActivated/Deactivated 拖拽合并）。
#include <cstdio>
#include <cstring>

#include "App/EditorApp.h"
#include "Components/CoreComponents.h"
#include "Core/Log.h"
#include "ECS/ComponentRegistry.h"
#include "EditorContext.h"
#include "Panels/BuiltInPanels.h"
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

/// Team 下拉名（TeamTable 无名表——M4.4 teams.json 资产化后换真名）
const char* TeamName(uint32_t t) {
    switch (t) {
        case 0: return "0 player";
        case 1: return "1 monsters";
        case 2: return "2 neutral";
        case 3: return "3 playerBullets";
        default: return "?";
    }
}

float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

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

/// 单字段控件（名字列已由调用方进入）。返回是否写入
bool DrawField(EditorContext& ctx, const FieldMeta& f, const FieldEditorMeta& ed, void* comp) {
    uint8_t* p = (uint8_t*)comp + f.offset;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(f.name);
    if (ed.tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ed.tooltip);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);

    if (ecs::HasHint(ed.hints, FieldHint::Hide)) {
        ImGui::TextDisabled("(internal)");
        return false;
    }

    // 覆盖层控件优先（枚举/颜色），命中即返回
    if (ecs::HasHint(ed.hints, FieldHint::Enum)) {
        if (DrawEnumControl(f, ed, p)) { ctx.dirty = true; return true; }
        return false;
    }
    if (ecs::HasHint(ed.hints, FieldHint::ColorHex) && f.type == FieldType::UInt32) {
        uint32_t c = *(uint32_t*)p;
        float col[4] = {((c >> 0) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f,
                        ((c >> 16) & 0xFF) / 255.0f, ((c >> 24) & 0xFF) / 255.0f};
        if (ImGui::ColorEdit4("##v", col,
                              ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
            *(uint32_t*)p = (uint32_t)(col[0] * 255) | ((uint32_t)(col[1] * 255) << 8) |
                            ((uint32_t)(col[2] * 255) << 16) | ((uint32_t)(col[3] * 255) << 24);
            ctx.dirty = true;
            return true;
        }
        return false;
    }

    bool changed = false;
    switch (f.type) {
        case FieldType::Float: {
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
                if (ecs::HasHint(ed.hints, FieldHint::Range)) v = Clampf(v, ed.rangeMin, ed.rangeMax);
                *(float*)p = v;
                changed = true;
            }
            break;
        }
        case FieldType::Double: {
            double v = *(double*)p;
            if (ImGui::DragScalar("##v", ImGuiDataType_Double, &v, 0.1, nullptr, nullptr, "%.3f")) {
                *(double*)p = v;
                changed = true;
            }
            break;
        }
        case FieldType::Int32:
            changed = ImGui::DragScalar("##v", ImGuiDataType_S32, p, 1);
            break;
        case FieldType::UInt32:
            changed = ImGui::DragScalar("##v", ImGuiDataType_U32, p, 1, nullptr, nullptr, "%u");
            break;
        case FieldType::UInt64:
            changed = ImGui::DragScalar("##v", ImGuiDataType_U64, p, 1, nullptr, nullptr, "%llu");
            break;
        case FieldType::Int16: {
            int v = *(int16_t*)p;
            if (ImGui::DragInt("##v", &v, 1)) { *(int16_t*)p = (int16_t)v; changed = true; }
            break;
        }
        case FieldType::UInt16: {
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
            if (ImGui::BeginCombo("##v", t < 8 ? TeamName(t) : "?")) {
                for (uint32_t i = 0; i < 8; ++i)
                    if (ImGui::Selectable(TeamName(i), i == t)) { *(uint32_t*)p = i; changed = true; }
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
    }
    ImGui::Separator();

    auto& reg = ecs::ComponentRegistry::Instance();
    for (uint16_t id = 0; id < reg.Count(); ++id) {
        const ComponentMeta& meta = reg.At(id);
        if (!meta.hasFn || !meta.hasFn(ctx.ActiveScene(), e)) continue;
        DrawComponent(app, meta, e);
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
                    DrawField(ctx, f, ed, comp); // 禁用态控件 changed 恒 false，不会置 dirty
                    ImGui::EndDisabled();
                } else {
                    DrawField(ctx, f, ed, comp);
                }
                anyActive |= ImGui::IsItemActive();
                anyDeactivated |= ImGui::IsItemDeactivated();
            }
            ImGui::EndTable();
        }
        DrawArraySeg(app, meta, comp);
        // 属性轨提交：空闲帧刷新缓存；交互结束帧 = before(缓存) vs after(现状)
        if (!ctx.Playing() && guid && meta.sizeOf > 0) {
            if (!anyActive) {
                idleKey_ = guid ^ ((uint64_t)meta.id << 48);
                idleSnap_ = ctx.SnapshotComponent(e, meta.id);
            } else if (anyDeactivated && !idleSnap_.empty() &&
                       idleKey_ == (guid ^ ((uint64_t)meta.id << 48))) {
                ctx.PushPropertyUndo(meta.name, guid, meta.id, idleSnap_,
                                     ctx.SnapshotComponent(e, meta.id));
            }
        }
    }
    ImGui::PopStyleVar();
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
