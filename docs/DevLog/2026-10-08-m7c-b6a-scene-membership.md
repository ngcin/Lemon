# M7c 批⑥ 切片 a — SceneMembership 数据面 + 场景档案 + SceneArchive::BuildInto

- 日期：2026-10-08
- 批文件：[2026-10-08-b6-scene-membership-core](../Plans/M7c/2026-10-08-b6-scene-membership-core.md)（开工首查两项同日落账：`Scene&` 调用面盘点 + membership 哈希流口径）
- 关联：[ADR-017](../ADR/ADR-017-Scene-Management-And-LoadScene.md) D1/D3 · 前情 [批⑤ 设计定案](./2026-10-08-loadscene-tier2-design.md)
- 性质：批⑥ 第一切片（用户拍板小批推进）；纯引擎数据面，C# 门面归批⑦。

## 实测数字

| 项 | 结果 |
|---|---|
| 单测 | **34,481 checks OK**（基线 34,435 + 新增 46——SceneTests 五件） |
| ctest | 4/4 |
| 回归 full | **20/20**（zh 态整跑；en 态首跑 19/20——smoke-anim `row=NO` = 批③ i18n 登记项逐位复现，机器语言遗留 en 所致，切回 zh-CN 全绿，非本批引入） |
| bench-survivor 门禁 | fps=90 ≥ 76.5 |
| 构建 | 零警告（附带修批① 遗留 `GameEntry.cpp:515` unused 警告） |

## 落地

1. **`Engine/ECS/SceneMembership.{h,cpp}`**（新）：组件（scene 句柄 + flags，bit0 = DDOL）+ 四原语——StampSceneMembership（**仅收编未打标实体**：增量装载不动幸存者 scene/flags）/ QueueDestroySceneGroup（两阶段入队，OnDestroy 走既有提交通知路径）/ MarkDontDestroyOnLoadTree（根树整体幸存，CollectSubtree 同款链遍历 + 深度护栏）/ Count×2。ScriptBox 同款纪律：不入注册表/序列化/StateHash。
2. **World 场景档案面**：`SceneRecord{handle,name,path,isLoaded}` + 发号/查找/ActiveSceneHandle；`scenes_`/`active_` registry 面原样保留（编辑器双 registry 与 GameEntry 现状零改动）。
3. **SceneArchive::BuildInto**：Load 拆 ParseSceneDoc/ApplySceneName/BuildEntities 三段共用，BuildInto = 不清空追加装载（ADR-017 D3 Build 阶段落位；坏档 false 且场景不动）；Load 行为逐位不变（坏条目丢弃/回收/name 恢复/迁移链全保留）。
4. **SceneTests 五件**（tests/engine/ 第八 TU + 三处注册）：打标计数与增量收编 / 组清场保 DDOL（根树）/ BuildInto 共存 + Load 清空对照 / StateHash 对 membership 不敏感（零重录钉住）/ World 档案发号寻址。

## 实现期发现

- **Stamp 增量语义**（批文件收口段详）：原稿全量覆写会抹 DDOL 来源组——测试驱动抓出后改"仅收编未打标"。
- **未指派组口径**：无组件实体 ≠ 不存在，须与 scene==0 同组计数（首跑 FAIL 即此，修正后绿）。
- 回归机器态 `~/.lemon/editor-settings.json` 遗留 en（批③ smoke 测试后未复位）→ 切回默认 zh-CN；根因（smoke 不固定语言）仍在候选池待修。

## 下一批

批⑥ 切片 b：换场协议编排（组清场 → DDOL 保留 → 随行清扫链（Fx 非实体附着清 / UI origin=Scene 卸 / Audio.Paused 清）→ BuildInto + 打标）+ smoke-scene 单跳；切片 c：多次换场 + DDOL 回放轨迹用例 + 03 分册 §2 注记。
