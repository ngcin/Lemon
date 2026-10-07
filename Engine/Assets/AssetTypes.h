// Lemon 引擎 — 资产类型域单源（M7a 批②；06 §2）
// 编辑器（AssetDatabase 写侧）与运行时（AssetIndex 只读侧）共需的类型判定与
// GUID hex 转换——此前住在 lemon-editor-core 的 AssetDatabase，引擎侧被迫自持
// mini 拷贝（ScriptHost 桥内 hex 解析实证）。本头 = 两侧唯一事实源：
//   * 枚举值序冻结（.meta "type" 字符串与 manifest 序列化按 AssetTypeName 走，
//     插位自由、值序不可变）；
//   * 纯逻辑零依赖（可单测；引擎内核分层最底）。
#pragma once

#include <cstdint>
#include <string>

namespace lemon::assets {

// Table = .tab 配置表资产（M6a 批②，ADR-012）
// AnimSet = .override 动画集容器（M6a 批② T3c：段名 → .anim 引用清单）
// Controller = .controller 动画状态机（M6a 批② T3d，ADR-013）
// Rml/Rcss = 游戏 UI 文档/样式表（M6b 批③b，ADR-014）
// Audio = 音频源（M6c，ADR-015：.wav/.ogg/.mp3/.flac → 烤制 LBA1）
// Font = 字体源（M7c 批①：.ttf/.otf → 导入期烘焙位图图集页，ADR-015 烤制先例
//        同款；运行时零 FreeType 零栅格化——02 §7 红线不破）
enum class AssetType
    : uint8_t { Sprite, Prefab, Script, Clip, Table, AnimSet, Controller, Rml, Rcss, Audio,
                Font, Generic };

const char* AssetTypeName(AssetType t);

/// 扩展名 → 类型（TypeOf = AssetDatabase 既有判定搬移；06 §1 资产源布局共识）
AssetType TypeOf(const std::string& relPath);

/// 16 位 hex（Inspector 槽显示 / C# Assets.SpriteOf 参数形态）
std::string GuidToHex(uint64_t guid);
/// 恰 16 位 hex 才合法（短串按截断值放行会静默错绑资产——#88）；非 hex = 0
uint64_t HexToGuid(const char* hex);
inline uint64_t HexToGuid(const std::string& hex) { return HexToGuid(hex.c_str()); }

} // namespace lemon::assets
