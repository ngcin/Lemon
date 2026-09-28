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

// ---- 存档分档（M6a 批② T5；06 §10 三档三文件 {slot_0,settings,meta}.sav）----
// 格式复用零版本变（LEMONSAV v1）；每档一个 SaveChannel 实例 + 独立文件，坏档
// 兜底按档隔离。档内结构 = 纯 KV，引擎不解析——两侧键约定（防后续里程碑撞僵
// schema，2026-09-28 用户口径）：
//   settings：版本化 KV——首键 "version"（整数字符串）= 键集结构版本；其余键 =
//     设置项自由增长（M6b 音量/手柄键位/画质档位直接加键），别把结构定死成字段；
//   meta：收集条目预留 "col.<条目id>.state" / "col.<条目id>.count"（条目 id →
//     状态/计数映射）；全局统计平键（如模板 vs.best）。
inline constexpr uint8_t kSaveChannelCount = 3;
inline constexpr uint8_t kSaveSlot = 0;      // 局内进度（v1 固定 slot_0；多档切换 v1.1 候补）
inline constexpr uint8_t kSaveSettings = 1;  // 设置（跨局、非进度）
inline constexpr uint8_t kSaveMeta = 2;      // 纪录/收集（跨局）
/// ch 越界回落 slot（消费侧先行红 warn；编辑器循环只传常量不会触发）
inline uint8_t ClampSaveChannel(uint8_t ch) {
    return ch < kSaveChannelCount ? ch : kSaveSlot;
}

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
