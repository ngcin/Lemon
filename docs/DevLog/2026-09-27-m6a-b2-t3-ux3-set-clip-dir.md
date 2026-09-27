# 2026-09-27 M6a 批② T3-UX3：集内动画文件落位改集子文件夹（跨集同名互相覆盖的设计修复）

## 事件

用户实测第四轮反馈：player.ani / player1.ani 都有 run 动作，Assets 下只有一个
run.clip，后保存的把先保存的覆盖了；另一处保存报"编辑目标条目丢失（重扫后
重试）"重扫无效。用户判断"这个设计有问题"——**判断正确**。

根因链（批文件
[2026-09-27-b2-t3-ux2 §7](../Plans/M6a/2026-09-27-b2-t3-ux2-anim-workbench-round2.md)）：
裸名落集目录 → 跨集同名撞同一文件；TryCreateClip 守卫只查内存 DB（磁盘孤儿被
POSIX rename 静默覆盖）；墓碑复活继承旧 guid 把两个集接到同一文件；悬空墓碑
重扫不可救。

修复（v3.3）：

- `SetClipDir()`：动画文件落 `Assets/<集名>/`（player.ani → Assets/player/run.clip）；
  四处落位统一（新建/复制/重命名/建集首段）。集成员关系语义不变（显式引用）。
- TryCreateClip：磁盘孤儿拒写 + 建父目录。
- SaveAll 悬空错误信息给出修复路径（悬空标记 + 从集移除 + 重建）。

## 实测

| 项 | 结果 |
|---|---|
| cmake --build --preset mac | 通过 |
| --smoke-anim（无头 120 帧） | errors=0，rt/cache/whole/set `=> OK` |
| tools/editor-regression.sh full | 14/14 PASS |

## 教训

- "每角色都有 run/idle"是动画资产的常态命名分布——**存放位设计必须假设跨容器
  同名**，裸名落共享目录等于把碰撞留给用户（T3c 设计时只防了集内重名）。
- 静默覆盖类 API（POSIX rename / WriteFileAtomic）上游必须双查（DB + 磁盘）；
  墓碑复活 + 裸名落位的组合会把"复活继承旧 guid"从收益变成跨容器接线事故。
