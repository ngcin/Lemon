# 2D 光照

Lemon 的 2D 光照是**光贴图合成**：场景先渲成一张图，光源画在一张与视角对齐的光贴图上，最后两图相乘。像素风友好、成本可控、默认关闭零成本——场景里一个光照组件都不放，渲染路径与无光照版**逐字节相同**（回归测试逐帧比对机器证，不靠目测）。

## 三段式怎么工作

帧内分三段：场景段渲到 `sceneRT` → 光照段渲 `lightRT`（底色 = 环境光 ambient，光源四边形按混合模式往上叠）→ 合成段全屏相乘输出。血条、飘字、UI 划在合成之后，**不受光**。

编辑器里只有 GameView 渲光照；SceneView 恒不渲（选中光源画 gizmo 圈代替实时预览）。

## Light2D 组件

一个实体挂一个 `Light2D` 即是一盏灯。Inspector 里 Add Component 直接添加，字段全可编辑。

| 字段 | 类型 | 语义 |
|---|---|---|
| `Type` | enum | `Global` 环境光 / `Point` 点光 / `Spot` 锥光 / `SpriteMask` 蒙版光 |
| `Blend` | enum | `Additive` 加亮 / `Screen` 柔和加亮 / `Subtract` 扣暗 |
| `Color` | RGBA 打包 | 颜色（C# 侧用 `Lighting.Rgba(r,g,b)` 打包） |
| `Intensity` | float | 强度 |
| `Range` | float px | 光域半径；Spot/Mask 亦是造型尺度 |
| `Angle` | rad | Spot 张角（方向 = 实体 Transform 旋转，local +X 为前） |
| `InnerRatio` | 0–1 | Spot 内锥比，内外锥间软边 |
| `MaskSprite` | sprite 引用 | SpriteMask 的蒙版图 |
| `Flags` | bitfield | bit0 = 启用（Inspector 的启用复选框） |

**类型语义**：

- `Global`：环境光锚点，至多一个生效；颜色×强度 = lightRT 底色。昼夜循环就是脚本补间这一盏。
- `Point`：径向衰减到零（smoothstep，光域边界无亮环/硬切）。
- `Spot`：`Angle` 张角 + Transform 旋转朝向，角向软边。
- `SpriteMask`：贴图即造型——蒙版因子 = **纹素 rgb × 纹素 a**，无径向衰减；白 RGB + alpha 渐变的 glow 类与不透明灰度蒙版两类素材通吃。悬空 `MaskSprite`（无效引用）提取侧丢弃并一次性 WARN。

**混合语义**：`Additive`（相加，最常用）、`Screen`（1−(1−a)(1−b)，高亮区更柔）、`Subtract`（扣暗，暗斑/投影感）。

> **ambient 使用注意（易踩）**：无 Global 灯 = ambient 白（1.0）。光贴图是 LDR、叠加在 1.0 截断，**Additive/Screen 对满值只加不亮（数学空操作），唯 Subtract 显形**。摆加亮类灯的场景，环境必须留暗（Global 灯 intensity < 1）；ambient 全白是「全亮无影」语义而非中性底。

## BlobShadow2D 组件

椭圆暗斑：在 lightRT 上以 Subtract 扣一块椭圆——便宜的地贴阴影，独立于任何光源存在（无灯时白底上暗斑仍生效）。

| 字段 | 语义 |
|---|---|
| `Radius` | 椭圆长半轴 px |
| `Aspect` | 短轴/长轴比 |
| `OffsetY` | 贴地偏移（+Y 向上） |
| `Opacity` | 0–1 强度；**0 即关**（帧内消失；组件无 Flags 字段，这就是它的开关） |

## 质量分级

`lightRT` 分辨率与灯上限随质量档（编辑器/游戏壳同一套 QualityManager）：

| 档 | lightRT 分辨率 | 每帧灯上限 |
|---|---|---|
| Low | 1/4 | 32 |
| Med | 1/2 | 64 |
| High | 全分辨率 | 64 |

