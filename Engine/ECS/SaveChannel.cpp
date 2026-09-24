// Lemon 引擎 — 游戏存档通道实现（M5 批④ D1；头文件见格式约定）
#include "ECS/SaveChannel.h"

#include <cstring>

namespace lemon::ecs {
namespace {

constexpr char kMagic[8] = {'L', 'E', 'M', 'O', 'N', 'S', 'A', 'V'};
constexpr uint32_t kSaveVersion = 1;

// 坏档防线（2026-09-24 审查 F-03）：Decode 消费不可信字节流，所有长度字段
// 先验上限再分配——十余字节坏档即可声明近 4 GiB 的 valLen/count，老实现
// reserve/vector 直接 bad_alloc/length_error 打死进程。
constexpr uint32_t kMaxEntries = 4096;         // 条目数上限（现用途几十条，余量充足）
constexpr uint32_t kMaxKeyLen = 255;           // 与 Set 的 key 契约一致
constexpr uint32_t kMaxValueBytes = 4u << 20;  // 单值 4 MiB（现用途 KB 级，百倍余量）

void PutU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back((uint8_t)(v & 0xFF));
    out.push_back((uint8_t)(v >> 8));
}
void PutU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((uint8_t)(v & 0xFF));
    out.push_back((uint8_t)((v >> 8) & 0xFF));
    out.push_back((uint8_t)((v >> 16) & 0xFF));
    out.push_back((uint8_t)((v >> 24) & 0xFF));
}
struct Reader {
    const uint8_t* p;
    size_t left;
    bool ok = true;
    uint16_t U16() {
        if (left < 2) { ok = false; return 0; }
        const uint16_t v = (uint16_t)(p[0] | (p[1] << 8));
        p += 2; left -= 2;
        return v;
    }
    uint32_t U32() {
        if (left < 4) { ok = false; return 0; }
        const uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        p += 4; left -= 4;
        return v;
    }
    bool Bytes(void* out, size_t n) {
        if (left < n) { ok = false; return false; }
        if (out) memcpy(out, p, n);
        p += n; left -= n;
        return true;
    }
};

} // namespace

bool SaveChannel::Set(const char* key, const void* bytes, uint32_t len) {
    if (!key || !*key || strlen(key) > 255) return false;
    std::vector<uint8_t> v(len);
    if (len && bytes) memcpy(v.data(), bytes, len);
    map_[key] = std::move(v); // 覆盖语义（同 key 整值替换）
    return true;
}

int32_t SaveChannel::GetLen(const char* key) const {
    if (!key) return -1;
    const auto it = map_.find(key);
    return it == map_.end() ? -1 : (int32_t)it->second.size();
}

int32_t SaveChannel::Get(const char* key, void* out, uint32_t cap) const {
    const int32_t len = GetLen(key);
    if (len < 0) return -1;
    if (cap < (uint32_t)len) return -2;
    const auto it = map_.find(key);
    if (len > 0) memcpy(out, it->second.data(), (size_t)len);
    return len;
}

bool SaveChannel::Remove(const char* key) {
    return key && map_.erase(key) > 0;
}

std::vector<uint8_t> SaveChannel::Encode() const {
    std::vector<uint8_t> out;
    out.reserve(16 + map_.size() * 32);
    out.insert(out.end(), kMagic, kMagic + 8);
    PutU32(out, kSaveVersion);
    PutU32(out, (uint32_t)map_.size());
    for (const auto& [key, val] : map_) { // 仅落盘路径遍历（不进确定序敏感逻辑）
        PutU16(out, (uint16_t)key.size());
        out.insert(out.end(), key.begin(), key.end());
        PutU32(out, (uint32_t)val.size());
        out.insert(out.end(), val.begin(), val.end());
    }
    return out;
}

bool SaveChannel::Decode(const uint8_t* data, size_t len) {
    Reader r{data, len};
    char magic[8];
    if (!r.Bytes(magic, 8) || memcmp(magic, kMagic, 8) != 0) return false;
    if (r.U32() != kSaveVersion) return false; // 版本头：不识别整档拒绝（M7 迁移链）
    const uint32_t count = r.U32();
    if (count > kMaxEntries) return false; // 条目数先验：拒按不可信 count 巨额 reserve
    std::unordered_map<std::string, std::vector<uint8_t>> rebuilt;
    rebuilt.reserve(count);
    for (uint32_t i = 0; i < count && r.ok; ++i) {
        const uint16_t keyLen = r.U16();
        if (r.ok && keyLen > kMaxKeyLen) return false; // key 超长 = 损坏（写侧不可能）
        std::string key(keyLen, '\0');
        if (!r.Bytes(key.data(), keyLen)) break;
        const uint32_t valLen = r.U32();
        // 分配前先验：单值上限 + 剩余字节足够（杜绝按 4 B 头声明 4 GiB 的 vector）
        if (r.ok && (valLen > kMaxValueBytes || r.left < valLen)) return false;
        std::vector<uint8_t> val(valLen);
        if (!r.Bytes(val.data(), valLen)) break;
        if (!rebuilt.emplace(std::move(key), std::move(val)).second)
            return false; // 重复 key = 损坏信号（写侧 map 语义不产生重复）
    }
    if (!r.ok) return false; // 半档拒绝（宁可不载不载错）
    if (r.left != 0) return false; // 尾随垃圾拒绝（写侧精确落盘）
    map_ = std::move(rebuilt);
    return true;
}

} // namespace lemon::ecs
