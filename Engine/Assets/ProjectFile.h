// Lemon 引擎 — project.lemon 只读解析（M7a 批②；ADR-016 D6/M8）
// 编辑器侧 OpenProjectPipeline 的存在性/回显逻辑原地不动；本件 = 运行时
//（lemon-game）与 packager 的最小只读面。写字段（name/guid/engineVersion/
// entryScene/schemaVersion）归 ProjectWizard/VsTemplateGen。
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace lemon::assets {

struct ProjectFile {
    bool ok = false;            // 语法合法 + name 在场（编辑器 BUG-2 同口径）
    std::string name;
    uint64_t guid = 0;
    std::string engineVersion;  // 不匹配 = 警告不阻断（ADR-016 M2）
    std::string entryScene;     // 可选（D6）：相对项目根路径；缺省 ""
    uint64_t fxFont = 0;        // 可选（M7c 批①）：Fx 飘字字体资产 guid（0 = 内置 5×7 页）
};

/// project.lemon 文本 → ProjectFile（纯函数，单测直测；坏档 ok=false 不炸）
ProjectFile ParseProjectFile(std::string_view text);

/// 读 <root>/project.lemon（文件缺失/读失败 = ok=false）
ProjectFile LoadProjectFile(const std::string& projectRoot);

/// entryScene 回退链（D6，ADR-016）：声明在场且文件存在 → 该路径；
/// 未声明 → 扫 Scenes/（含 Assets/ 内嵌场景目录）唯一 .scene → 唯一者；
/// 多场景/零场景且未声明 → 空串（红字响亮归调用方——运行时无法猜入口）。
std::string ResolveEntryScene(const std::string& projectRoot, const ProjectFile& pf);

} // namespace lemon::assets
