# M7c 批⑤ — LoadScene 档2 设计定案（ADR-017）

- 日期：2026-10-08
- 关联：[ADR-017](../ADR/ADR-017-Scene-Management-And-LoadScene.md) · [M7c.md](../Plans/M7c/M7c.md) 批⑤–⑩ · M6b 判据⑥ 债清 · [08 砍单清单](../EngineDesign/08-Development-Roadmap.md) 第 8 条转正
- 性质：纯设计会话（三轮：现状考古 → 设计提案 → 用户两问反驳 → 拍板），零代码改动。

## 事件

M7c 候选池"LoadScene 档2"经会话讨论转正式排期。动因 = 用户需求升级（2026-10-08）：幸存者类实际多关卡（草原/森林/火山）+ 武器收集页/成就等多 UI 场景，单场景局限成立；要求参考 Unity 实现含命名 + DontDestroyOnLoad；"很多我没提到的内容"由提案侧主动补全（事件族/Scene 结构体/编辑器集成/确定性/打包面）。

## 开工前核证（节选，全表见 ADR-017）

- **SDK 生命周期零缺口**：Awake/OnEnable/Start/Update/LateUpdate/OnDestroy 全在（`GameObject.cs:136-141`），OnDestroy 恰好一次（F-08.2）——原以为的最大新建件不存在。
- **托管实例在 C# 侧按句柄键控 + 句柄是 registry 局部索引**（`Behaviours.cs:158`、`Scene.h:118`）——架构分叉（D1）的裁决依据。
- 管线/提取/tick 全吃单 `Scene&`；TextureStore eager 全量预载（大资产成本在启动不在换场——LoadSceneAsync 的真异步对象是激活门与游戏侧初始化，资产预取是 M9+ 接口位）；JobSystem 单线程诊断档。
- Unity 语义核查（四源）：同步 LoadScene = 下一帧装载（[官方文档](https://docs.unity3d.com)）；装载序 Awake/OnEnable→sceneLoaded→Start（[论坛](https://discussions.unity.com) + [StackExchange](https://gamedev.stackexchange.com)）；DontDestroyOnLoad 仅根生效、标子 = 根树整体幸存（[CSDN](https://www.csdn.net)）。

## 拍板（D1–D8 均按建议；两处讨论改判）

- **D3 LoadSceneAsync：砍单 → 进计划（批⑧）**。用户论证：TD/ARPG 关卡初始化重（NPC/商店/事件）+ 大地图无进度条体验差，"现在不做，后面还是要增加"。设计回应 = 统一管线（同步 = async 无预算特例，Unity 同步本就下一帧装载）+ progress 呈现量契约 + Assets 阶段占位（M9 tilemap/按需装载接入位）。
- **D2 Additive：维持 v1 预留**。用户问询塔防/守卫剑阁副本形态 → 裁定不需要（守卫剑阁 = 单图触发器形态；副本 = Single+读条 WoW/Diablo 式）；真消费者 = ARPG 连续无缝大世界。五类用途评估表落 ADR-017 备查，免半年后重新论证。
- D1 单 registry + SceneMembership（DDOL = 改标签 O(1)，实体不死实例续命）/ D4 路径>唯一 stem>红字、无 build index / D5 DDOL 非根 WARN+作用于根树 / D6 Time.Scale 保留（Audio.Paused 反之换场强制清）/ D7 DontDestroyOnLoad = LemonBehaviour 静态 / D8 flat `Lemon.SceneManager`——均按建议。

## 登记与债清

- M7c 批⑥–⑩ 预登记（引擎核心 / SDK 门面 / LoadSceneAsync / 消费者迁移 / 编辑器打磨可选）；批⑥ 开工前落批文件（文件/行级分解 + 全 `Scene&` 调用面盘点 + membership 哈希流口径定案）。
- **M6b 判据⑥ 债清**：2026-09-30 收官时"LoadScene 档2 评估结论落 ADR"挂账（ADR 停在 016）→ ADR-017 即清偿；M6b.md 判据行已加注记。
- 08 砍单清单第 8 条（LoadScene 后置项）退役，转 M7c 批⑤–⑩ 正式排期。

## 实测数字

无（纯设计会话，零代码改动；核证事实与来源见 ADR-017"开工前核证"）。
