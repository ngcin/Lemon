// Lemon 引擎 — 资产类型域单源实现（M7a 批②；见 AssetTypes.h）
#include "Assets/AssetTypes.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

namespace lemon::assets {

const char* AssetTypeName(AssetType t) {
    switch (t) {
        case AssetType::Sprite: return "sprite";
        case AssetType::Prefab: return "prefab";
        case AssetType::Script: return "script";
        case AssetType::Clip: return "clip"; // M5 批③：06 §2.2 clip2d（.anim JSON）
        case AssetType::Table: return "table"; // M6a 批②：.tab 配置表（ADR-012）
        case AssetType::AnimSet: return "animset"; // M6a 批② T3c：.override 动画集容器
        case AssetType::Controller: return "controller"; // T3d：.controller 状态机
        case AssetType::Rml: return "rml";   // M6b 批③b：UI 文档（ADR-014 一屏一文档）
        case AssetType::Rcss: return "rcss"; // M6b 批③b：UI 样式表（<link> 引用）
        default: return "generic";
    }
}

AssetType TypeOf(const std::string& relPath) {
    std::string ext = std::filesystem::path(relPath).extension().string();
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") return AssetType::Sprite;
    if (ext == ".prefab") return AssetType::Prefab;
    if (ext == ".cs") return AssetType::Script;
    if (ext == ".anim") return AssetType::Clip; // M5 批③帧动画资产（06 §2.2）
    if (ext == ".tab") return AssetType::Table; // M6a 批②配置表资产（ADR-012）
    if (ext == ".override") return AssetType::AnimSet; // M6a 批② T3c 动画集容器
    if (ext == ".controller") return AssetType::Controller; // T3d 动画状态机
    if (ext == ".rml") return AssetType::Rml;     // M6b 批③b UI 文档（ADR-014）
    if (ext == ".rcss") return AssetType::Rcss;   // M6b 批③b UI 样式表
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac")
        return AssetType::Audio; // M6c 竖切批：音频源（ADR-015 D1 四格式）
    return AssetType::Generic;
}

std::string GuidToHex(uint64_t guid) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)guid);
    return buf;
}

uint64_t HexToGuid(const char* hex) {
    if (!hex) return 0;
    uint64_t v = 0;
    int n = 0;
    for (const char* p = hex; *p && p - hex < 16; ++p) {
        v <<= 4;
        ++n;
        char c = *p;
        if (c >= '0' && c <= '9') v |= (uint64_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (uint64_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (uint64_t)(c - 'A' + 10);
        else return 0; // 非 hex 字符
    }
    if (n != 16) return 0; // #88：短 hex 串按截断值放行会静默错绑资产——恰 16 位才合法
    return v;
}

} // namespace lemon::assets
