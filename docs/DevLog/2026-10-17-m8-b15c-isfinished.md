# 2026-10-17 M8 批⑮c — Anim.IsFinished 播放状态查询补齐（机器面收口）

批文件：[Plans/M8/2026-10-17-b15-anim-runtime-semantics.md](../Plans/M8/2026-10-17-b15-anim-runtime-semantics.md) §2 行 3。
上一条目：[⑮a 终态显式建模](./2026-10-17-m8-b15a-terminal-state.md)。

## 改动

- **SDK**（`Lemon.SDK/Anim.cs`）：
  - 新增 `Anim.IsFinished(GameObject)`——直读 `Animator2D.Ended`（FIELD_RT 镜像，
    M6a 批① T3d 落的段末边沿：引擎 Once 段钳 total 置位、`Play` 切段 SDK 归零）。
    **零新桥、零引擎改动、零 vtable 槽**（60 不变，b15 §2 行 3 口径兑现）。
  - `IsPlaying` 注释改三态口径：Playing / Paused / Finished 互斥面组合完整；
    删去旧注释"播完 SDK 侧不可判"的过时表述（ended 镜像本就可达）。
- **script-tests**：
  - `AnimFinishedProbeBehaviour`（typeId 28，表尾注册——0 基第 29 个；探针清单
    断言 28→29）；帧序：帧1 loop 段 → false（1570）；帧2 引擎置位 → true（1571）；
    帧3 `Play` 切段；帧4 落地后读 → false（1572）后自毁。码位 1573/1574 = 反例
    哨位（计数器不观测，收到即暴露在失败计数差里）。
  - C++ 夹具 `TestAnimFinishedSdk`（`Tests/script/main.cpp`）：管线不含
    AnimatorSystem——帧间 C++ 直改 `Animator2D.ended` 模拟引擎段末置位；帧3 末
    对拍 `clipId=0x88 && ended==0`（Play 写落字段契约）。

## 实测侧证（踩坑记录）

- **Native.Write 帧末落地、同帧读回是旧值**：首版探针帧3 `Play` 后同帧读
  `IsFinished` 得旧值 1（引擎侧组件本帧末才更新）——探针读挪一帧（帧4）解。
  该行为与 AnimFx 探针"帧末对拍"口径一致，登记为 SDK 读时序的既有语义
  （不是缺陷：读 = Native.Read 活读引擎态，写 = 帧末应用）。

## 门禁

- 构建（mac preset）零警告；ctest **4/4**；script-tests **1,924**（1,918 + 6）；
  单测 35,024 持平（SDK-only 批，引擎面零改动）；vtable 60 / 组件 33 / 系统 21
  不变；金回放不受影响（零引擎改动、零默认档行为漂移）。

## 余项

- ⑬c 动画事件 payload 类型化 → 下一批；批⑭（带 ⑮d 横幅 + Manual 动画章）随走查；
  ⑮b facing 已拍板挪 M9 首批。Manual 动画章 IsFinished/IsPlaying 三态口径随批⑭。
