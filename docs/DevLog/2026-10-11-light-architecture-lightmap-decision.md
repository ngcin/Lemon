# 2026-10-11 光照架构决策 —— M8 改道光贴图合成 + Light2D 组件模型

## 事件

用户在 M8 开工规划讨论中拍板（2026-10-11）：**放弃 02 §8 原设计"Luma 裁剪延迟（法线 RT + 点/锥光 + blob）"，改为"yami 式光贴图渲染架构 + Unity 式 Light2D 组件模型"**，Luma 全套判定过重不采用。决策全表 = [ADR-019](../ADR/ADR-019-Light2D-Lightmap-Component.md)（D1–D8 均按建议采纳）。

三方案对比（本日实测代码量核实）：

| 方案 | 架构 | 代码量级 | 取舍 |
|---|---|---|---|
| yami 光贴图 | 场景画完后光源叠进一张 lightRT、全屏乘回；无逐对象材质 | 数百行级 | **渲染架构采纳**（三段式 + 混合模式 + 采样模式语义） |
| Unity URP 2D | 逐排序层光纹理 + 逐精灵材质采样，可选法线 | 引擎内置不可比 | **组件模型采纳**（Light2D 四类型/组件粒度/Add Component 形态） |
| Luma 全延迟 | GBuffer/法线 RT + 烘焙/探针/面积光/间接光 | 实测 ~7,400 行 C++ + ~2,100 行 WGSL | 否决（3D 级件占 2/3，与本品类不匹配） |
| （原设计）法线 RT 裁剪延迟 | Luma 直译裁剪 | 估 2,000–3,000 行 | 否决（像素风素材无法线贴图 → 视觉收益不成立；MRT/变体管理成本高） |

新方案估 1,500–2,500 行，且**不碰精灵热路径**（bindless 实例化管线零改动——刚过 fps 门禁的管线零风险）；与后处理链共享同一套全屏 pass 基础设施（批② 铺、批④ 消费）。

关键动因两条：

1. **美术现实**：法线 RT 的收益完全依赖素材带法线贴图，像素风素材包（含在用的 yami-dungeon）基本不提供；无法线图时延迟光照观感与光贴图乘法相当。割草品类的真实光照价值（彩色光池/昼夜/枪口闪光/辉光）光贴图全覆盖。
2. **工程成本**：光贴图零 MRT 零 sprite.frag 变体零图集链路改动；法线方案每一处都踩已登记技术债触发器（bindless 翻倍 #L20、Renderer 包含 #L34、变体管理）。

法线路径不删——转**二阶段触发项**（触发 = 素材带法线贴图的游戏立项，届时另立 ADR）；SDF 几何硬阴影维持不做（砍单序列第 2 条语义收窄，ADR-019 D6 治理）。

## 新口径

- 光照 = **Light2D**（Global/Point/Spot/SpriteMask 四类型 + Additive/Screen/Subtract 三混合）+ **BlobShadow2D**（lightRT 上 Subtract 椭圆暗斑）两组件；**Add Component 挂组件**（非 Create 菜单项——用户问询的编辑器形态就此定形）。
- 渲染三段式：sceneRT → lightRT（ambient 底 + 光源叠加）→ 全屏乘法合成 → UI 段后画；**关灯零成本** = 零 Light2D 且零 BlobShadow2D 时整链跳过、两壳路径与今日逐字节同（08 M8 验收的机械口径，批② smoke 像素断言证）。
- 逐对象受光 v1 = 全局 + 段豁免（血条/飘字/位图文本/UI 不受光）；yami 的 **anchor 锚点采样**（角色走进光里）列 v1.1 触发式，实现路径已备（CPU 染 colorBits 零 shader 改动）。
- vtable **零新增槽**（59 维持，M23 余量 13）；组件读写走 op2/op3（AudioSource 先例）；`Lemon.Lighting` 门面纯 C#。
- 质量分级：lightRT = 1/4·1/2·全分辨率（Low/Med/High），光数上限 64（Low 32）——QualityManager 首次接入两壳。
- M8 批次定形（[M8.md](../Plans/M8/M8.md)）：⓪ 设计落账 ✅ 当日 → ① 组件 → ② 编辑器光贴图竖切 → ③ lemon-game+蒙版+blob+SDK → ④ 后处理链 → ⑤ 模板消费+真人走查 → ⑥ 压测 A+soak（#M4/#L5 触发点）→ ⑦ 文档站+收官。
- 已知代价：两组件注册 ⇒ **金回放三档一次性重录**（组件名入哈希流，ADR-015 先例），批① 内闭环；bindless +2 槽（#L20 记账）。

## 不变项

- 执行序 M7c ✅ → M8 → M9 → M7b（08 文首注记口径不动）。
- 回归基线不动：full 22 步 / ctest 4/4 / 单测 34,754 / script-tests 1,834 / bench-survivor 门禁 fps≥76.5 / 金回放 mismatches=0（重录后口径）。
- 移植红线不动：yami JS 禁拷（架构思想借鉴、实现原创）；Luma 本次同样不拷（原 WGSL→GLSL 直译路线随本决策废弃）。
- M8 其余范围不动：后处理链（bloom/冲击波/色调）、压测 A 终测、2h soak、文档站 v1、incremental 模板（若余量）。

## 落账

- 新建 [ADR-019](../ADR/ADR-019-Light2D-Lightmap-Component.md)（D1–D8 拍板全表 + Non-goals + 后果风险）。
- [02-Rendering](../EngineDesign/02-Rendering-Vulkan.md)：§8 整节重写（修订标注）；§4 Light Pass 行、§3.4 sprite.frag 注释、§10 移植表 Luma 行三处法线残迹清理。
- [08-Development-Roadmap](../EngineDesign/08-Development-Roadmap.md)：§0 总览表 M8 行改道注记 + §2 M8 节设计变更 blockquote 与正文同口径 + §4 砍单第 2 条语义收窄注记 + #L34"预计不触发"注记。
- 新建 [Plans/M8/](../Plans/M8/M8.md)：总览页（现状盘点 11 项 + 批次表 ⓪–⑦ + 架构债对表）+ [批⓪ 批文件](../Plans/M8/2026-10-11-b0-design-and-docs.md)（done）+ [批① 批文件](../Plans/M8/2026-10-11-b1-components.md)（planned，触点清单行级预填）。
- 本条目（决策类 DevLog，写完即终稿）。
