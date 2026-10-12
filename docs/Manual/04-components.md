# 组件目录

Lemon 是 ECS：**组件 = 纯数据**（POD，字段冻结），行为由引擎系统或 C# 脚本驱动。
组件在反射注册表登记（登记序 = id，只增不改序），Inspector/Add Component/序列化/
C# 镜像四处共用一份元数据——新组件对编辑器零代码可见。

关键字段速查（完整字段面见 Inspector 与 C# 镜像 `Lemon.Interop.Components`）：

## 核心（id 0–4）

| 组件 | 关键字段 | 用途 |
|---|---|---|
| `Transform2D` | pos / rot / scale | 位形（每个实体都有） |
| `Velocity` | v | 线速度（Movement 积分） |
| `Hierarchy` | parent 等 | 父子链（引擎维护，勿手改） |
| `Meta` | prefabId / team / layer / tag / guid | 实体元信息（guid = 持久身份，Undo/存档找回） |
| `DestroyQueueTag` | — | 销毁队列标记（tag 组件） |

## 渲染（id 5–8 + 28/29）

| 组件 | 关键字段 | 用途 |
|---|---|---|
| `SpriteRenderer` | spriteId / colorRGBA / sortOrder / sortingLayer / flags | 精灵绘制（flags：bit0 flipX / bit1 flipY / bit2 enabled）；引用真源 = spriteGuid 双写 |
| `Animator2D` | clipId / speed / loop（Once/Loop/PingPong）/ playOnStart | 帧动画播放 |
| `ParticleEmitterRef` | emitterId / playing | 粒子发射器绑定 |
| `SortingOverride` | order | 排序微调（层 → orderTier → 批键） |
| `AnimGraph` | controllerGuid / setGuid | 动画状态机绑定（.controller + .override 资产） |
| `AnimParams` | param0–7 | 状态机参数黑板（运行时态，不入档） |

## 行为/AI（id 9–20）

| 组件 | 关键字段 | 用途 |
|---|---|---|
| `Health` | max / cur / iframeWindow | 生命与无敌帧 |
| `Mover` | speed | 匀速移动（配 Velocity） |
| `Patrol` | a / b / pauseTime | 两点巡逻 |
| `Chase` | speed / aggroRange / keepRange / targetTeam | 追击（按 team 找目标） |
| `Flee` | speed / range | 逃离 |
| `Shooter` | projectileId / interval / range / targetTeam | 发射弹幕 |
| `Projectile` | speed / lifetime / damage / pierce / homing / hitRadius / knockback | 弹体 |
| `Spawner` | prefabId / interval / burst / range / maxAlive | 刷怪/刷物 |
| `Hazard` | dps / tickInterval / radius | 范围伤害区 |
| `Collectible` | kind（Gem/Coin/Heart）/ magnetRadius / magnetSpeed / value | 拾取物 |
| `Trigger2D` | triggerId / once / radius | 圆形触发器 |
| `Knockback` | impulse / decay | 击退冲量 |

## 玩法数值（id 21–27）

| 组件 | 关键字段 | 用途 |
|---|---|---|
| `Stats` | moveSpeed / attack / defense / critRate / critDmg / pickupRadius / luck | 角色属性面板 |
| `StatusEffects` | active[4]（id/stacks/remain/source） | 状态效果槽 |
| `Inventory` | items[16] / gold | 物品与金币 |
| `Equipment` | weaponId / armorId / relicIds[3] | 装备 |
| `XpProgress` | xp / xpToNext / level | 经验升级 |
| `IncrementalState` | rate / multiplier / cached | 增量品类数值 |
| `WaveDirector` | spawnTeam / capAlive / waves[16] | 波次导演（波表可在 Inspector 逐格编辑） |

## 界面 / 音频 / 光照（id 30–33）

| 组件 | 关键字段 | 用途 |
|---|---|---|
| `UIDocument` | sourceAssetGuid / showOnStart / modal | 一组件 = 一屏 RmlUi 界面（详见 [界面](10-ui.md)） |
| `AudioSource` | clipGuid / volume / refDist / maxDist / group | 挂实体的循环/空间声（详见 [音频](11-audio.md)） |
| `Light2D` | type / blend / color / intensity / range / angle / maskSprite | 2D 光源（详见 [2D 光照](08-lighting.md)） |
| `BlobShadow2D` | radius / aspect / offsetY / opacity | 椭圆地贴阴影 |

## 使用注意

- 带「运行时」提示的字段（如 Chase.target、Animator2D.nextClipId）是运行时态：
  **入状态哈希（可回放）但不入场景档**——每局由系统或脚本重播种。
- team 字段是势力引用（Team 表在 `Data/` 声明，势力关系矩阵决定追击/伤害阵营）；
  非战斗实体给 team 2（静态原点）避免进可追击表。
- 同实体同类型组件唯一；C# 侧读写用镜像 struct（`Lemon.Interop` 命名空间）。

> 设计依据：[03 ECS 分册](../EngineDesign/03-ECS-Runtime.md)；登记表本体 =
> `Engine/Components/ComponentCatalog.cpp`（单一可审计列表）。
