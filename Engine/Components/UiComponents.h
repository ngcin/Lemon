// Lemon 引擎 — 组件目录 · UI 组首件（M6b 批③d 前置，原 M6a 批③；Unity UIDocument
// 同构挂载粒度：一组件 = 一 .rml 文档资产 = 一屏，ADR-014 M1）
// 运行时状态（装载句柄/shown 态）全在 UiSubsystem（name→doc map），**严禁回写本
// 组件**（快照确定性铁律——EnterPlay 装载钩只读扫描，批文件 §5）。Play 期翻
// showOnStart/modal 只影响下次进 Play（快照不回写）；运行时显隐走 C# UI.Show/Hide
// （设计期/运行时两通道不打架的单一约定）。
#pragma once

#include <cstdint>
#include <type_traits>

namespace lemon::ecs {

struct UIDocument {
    uint64_t sourceAssetGuid = 0; // .rml 资产 GUID（Unity sourceAsset 的 GUID 形态）；0 = 未挂（合法，Inspector 提示）
    uint8_t  showOnStart = 1;     // 进 Play 即显；0 = 装载但隐藏（死亡/结算类动态屏由 C# UI.Show 点亮）
    uint8_t  modal = 0;           // M1 模态标记初值（运行时 UI.Show(doc, modal) 可覆写）
    uint16_t reserved = 0;        // 布局余量（尾加纪律）
};

// ---- 布局冻结（M3 桥侧 blittable 前提：C# 镜像 struct 与此逐字节对齐，改动=破回放）----
// u64+u8+u8+u16 裸 12B、自然对齐 sizeof=16/alignof=8（2026-09-29 审核修正：原误写
// 8B；C# 镜像 Sequential 无 Pack 逐字节同构，注册一律 sizeof——勿手写 elemSize）。
static_assert(std::is_trivially_copyable_v<UIDocument> && sizeof(UIDocument) == 16 &&
                  alignof(UIDocument) == 8,
              "UIDocument 布局冻结");

} // namespace lemon::ecs
