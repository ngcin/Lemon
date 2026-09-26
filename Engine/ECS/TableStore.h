// Lemon 引擎 — 配置表存储（M6a 批② T2；ADR-012 D1 运行时半段）
// .tab 资产（全字符串格 行×列 网格，第 0 行 = 列头）按资产 GUID 低 32 位登记
// （clipId/prefabId 同款映射约定）。World 持有 + 非 ECS 通道（09 §6.8 先例二：
// Clips/Saves/RtUi/Fx 同族——不入 StateHash，零重录）；编辑器 EnterPlay 一次性
// 建表（BuildPlayTableCache，进 Play 时刻资产快照，Play 中改 .tab 不生效）。
// 解析归编辑器侧（Assets/Csv.h ParseTableJson）——引擎零 JSON 依赖（依赖向下：
// editor→engine）；M7 .baked 打包按同 schema 消费。全字符串格 = 引擎零类型系统，
// 数值解释归 C#（Lemon.Table.Int/Float 容错包装）。表只 Find 不遍历：unordered_map
// 迭代序不进任何确定路径（ClipTable 同款纪律）。
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace lemon::ecs {

class TableStore {
public:
    /// 登记一张表（id=0 / 空网格 = 拒绝；重复 id = 覆盖后者胜——ClipTable 同款）。
    /// 行×列上限归解析器（64×1024×128 字符，ADR-012 D1），此处不再防呆。
    bool Add(uint32_t id, std::vector<std::vector<std::string>> rows);
    /// id 未登记 → nullptr（调用方降级，C# 侧 -1 语义源点）
    const std::vector<std::vector<std::string>>* Find(uint32_t id) const;
    /// 取格（无表/行/列越界 = nullptr；行参差防御——编辑器侧已保证矩形，双保险）
    const std::string* Cell(uint32_t id, uint32_t row, uint32_t col) const;
    void Clear() { tables_.clear(); }
    uint32_t Count() const { return (uint32_t)tables_.size(); }

private:
    std::unordered_map<uint32_t, std::vector<std::vector<std::string>>> tables_;
};

} // namespace lemon::ecs
