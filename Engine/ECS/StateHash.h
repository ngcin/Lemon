// Lemon 引擎 — 确定性状态哈希（03 §12 逐帧回放一致的判定器）
// 对场景全状态做 FNV-1a：组件按注册表 id 序 → 池内 packed 序 → 逐字段字节。
//   * 逐字段（而非整 struct）：padding 字节不保证初始化，整块哈希会产生假阳性分歧；
//   * 定长数组段（StatusEffects/Inventory）按 count + 有效元素哈希；
//   * 同一操作序列下（同 seed/输入/线程档）哈希必须逐帧相等——不等即回放断点。
#pragma once

#include <cstdint>

#include "ECS/Scene.h"

namespace lemon::ecs {

/// 计算场景状态哈希（回放校验用；热路径外，~1ms@万实体）
uint64_t ComputeStateHash(Scene& scene);

} // namespace lemon::ecs
