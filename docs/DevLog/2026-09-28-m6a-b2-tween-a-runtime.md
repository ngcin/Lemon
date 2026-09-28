# M6a 批②外挂：A 档运行时补间 Lemon.Tween 落地

2026-09-28 · 用户插入项（批② 计划外）。当日讨论：三档评估（A 运行时 tween API /
B 关键帧属性动画+编辑器 / C 完全体）→ 用户拍板「实现 A 档即可，编辑器之类的不
列入计划」。批文件 [2026-09-28-b2-tween-a-runtime.md](../Plans/M6a/2026-09-28-b2-tween-a-runtime.md)。

## 事件

- 引擎新通道：`ECS/TweenTable.{h,cpp}`（World 持有非 ECS 指令表，不入
  StateHash——效果经组件字段入哈希，基准场零调用零漂移）+ `TweenSystem`
  （插 AnimGraph 后、事件派发前，注册序尾插不占 RNG 子流；空表早退）+
  `GameEvent.TweenFinished` 表尾追加（src=实体、userArg=句柄）。
- ABI 表尾追加 4 桥：`tweenTo/tweenKill/tweenKillEntity/tweenAlive`（ScriptHost
  双侧 + NativeApi.cs 镜像；旧宿主判空降级）。
- SDK 新面：`Lemon.SDK/Tween.cs`——`To<T>` 通用按名字段版（float/Vec2/uint 三
  形参重载）+ Position/Scale(+uniform)/Rotation/Color/Alpha 糖 + Alive/Kill/
  KillAll。GameEvent 镜像同步（Interop/Components.cs）。
- 核心语义两条（03 §8.3 已回写）：**存活补间拥有字段**（同帧脚本写被覆写、
  同字段新建顶替、Kill/完成后归还——瞬时指令最近者终审，与 Animator 的持久
  档面脚本终审相反但同族自洽）；**当 tick 首写**（脚本批后跑，Update 里发起
  立即见效；完成事件当帧派发）。
- 字段寻址 = FieldMeta 按名（零新反射），建时解析存偏移逐 tick getFn 直写；
  白名单 Float/Vec2/UInt32（颜色四字节通道插值，0.5 处 127.5→128 四舍五入）。
- 缓动 9 种纯函数（Linear/Quad×3/Cubic×2/Back/Elastic/Bounce）；Mode Once/
  Yoyo（三角波永续）；timeScale=0 冻结（dt 缩放先于系统）；duration≤0 即完成。

## 验证

- **阴性验证两态**：测试管线注释掉 TweenSystem（引擎其余全在）→ script-tests
  红 `FAIL tween: 当 tick 首写（0.25 → 1.5）`（补间建了没人推进，scale 停
  1.0）；接回 → 绿 1666 checks。
- ctest 3/3：engine-tests 新增 `TestTweenTable`（拒建三路径/顶替/OutCubic(0.5)
  =1.875 精确/Yoyo 折返 7.5/完成恰一事件/颜色字节 0x80/Kill 幂等/销毁自清）；
  script-tests 新增 `TestTweenSdk`（TweenProbeBehaviour typeId 15 表尾：五相位
  状态机——Once 线性精确值 1.5/2/2.5/3、Yoyo 峰值与折返、Kill 冻结、颜色字节、
  **字段所有权反面**（脚本每帧写 pos=99 被覆写 15、KillAll 后 99 站住）、
  轮询句柄亡/活）；`TestSystemPipelineOrder` 18→19（"Tween" 插位锁）。
- 探针清单计数 15→16（热重载段同步）。
- 回归 full **首跑 13/14**（smoke-drag rot -1.570 vs -1.571——T1/T3 起有案的
  首跑加载抖动，与补间无关）→ 复跑 **14/14**。
- 过程修三处自察：① Advance 收敛段原「二次重算谓词」漏组件被摘情形 → 改单趟
  scratch 收敛；② KillField 引用不存在的 FieldNameOf → 改注册表名→偏移解析；
  ③ EaseOutBounce 同表达式 `t -= … * t` 未测序修改（-Wunsequenced 真 UB）→
  拆中间变量。测试自身一处：替身句柄用 Linear 却断言 OutCubic 精确值 → 统一
  OutCubic。

## 遗留

- 真人验收（手感项）：模板/玩法里加受击闪白（`Tween.Color` 0.1s）+ 拾取弹跳
  （`Tween.Scale` OutBack）体验——代码面无阻塞。
- B 档（关键帧属性动画 + dope sheet 编辑器）不立项（用户拍板）；本批缓动纯
  函数 + FieldMeta 字段寻址为将来 B 档直接复用的地皮。
- 编辑器 Inspector 无补间可视化（A 档无编辑器面，属定义内）。