超上限截断 + 一次性 WARN。blob 暗斑独立计数，不挤灯的预算。

## C# 脚本（`Lemon.Lighting`）

定位灯实体 = **调用方职责**（脚本就挂在灯实体上，或按 GUID/场景结构找——SDK 无按组件反查 API）。颜色一律 `Lighting.Rgba(r, g, b)` 打包。

| API | 语义 |
|---|---|
| `SetGlobalAmbient(g, rgba, intensity)` | 整写为 Global 环境光并启用——昼夜脚本的正道入口 |
| `DayNight(g, rgba, intensity, seconds, ease)` | color + intensity 双补间；返回 intensity 补间句柄（等它即等整组） |
| `Enable / Disable / Toggle(g)` | 开关；返回**操作后**启用态（幂等查询语义） |
| `IsEnabled(g)` | 查询 |
| `SetColor / SetIntensity / SetRange(g, v)` | 读改写单字段（保其余字段） |
| `BlobShadow.Set(g, radius, aspect, offsetY, opacity)` | blob 整写 |
| `BlobShadow.SetOpacity(g, v)` | 只改暗斑强度（0 = 关） |

**两拍时序（重要）**：`SetGlobalAmbient` / `BlobShadow.Set` 在实体**还没有**该组件时走暂存添加，返回 `false`——结构变更下一帧帧头才生效，且先落的是**引擎 C++ 默认值**（Light2D = Point/白/启用：第二拍前这盏灯已经可见），下一 tick 再调一次本方法才落你写的值。`Instantiate` + 动态挂灯的场景就是标准两拍；场景里已带组件（序列化摆好的灯）则即时整写返回 `true`，无此坑。

**补间在途禁写同字段**：`Tween` 存活期间拥有字段，同帧脚本写会被覆写（`DayNight` 补间进行中不要再 `SetColor`）。

昼夜循环完整样例（晨→昼→暮→夜四关键帧环形链式推进）：

```csharp
using Lemon;

public sealed class DayNightCycle : LemonBehaviour
{
    private static readonly uint[] kColors = {
        Lighting.Rgba(255, 236, 200), // 晨：暖白微黄
        Lighting.Rgba(255, 255, 255), // 昼：正白满亮
        Lighting.Rgba(255, 160, 96),  // 暮：橙红低角度
        Lighting.Rgba(96, 128, 200),  // 夜：冷蓝压暗
    };
    private static readonly float[] kIntensity = { 0.55f, 0.75f, 0.45f, 0.30f };

    private int _phase;
    private long _handle; // 在途 intensity 补间句柄（0 = 无在途）

    protected override void Start()
    {
        // 场景自带 Global 灯组件 = 即时整写（两拍坑只在动态添加路径）
        Lighting.SetGlobalAmbient(gameObject, kColors[0], kIntensity[0]);
    }

    protected override void Update()
    {
        if (_handle != 0 && Tween.Alive(_handle)) return; // 在途——同字段禁写
        _phase = (_phase + 1) & 3;                        // 接力下一段
        _handle = Lighting.DayNight(gameObject, kColors[_phase], kIntensity[_phase], 20f);
    }
}
```

## 边界与不做的事

- 逐对象受光（anchor 锚点采样）列 v1.1 触发式——消费者出现即做。
- 法线贴图任何形式不做（像素风素材基本无法线贴图）；SDF 几何硬阴影、Max 混合、逐精灵光照材质、光照贴图烘焙均不在范围。
- 设计依据与实测数字：[ADR-019](../ADR/ADR-019-Light2D-Lightmap-Component.md)（方案拍板）、[02 渲染分册 §8](../EngineDesign/02-Rendering-Vulkan.md)（三段式/衰减定形/两壳装配）、[09 测试分册 §6.11](../EngineDesign/09-Testing.md)（smoke 断言与 bench 口径）。
