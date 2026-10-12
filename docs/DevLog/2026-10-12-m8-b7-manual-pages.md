# M8 批⑦ 日2 —— 手册十一页全数落齐（B 组收口）

- 日期:2026-10-12(批⑦ A/B/C 三组全部收口;余 = 段收官 D 组,gate = 批⑥ B2 判读)
- 批文件:[Plans/M8/2026-10-12-b7-docsite-closeout.md](../Plans/M8/2026-10-12-b7-docsite-closeout.md) §2 B 组
- 前情:日1(基座 + §6.11 + intro/lighting)见 [2026-10-12-m8-b7-docsite-start.md](./2026-10-12-m8-b7-docsite-start.md)

## 1. 交付(十一页,B2–B7/B9–B13)

02 新建项目 / 03 编辑器 / 04 组件目录 / 05 C# 脚本 / 06 场景与多场景 / 07 资产管线 /
09 后处理与战斗表现 / 10 界面 / 11 音频 / 12 出包 / 13 调试。

**验收**:`./tools/build-manual.sh` 生成 **13 页零错误**(站内死链/图片缺失/越子集
语法均硬失败口径下通过 = 导航图全连通、语法全在白名单);01-intro 尾部十三页
导读链全通。构建产物 `build/manual/`,file:// 直开。

## 2. 事实核对清单(写前取证,API 逐名对源码)

- **组件表 34 项**(id 0–33)逐字段对 `Engine/Components/ComponentCatalog.cpp`(单一
  可审计列表);FIELD_RT 运行时态不入档、team 2 非战斗惯例、Script 8 槽同类型唯一
  皆从登记表/头注直取。
- **LemonBehaviour 生命周期七回调 + GameObject 门面**(Add/Remove/Destroy 帧首
  应用两拍)对 `GameObject.cs`;IForEachSystem 批量系统示例对 `Scripting.cs` 签名
  (`ref readonly Chunk`、`Query.With<T1,T2>`);`Time.DeltaTime` 名核错一次当场改
  (初稿误写 Time.Delta)。
- **SceneManager 全 API + 换场帧协议⑤时序 + AsyncSceneLoad(progress 纯呈现量/
  allowSceneActivation 0.9 封顶/completed 恰一次/await 域线程续跑)对
  `SceneManager.cs` 头注契约**;ADR-017 实链(初稿目录虚链,查 ADR 清单后改)。
- **Audio 九 API**(Play/PlayOneShot/PlayAt/PlayBgm/StopBgm/Stop/SetGroupVolume/
  MasterVolume/Paused/StopAll)与 **Ui(RtUi)/UI(RmlUi) 两门面分工**(Ui.cs 49 行/
  GameUI.cs 413 行)逐名核对;TimeScale 不自动映射暂停、BGM 单槽引擎侧、loop 点
  烘焙期口径从头注直取。
- **Fx 门面**(Text×3/Crit/Miss/Bar×2/FxStyle/FxBarSkin)对 `Fx.cs`;**FX 链无脚本面
  参数位**(grep SDK 无 bloom/shockwave 入口)——如实登记为引擎侧质量档驱动,不造 API。
- **质量三档表**对 `Engine/Renderer/Quality.h` TierParams(Low 20k/0.25/32/false、
  Med 50k/0.5/64/true、High 100k/1.0/64/true)。
- **出包命令/参数/自检行**对 `Windows-Build-Guide.md` §6 原文(四 Miss/extra 全 0
  判据、HintPath 重锚、--runtime=构建树根、不支持交叉出包)。
- **两壳旗标表**对 `EditorEntry.cpp` usage 块与 `GameEntry.cpp` CLI 头注原文。

## 3. 手册侧新固化的用户口径(源码有、此前无成文页)

- 结构命令两拍(Add/Remove/Destroy 与动态挂灯同帧查不到 = 语义非 bug)。
- progress 禁挂玩法分支(ADR-017 D3 响亮规则的用户面表述)。
- UI 三层分工选型(世界空间 HUD / RtUi 8 槽 / RmlUi;「忘了 UI.Apply = 屏上不动」)。
- 音频流程冻结防线(Time.Scale=0 不误停 BGM)。
- 挂机/自动化纪律(--vsync 必开 + 窗口可见 + caffeinate,批⑥ soak 四轮教训的用户面)。

## 4. 状态

批⑦ A(基座)/B(内容)/C(§6.11)三组全收口;D 组(08 勾销/AGENTS 瘦身/收官 DevLog)
等批⑥ soak B2 对照跑 A 判读落账后触发。手册 README 页状态表已全勾。
