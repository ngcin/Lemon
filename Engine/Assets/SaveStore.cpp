// Lemon 引擎 — 游戏存档三通道落盘实现（M7a 批③ 自 EditorContext 搬家，逐行同源）
#include "Assets/SaveStore.h"

#include <filesystem>
#include <fstream>
#include <vector>

#include "Core/FileOps.h"
#include "Core/Log.h"

namespace lemon::assets {
namespace fs = std::filesystem;

// 三档三文件，坏档兜底按档隔离
static const char* SaveFileName(uint8_t ch) {
    switch (ecs::ClampSaveChannel(ch)) {
    case ecs::kSaveSettings: return "settings.sav";
    case ecs::kSaveMeta: return "meta.sav";
    default: return "slot_0.sav";
    }
}

std::string SaveStore::FilePath(const std::string& projectRoot, uint8_t ch) {
    return projectRoot.empty() ? std::string()
                               : projectRoot + "/.lemon/saves/" + SaveFileName(ch);
}

bool SaveStore::Write(const std::string& projectRoot, uint8_t ch,
                      const ecs::SaveChannel& chn) {
    const std::string path = FilePath(projectRoot, ch);
    if (path.empty() || chn.Count() == 0) return false;
    std::error_code ec;
    const fs::path p(path);
    fs::create_directories(p.parent_path(), ec);
    // 上一代转备份（首次落盘无 .bak 属正常）
    if (fs::exists(p, ec))
        fs::copy_file(p, fs::path(path + ".bak"), fs::copy_options::overwrite_existing, ec);
    const std::vector<uint8_t> bytes = chn.Encode();
    if (!lemon::WriteFileAtomic(path, bytes.data(), bytes.size())) { // F-04：统一原子写（含 flush 检查）
        LEMON_WARN("存档写入失败（临时写入/改名）：%s", path.c_str());
        return false;
    }
    return true;
}

void SaveStore::Load(const std::string& projectRoot, uint8_t ch, ecs::SaveChannel& dst) {
    // 坏档防线（2026-09-24 审查 F-03）：整档上限 16 MiB，超限不 slurp——
    // 现用途 KB 级；超大文件多为损坏/误指，Decode 侧另有条目/单值上限。
    constexpr uint64_t kMaxSaveFileBytes = 16ull << 20;
    const std::string path = FilePath(projectRoot, ch);
    if (path.empty()) return; // 无项目（bench/smoke tempdir 外的裸会话）= 空通道开局
    auto tryDecode = [&](const std::string& p) {
        std::error_code ec;
        if (!fs::exists(p, ec)) return false;
        if (const uint64_t sz = fs::file_size(p, ec); ec || sz > kMaxSaveFileBytes) {
            LEMON_WARN("存档异常（大小超 16 MiB 上限），已跳过：%s", p.c_str());
            return false;
        }
        std::ifstream f(p, std::ios::binary);
        if (!f) return false;
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
        if (!dst.Decode(bytes.data(), bytes.size())) {
            LEMON_WARN("存档损坏，已跳过：%s", p.c_str());
            return false;
        }
        return true;
    };
    if (tryDecode(path) || tryDecode(path + ".bak")) return; // 主档坏 → 备份兜底
    // 旧 game.sav 惰性迁移（M6a 批② T5，仅 slot 档）：新档不存在 → 读旧名，写恒
    // 写新名（免 rename 竞态；旧文件保留，游戏侧无感）
    if (ch == ecs::kSaveSlot) {
        const std::string legacy = projectRoot + "/.lemon/saves/game.sav";
        if (!tryDecode(legacy)) tryDecode(legacy + ".bak");
    }
}

} // namespace lemon::assets
