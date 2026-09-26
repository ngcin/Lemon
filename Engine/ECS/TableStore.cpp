// Lemon 引擎 — 配置表存储实现（M6a 批② T2；TableStore.h 头说明）
#include "ECS/TableStore.h"

namespace lemon::ecs {

bool TableStore::Add(uint32_t id, std::vector<std::vector<std::string>> rows) {
    if (id == 0 || rows.empty()) return false;
    tables_[id] = std::move(rows); // 重复 id = 覆盖（后者胜）
    return true;
}

const std::vector<std::vector<std::string>>* TableStore::Find(uint32_t id) const {
    const auto it = tables_.find(id);
    return it == tables_.end() ? nullptr : &it->second;
}

const std::string* TableStore::Cell(uint32_t id, uint32_t row, uint32_t col) const {
    const auto* t = Find(id);
    if (!t || row >= t->size() || col >= (*t)[row].size()) return nullptr;
    return &(*t)[row][col];
}

} // namespace lemon::ecs
