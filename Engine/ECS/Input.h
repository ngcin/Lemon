// Lemon 引擎 — 输入快照（03 §4 系统 #1 / §12 确定性回放的输入通道）
// 主线程采样 → 拷贝为 POD 快照 → 模拟线程整帧消费；录制/回放即快照序列。
// M2：由宿主（bench-sim 程序化脚本或回放文件）ApplyInput 注入；
//     SDL 键鼠采样接驳在 M5 玩法层。
#pragma once

#include <cstdint>

namespace lemon::ecs {

struct InputState {
    uint64_t buttons = 0; // 语义键位掩码（位分配：bit0=up1=down2=left3=right4=attack5=confirm(重开/确认，M5 批④)6=pause(批③d-2)）
    float ax = 0.0f;      // 移动轴 -1..1（模拟摇杆/键盘合成）
    float ay = 0.0f;

    bool operator==(const InputState& r) const {
        return buttons == r.buttons && ax == r.ax && ay == r.ay;
    }
};

} // namespace lemon::ecs
