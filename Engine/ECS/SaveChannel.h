// Lemon 引擎 — 游戏存档通道（M5 批④ D1；06 §10 M5 口径）
// World 级内存 KV（key → bytes）：C# Lemon.Save 写读，编辑器域 IO 钩子落盘。
// 呈现/用户数据段——不入 StateHash（ComputeStateHash 只哈希 Scene），回放零漂移。
// 文件格式 = 定长头二进制（M5 版无压缩；gzip 归 M7 packager，06 §10 修订注）：
//   "LEMONSAV" magic(8) + u32 版本(1) + u32 条目数 + 每条 { u16 keyLen, key,
//   u32 valLen, val }。防损坏三件套在编辑器 IO 侧：版本头 + 原子改名 + .bak。
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace lemon::ecs {

class SaveChannel {
public:
    /// 写/覆盖一条（len 0 = 空值合法）。key 限 255 字节（超长拒绝返回 false）。
    bool Set(const char* key, const void* bytes, uint32_t len);
    /// 长度（-1 = 无此键）
    int32_t GetLen(const char* key) const;
    /// 拷贝到 out（cap 不足返回 -2 不拷贝；无键 -1；成功返回字节数）
    int32_t Get(const char* key, void* out, uint32_t cap) const;
    bool Remove(const char* key);
    void Clear() { map_.clear(); }
    uint32_t Count() const { return (uint32_t)map_.size(); }

    /// 编解码（编辑器落盘/载入专用；Decode 清空后重建，坏数据返回 false）
    std::vector<uint8_t> Encode() const;
    bool Decode(const uint8_t* data, size_t len);

private:
    std::unordered_map<std::string, std::vector<uint8_t>> map_;
};

} // namespace lemon::ecs
