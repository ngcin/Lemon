# 2026-09-24 · M6a 批⓪：scripts[] 多脚本 + sprite 引用 GUID 化（架构地债券清偿）

来源：[M6a 批文件](../Plans/M6a/2026-09-24-b0-multiscript-guid.md)（T1→T6 六任务一日
连打通，四个 commit：`6832981` / `8806be8` / `88c184b` / `225d4f4` + 收尾文档批）。
动机 = 08 §2 重排定案：多脚本与 GUID 晚做返工面最大（模板拆脚本、用户项目实体
组织、prefab/场景档格式全压其上），批⓪ 先行清地基。

## 落地面

1. **ScriptBox 多槽（T1）**：内嵌 8 槽 `ScriptSlot`（旧单槽同构 40B）；实体级
   `notified`（F-08.2 恰好一次）+ 槽级 disabled；`.scene` schema 1→**v2**
   `scripts[]`（迁移链首例——.prefab 不走迁移链靠双读隐式升级）；AttachBehaviour
   拆追加/解析两路。**同实体同类型唯一**（决策 4，两轮论证推翻 Unity 对齐默认）：
   入口三闸（Inspector 置灰 / C++ 槽层幂等 / Behaviours.Attach 红字断言）+ 格式
   宽容（加载同名保序留首见）。
2. **spriteGuid（T2）**：SpriteRenderer 尾加 `uint64_t`（12→24B，C# 镜像/探针
   同步）；`EditorContext::ResolveSpriteRefs` 挂四站点 + Prefab 编辑态落地；存量
   自动回填（guid=0 + 登记本体号 → 补写标 dirty）；Inspector 三口双写。
3. **SDK 双路由（T3）**：`IComponent` 标记 + `GameObject.Add/Get/RemoveComponent<T>`
   统一 `where T : notnull` 运行时分路（值组件经 Type→id 反查 + 开放泛型策略类
   反射绑定一次，零装箱）；op5 `DetachScript`（单槽卸载：OnDestroy+退订+保序移除）
   + `lemon_scripts_detach` 导出（旧 Entry 挂空安全）。
4. **模板三拆（T4）**：PlayerBehaviour 239 行拆 Movement/Combat/Hud；共享态归
   `GameMain.Run` 静态（切面记录见下）；注册序 = 执行序 移动→战斗→HUD；生成器
   sprite 写号全面改 ByGuid（携切片表 cell-0 惯例）；模板重生成入库。
5. **--smoke-guid（T5）**：三难并发（插入+改名+删 manifest）→ 新 ctx 重开 →
   逐实体归一断言（drift 7/7、renamed OK、dangling 0）；回归脚本第 14 步。

## 切面记录（T4，供批②配置表参考）

- `_runTime/_kills/_best/_dead` 四共享态归 `GameMain.Run`——战斗写、HUD/移动读；
  热重载迁移由 PlayerCombat 的 StateBag 代收代还（静态随 A 线换域重建，不经
  StateBag 通道，必须由某实例带包）。blade 自愈/三选一轮换/死亡结算留在战斗侧
  （与击杀事件同源，不再切）。
- 跨类型 Update 序 = 注册序（dense 桶 (ExecutionOrder, 注册序)）——槽序只影响
  Inspector 展示与序列化键序，**不是执行序**（Unity 心智差异，04 §3.2 已记）。

## 实测坑（当日修）

- **切片表本体/cell 号相邻歧义**：smoke-guid BossMob 行实证——旧 cell-0 号跨漂移
  后恰好撞新本体号被"区间内"误保。对策：切片表一律按 cell 口径归一（区间外回
  cell 0，本体引用降级；逐 cell guid 化留 M6c (guid, cell)）。
- ExitPlay 逐字节比对与解析次序：ResolveSpriteRefs 必须在比对**前**（无资产变动
  = 幂等无写、比照相常成立；真有变动 = 如实报不一致——引用升级非泄漏）。
- TestScript 表尾追加新探针会改热重载列表断言计数（10→11）——连带翻新。

## 验证落账

ctest 3/3（新增 TestSpriteGuidResolve + TestSdkDualRoute）；m5b2 三档金回放
mismatches=0（**零重录先例三**落 09 §6.8：加字段 × 基准场零实例）；smoke-template
全链 PASS（行为面与拆分前逐项一致）；editor-regression full 13/14（唯一 FAIL =
09 §8 记档 smoke-ui 像素注入偶发，复跑绿）；bench-survivor **fps=82**（≥55 判据，
M5 基线 58~67 上方）；模板重生成 diff 复核 = 逐运行随机面（实体 guid/时间戳）外
逐字节一致。
