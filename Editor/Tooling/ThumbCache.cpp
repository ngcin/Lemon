// Lemon 编辑器 — 通用图片缩略图缓存实现（stb 解码只进 Editor 树，同 AssetGpuCache）
#include "Tooling/ThumbCache.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

#include "stb_image.h"
#include "stb_image_resize2.h"

#include "App/ImGuiBackend.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetGpuCache.h"
#include "Core/Log.h"

namespace lemon::editor::thumbcache {
namespace {

// 160px 边（4:3 ≈ 160×120 → ~75KB RGBA）：列表缩略图足够，4K 原图全解码后即弃。
constexpr int kMaxEdge = 160;
// LRU 上限（≈12MB GPU 上界；逐出需 WaitIdle，容量取"单目录图海"够用即可）
constexpr size_t kCap = 128;
// 每帧解码预算（大目录摊平首屏卡顿；2 张 ≈ 数 ms 量级）
constexpr int kBudgetPerTick = 2;

struct Item {
    void* tex = nullptr;   // ImTextureID（描述符集）
    uint32_t rhi = 0;      // rhi::Texture.id（逐出/清场销毁用）
    int w = 0, h = 0;      // 原图尺寸（出参回显）
    bool failed = false;   // 解码/上传失败 → 永久占位（不重试风暴）
    bool borrowed = false; // 借用 AssetGpuCache 页纹理：逐出只删表项不销毁
};

struct State {
    rhi::Device* device = nullptr;
    ImGuiBackend* ui = nullptr;
    const AssetDatabase* db = nullptr;
    const AssetGpuCache* gpu = nullptr;
    std::unordered_map<std::string, Item> map;
    std::deque<std::string> lru;            // 尾 = 最近用
    std::vector<std::string> pending;       // 待解码（去重由 map/占位保证）
    std::vector<std::string> pendingSeen;   // 与 pending 同长防重
};
State& S() {
    static State s;
    return s;
}

bool HasImageExt(const std::string& p) {
    const size_t dot = p.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string e = p.substr(dot);
    for (char& c : e) c = (char)tolower((unsigned char)c);
    return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp";
}

void Destroy(Item& it) {
    if (!it.borrowed) {
        if (it.tex && S().ui) S().ui->UnregisterViewportTexture(it.tex);
        if (it.rhi && S().device) {
            S().device->WaitIdle(); // 在途帧可能采样（热重导同口径；逐出低频）
            S().device->DestroyTexture(rhi::Texture{it.rhi});
        }
    }
    it = Item{};
}

/// 解码 + 缩放 + 上传一张（同步，几 ms 量级；调用侧限频）
bool Load(const std::string& abs, Item& out) {
    int w = 0, h = 0, comp = 0;
    uint8_t* px = stbi_load(abs.c_str(), &w, &h, &comp, 4);
    if (!px || w <= 0 || h <= 0) {
        stbi_image_free(px);
        return false;
    }
    out.w = w;
    out.h = h;
    const float scale = std::min(1.0f, (float)kMaxEdge / (float)std::max(w, h));
    const int dw = std::max(1, (int)(w * scale)), dh = std::max(1, (int)(h * scale));
    std::vector<uint8_t> small((size_t)dw * dh * 4);
    // 4CHANNEL（无 alpha 语义假设）是 resize2 RGBA 最快档（头文件 §53）
    if (!stbir_resize_uint8_linear(px, w, h, w * 4, small.data(), dw, dh, dw * 4,
                                   STBIR_4CHANNEL)) {
        stbi_image_free(px);
        return false;
    }
    stbi_image_free(px);
    rhi::Texture tex = S().device->CreateTexture(
        {.width = (uint32_t)dw, .height = (uint32_t)dh, .debugName = "thumbCache"});
    if (!tex.id) return false;
    S().device->UploadTexture(tex, small.data(), (uint64_t)dw * dh * 4);
    out.rhi = tex.id;
    out.tex = S().ui->RegisterViewportTexture(tex.id); // 描述符集直采样，零 bindless 槽
    return out.tex != nullptr;
}

} // namespace

void Init(rhi::Device* device, ImGuiBackend* ui, const AssetDatabase* db,
          const AssetGpuCache* gpu) {
    Clear();
    State& s = S();
    s.device = device;
    s.ui = ui;
    s.db = db;
    s.gpu = gpu;
}

void Clear() {
    State& s = S();
    for (auto& [k, it] : s.map) Destroy(it);
    s.map.clear();
    s.lru.clear();
    s.pending.clear();
    s.pendingSeen.clear();
}

void Tick() {
    State& s = S();
    if (!s.device || s.pending.empty()) return;
    int n = 0;
    while (n < kBudgetPerTick && !s.pending.empty()) {
        const std::string abs = std::move(s.pending.front());
        s.pending.erase(s.pending.begin());
        s.pendingSeen.erase(s.pendingSeen.begin());
        auto it = s.map.find(abs);
        if (it == s.map.end()) continue; // 已被 LRU 逐出 = 不再可见，跳过
        if (!Load(abs, it->second)) it->second.failed = true;
        ++n;
    }
}

void* Get(const std::string& absPath, int* w, int* h) {
    State& s = S();
    if (!s.device || !s.ui || absPath.empty() || !HasImageExt(absPath)) return nullptr;
    auto it = s.map.find(absPath);
    if (it != s.map.end()) { // 命中：LRU 提尾（失败项也走占位，不重排无妨）
        if (auto l = std::find(s.lru.begin(), s.lru.end(), absPath); l != s.lru.end()) {
            s.lru.erase(l);
            s.lru.push_back(absPath);
        }
        if (w) *w = it->second.w;
        if (h) *h = it->second.h;
        return it->second.failed ? nullptr : it->second.tex;
    }
    // 新请求：先入表占位（去重）+ 排队，本帧返回 nullptr（占位框）
    Item& item = s.map.emplace(absPath, Item{}).first->second;
    s.lru.push_back(absPath);
    if (std::find(s.pendingSeen.begin(), s.pendingSeen.end(), absPath) ==
        s.pendingSeen.end()) {
        s.pendingSeen.push_back(absPath);
        s.pending.push_back(absPath);
    }
    // 项目内已导入精灵：直接复用 AssetGpuCache 的 thumb（零解码零上传）
    const std::string& root = s.db->ProjectRoot();
    if (!root.empty() && absPath.rfind(root + "/", 0) == 0 && s.gpu) {
        const std::string rel = absPath.substr(root.size() + 1);
        if (const AssetEntry* e = s.db->FindByPath(rel);
            e && !e->missing && e->type == AssetType::Sprite) {
            if (void* t = s.gpu->Thumbnail(e->guid)) {
                item.tex = t; // 借用页内纹理：逐出只删表项（borrowed 标记）
                item.borrowed = true;
                uint32_t pw = 0, ph = 0;
                if (s.gpu->PageInfo(e->guid, pw, ph)) {
                    item.w = (int)pw;
                    item.h = (int)ph;
                }
                if (w) *w = item.w;
                if (h) *h = item.h;
                return item.tex;
            }
        }
    }
    while (s.lru.size() > kCap) { // LRU 逐出（队首 = 最旧）
        const std::string old = s.lru.front();
        s.lru.pop_front();
        if (auto oit = s.map.find(old); oit != s.map.end()) {
            Destroy(oit->second); // 借用项 rhi=0：只删表项不碰页内纹理
            s.map.erase(oit);
        }
    }
    return nullptr;
}

} // namespace lemon::editor::thumbcache
