using System;
using Lemon;

/// <summary>流程状态机（M7c 批⑨ 多场景形态；前身 = M6b 批③d-2 档1 单场景版——
/// RunSweeper 清场补丁退役，引擎换场清场（批⑦ D2「除 DDOL 系外全清」）接管）。
/// 跨场壳（批⑨ D1）= 本实体 DDOL（Awake 自标 + MainMenu 重装新种子 Booted 守卫
/// 自毁）+ 流程文档 code-mount（UI.Show 通道 B 现载 = origin=CSharp 跨场幸存
/// ——换场 sweep 只卸 origin=Scene，场景声明式 UIDocument 不跨场）。
/// 换场门面（批⑨ D5）：进战斗 = LoadingScreen.Begin（LoadSceneAsync + 加载屏，
/// 批⑧ 样例消费）；回菜单 = 同步 LoadScene("MainMenu")。战斗场 Grass.scene
/// 自含 Player/Director（批⑨ D2）——装载即开局，重开 = 重装载，无 spawn 握手。
/// 键盘位（批⑨ D3，批⑦ 敞口② 修法）：菜单 R=开始；结算 R=重开 / Esc=回菜单
/// ——语义位入 InputState = 可回放；鼠标点击路径为人用（非回放口径）。
/// **死亡策略归游戏侧**（何时复活/何时结算由 PlayerCombat.Die 决定；本模板示例
/// = 每局一次复活，改复活道具/表驱动只动那一处分叉）。</summary>
public sealed class GameFlow : LemonBehaviour
{
    // 模板资产 GUID（{GUID:…} 占位符——生成期自 VsTemplateGen.h 常量回填，勿手写 hex）
    // M6c 批④：BGM（Assets/Audio/bgm.ogg——EnterRun 起播含装载期，单槽交叉淡出
    // = 重开不叠曲）
    private const string kBgm = "7e57400000000001";
    private const string kMenuScene = "MainMenu";
    private const string kBattleScene = "Grass";

    internal enum State { Menu, Loading, Run, Paused, Results, Settings }

    // ---- 流程态（静态：UI 事件经 GameMain 单点订阅路由，不持实例）----
    internal static State St = State.Menu;
    private static State settingsFrom = State.Menu; // 设置屏返回目标（入口双源）
    private static bool prevPause, prevConfirm;     // 边沿（按住只触发一次）

    // 跨场种子守卫（批⑨ D1）：MainMenu 重装的新种子标记 dup、首 Update 自毁——
    // Booted 静态随域重建复位，热重载经 StateBag 传递（OnHotReloadOut/In）
    private static bool Booted;
    private bool dup;

    // 结算数据（ShowResults 落板 + 热重载重灌）
    private static string resTitle = "", resScore = "", resTime = "", resKills = "",
                         resBest = "";

    protected override void Awake()
    {
        if (Booted) { dup = true; return; } // 重装副本：静默等首帧自毁
        Booted = true;
        // 跨场幸存（批⑦ D1 根位式：位只标根 O(1)，清场判祖先链）
        LemonBehaviour.DontDestroyOnLoad(gameObject);
    }

    protected override void Start()
    {
        if (dup) return;
        // 进 Play 即菜单。文档 = 通道 B 现载（origin=CSharp；批⑨ 起流程四屏不再
        // 走场景 UIDocument 声明——origin=Scene 换场即卸，见类头注）。隐藏屏
        // Show+Hide 一次性预装载：通道 B 只在 Show 现载，此后隐藏态可写
        //（RefreshSettingsUi 首帧即写 settings DOM——③d-2 先例语义保持）
        Time.Scale = 0f;
        St = State.Menu;
        UI.Show(GameMain.MainDoc);
        UI.Show(GameMain.PauseDoc);     UI.Hide(GameMain.PauseDoc);
        UI.Show(GameMain.SettingsDoc);  UI.Hide(GameMain.SettingsDoc);
        UI.Show(GameMain.ResultsDoc);   UI.Hide(GameMain.ResultsDoc);
        UI.Apply();
        LoadSettings();
    }

    protected override void Update()
    {
        if (dup) { gameObject.Destroy(); return; } // 重装副本首帧自毁（Awake 期不动世界）
        bool pauseEdge = Input.Pause && !prevPause;       // Esc/P（bit6）
        bool confirmEdge = Input.Confirm && !prevConfirm; // R（bit5「确认/重开」）
        switch (St) {
        case State.Menu:
            if (confirmEdge) EnterRun(); // 键盘路径（批⑨ D3：入 InputState 可回放）
            break;
        case State.Loading:
            break;                       // 加载屏期间无输入消费
        case State.Run:
            if (pauseEdge) SetPaused(true);
            break;
        case State.Paused:
            if (pauseEdge) SetPaused(false);
            break;
        case State.Results:
            if (confirmEdge) EnterRun();       // R = 重开
            else if (pauseEdge) ReturnToMenu(); // Esc = 回菜单
            break;
        }
        prevPause = Input.Pause;
        prevConfirm = Input.Confirm;
    }

    // ---- 流程原语（PlayerCombat 死亡分叉 / UI 事件调用）----

    /// <summary>开始/重开一局：隐入口屏 + 加载屏 + 异步装载（激活帧引擎清场接管
    /// RunSweeper 职责；sceneLoaded(Grass) → 重置 Run → Run 态）。Loading 中重入
    /// 忽略。</summary>
    internal static void EnterRun()
    {
        if (St == State.Loading) return;
        St = State.Loading;
        Time.Scale = 0f; // 装载期冻结（sceneLoaded 置 1）
        UI.Hide(GameMain.MainDoc);
        UI.Hide(GameMain.ResultsDoc);
        UI.Apply();
        Audio.Paused = false; // 暂停残留防御（换场亦强制清——b6b D6；先归位再开局）
        Audio.PlayBgm(kBgm, 0.55f); // M6c 批④：开战 BGM（EnterRun 起播含装载期）
        GameMain.HideCardsDoc(); // 在途动态屏一并收
        LoadingScreen.Begin(kBattleScene); // 批⑧ 样例：LoadSceneAsync + 加载屏 + DDOL 驱动
    }

    /// <summary>sceneLoaded 路由（GameMain.Configure 订阅；同步/异步两门面共用）。
    /// 未知场景（测试/工具场）= 隐菜单——流程壳不越界认领。初始 entryScene 装载
    /// 不推事件（oldHandle==0）——Start 已覆盖菜单首显。</summary>
    internal static void OnSceneLoaded(Scene scene, LoadSceneMode mode)
    {
        if (scene.name == kMenuScene) {
            St = State.Menu;
            Time.Scale = 0f;
            UI.Show(GameMain.MainDoc); // 重装副本种子已自毁——DDOL 壳独占流程
            UI.Apply();
        } else if (scene.name == kBattleScene) {
            GameMain.Run.Time = 0f;    // 一局共享态随装载归零（原 Spawning 段迁移）
            GameMain.Run.Kills = 0;
            GameMain.Run.Dead = false;
            GameMain.Run.ReviveUsed = false;
            Time.Scale = 1f;
            St = State.Run; // 战斗场自含 Player/Director（批⑨ D2）——装载即开局
        } else {
            UI.Hide(GameMain.MainDoc); // 未知场不认领——菜单只属于菜单场
            UI.Apply();
        }
    }

    /// <summary>结算屏（死亡策略的第二半——何时调由游戏侧决定）。</summary>
    internal static void ShowResults(string title, string score, string time,
                                     string kills, string best)
    {
        resTitle = title; resScore = score; resTime = time;
        resKills = kills; resBest = best;
        Time.Scale = 0f;
        UI.Show(GameMain.ResultsDoc);
        UI.SetText(GameMain.ResultsDoc, "results-title", title);
        UI.SetText(GameMain.ResultsDoc, "res-score", score);
        UI.SetText(GameMain.ResultsDoc, "res-time", time);
        UI.SetText(GameMain.ResultsDoc, "res-kills", kills);
        UI.SetText(GameMain.ResultsDoc, "res-best", best);
        UI.Apply();
        St = State.Results;
    }

    /// <summary>回主菜单：同步换场（批⑨ D5：小场景无屏闪）+ 静场 + 在途动态屏
    /// 收屏 + HUD 隐藏（code-mounted 跨场幸存——显式收）。激活帧
    /// sceneLoaded(MainMenu) 显菜单。</summary>
    internal static void ReturnToMenu()
    {
        // review 2026-10-02 #1：暂停中回菜单必须解挂——Audio.Paused 残留会使下局
        // PlayBgm 新声部生而挂起（整局静音）；StopBgm 只停曲不清全局暂停态
        Audio.Paused = false;
        Audio.StopBgm(0.5f); // M6c 批④：回菜单静场（0.5s 淡出）
        Time.Scale = 0f;
        UI.Hide(GameMain.PauseDoc);
        UI.Hide(GameMain.SettingsDoc);
        UI.Hide(GameMain.ResultsDoc);
        UI.Hide(GameMain.HudDoc); // 批⑨：HUD origin=CSharp 跨场幸存——显式收屏
        GameMain.HideCardsDoc();
        UI.Apply();
        SceneManager.LoadScene(kMenuScene); // 同步门面：下帧激活 → OnSceneLoaded
    }

    /// <summary>暂停对（Run↔Paused；卡片冻结期 Input 已被模态让出，天然不响应）。
    /// 批④：Audio.Paused 先挂起再冻结（恢复反向）——D5 显式语义，Ui 组免疫
    ///（暂停屏按钮音仍可响）。</summary>
    internal static void SetPaused(bool on)
    {
        Audio.Paused = on;
        if (on) {
            Time.Scale = 0f;
            UI.Show(GameMain.PauseDoc);
            UI.Apply();
            St = State.Paused;
        } else {
            UI.Hide(GameMain.PauseDoc);
            UI.Apply();
            Time.Scale = 1f;
            St = State.Run;
        }
    }

    // ---- UI 事件路由（GameMain.OnUiEvent 分发；Click 通道）----

    internal static void HandleUiEvent(Lemon.UiEvent e)
    {
        if (e.DocStr == GameMain.MainDoc) {
            if (e.EvStr == "start") EnterRun();
            else if (e.EvStr == "settings") OpenSettings(State.Menu);
        } else if (e.DocStr == GameMain.PauseDoc) {
            if (e.EvStr == "resume") SetPaused(false);
            else if (e.EvStr == "settings") OpenSettings(State.Paused);
            else if (e.EvStr == "tomenu") ReturnToMenu();
        } else if (e.DocStr == GameMain.SettingsDoc) {
            if (e.EvStr == "toggle-fxtext") GameMain.Settings.FxText = !GameMain.Settings.FxText;
            else if (e.EvStr == "toggle-fxbar") GameMain.Settings.FxBar = !GameMain.Settings.FxBar;
            else if (e.EvStr == "back") { CloseSettings(); return; }
            else return;
            SaveSettings(); // 开关翻转即持久化 + 刷新标签
        } else if (e.DocStr == GameMain.ResultsDoc) {
            if (e.EvStr == "restart") EnterRun();
            else if (e.EvStr == "tomenu") ReturnToMenu();
        }
    }

    /// <summary>热重载重放（DocumentReloaded——shown 态重放，隐藏态不重放防凭空
    /// 亮屏；③d-1 卡片同款契约）。设置屏重载 = 标签重灌（SaveSettings 顺带）。</summary>
    internal static void OnDocReloaded(string doc)
    {
        if (doc == GameMain.MainDoc) {
            if (St == State.Menu) { UI.Show(GameMain.MainDoc); UI.Apply(); }
        } else if (doc == GameMain.PauseDoc) {
            if (St == State.Paused) { UI.Show(GameMain.PauseDoc); UI.Apply(); }
        } else if (doc == GameMain.SettingsDoc) {
            if (St == State.Settings) { UI.Show(GameMain.SettingsDoc); UI.Apply(); }
            SaveSettings(); // DOM 重建——开关标签重灌
        } else if (doc == GameMain.ResultsDoc && St == State.Results) {
            ShowResults(resTitle, resScore, resTime, resKills, resBest);
        }
    }

    // ---- 设置（批③d-2 D3：两真实开关，Settings 档版本化 KV）----

    private static void OpenSettings(State from)
    {
        settingsFrom = from;
        UI.Show(GameMain.SettingsDoc);
        UI.Apply();
        St = State.Settings;
    }

    private static void CloseSettings()
    {
        FlushVolIfDirty(); // review 2026-10-02 #31：音量落盘收口到关屏（拖动去抖）
        UI.Hide(GameMain.SettingsDoc);
        UI.Apply();
        St = settingsFrom; // 底层屏（菜单实底/暂停 scrim）未动——回即见
    }

    private static void LoadSettings()
    {
        GameMain.Settings.FxText = Save.GetString("fx.text", Save.Chan.Settings) != "0";
        GameMain.Settings.FxBar = Save.GetString("fx.bar", Save.Chan.Settings) != "0";
        // M6c 批④：音量四路（vol.* int 0..100 字串，缺省 80）→ 引擎应用 + 回显
        GameMain.Settings.MasterVol = VolOf("vol.master");
        GameMain.Settings.BgmVol = VolOf("vol.bgm");
        GameMain.Settings.SfxVol = VolOf("vol.sfx");
        GameMain.Settings.UiVol = VolOf("vol.ui");
        ApplyVolumes();
        SaveSettings(); // 首开建档（version=1）+ 标签/滑条刷新
    }

    // ---- M6c 批④：音量四路（设置屏滑条；Settings 档 vol.* 持久化）----

    private static float VolOf(string key)
        => int.TryParse(Save.GetString(key, Save.Chan.Settings), out int v)
               && v >= 0 && v <= 100 ? v / 100f : 0.8f;

    /// 引擎应用（Master + 三组；staging 写当帧提交，装载前后重复调 = 幂等终态）。
    /// （Settings 为静态类——成员全限定访问，无实例别名。）
    private static void ApplyVolumes()
    {
        Audio.MasterVolume = GameMain.Settings.MasterVol;
        Audio.SetGroupVolume(AudioGroup.Bgm, GameMain.Settings.BgmVol);
        Audio.SetGroupVolume(AudioGroup.Sfx, GameMain.Settings.SfxVol);
        Audio.SetGroupVolume(AudioGroup.Ui, GameMain.Settings.UiVol);
    }

    /// 滑条值落定（key = 滑条 id；payload = "%f" 值串 0..100）。同值早退——
    /// SetAttr 回显自回环防线（RmlUi 值未变不派发，回显等值也免二次落盘）。
    internal static void OnVolumeChange(string key, string payload)
    {
        if (!float.TryParse(payload, System.Globalization.NumberStyles.Float, IC,
                            out float v)) return;
        v = Math.Clamp(v / 100f, 0f, 1f);
        if (key == "vol-master") { if (Math.Abs(v - GameMain.Settings.MasterVol) < 0.001f) return; GameMain.Settings.MasterVol = v; }
        else if (key == "vol-bgm") { if (Math.Abs(v - GameMain.Settings.BgmVol) < 0.001f) return; GameMain.Settings.BgmVol = v; }
        else if (key == "vol-sfx") { if (Math.Abs(v - GameMain.Settings.SfxVol) < 0.001f) return; GameMain.Settings.SfxVol = v; }
        else if (key == "vol-ui") { if (Math.Abs(v - GameMain.Settings.UiVol) < 0.001f) return; GameMain.Settings.UiVol = v; }
        else return;
        ApplyVolumes();
        // review 2026-10-02 #31：拖动每步只应用 + 回显，落盘去抖到关屏——此前每个
        // 步进 Change 都 Save.Flush 全档（主线程同步 IO 一次满拖放大 ~20×）
        RefreshSettingsUi();
        volDirty = true;
    }

    private static string Pct(float v) => ((int)Math.Round(v * 100f)).ToString(IC);
    private static readonly System.Globalization.CultureInfo IC =
        System.Globalization.CultureInfo.InvariantCulture;

    // #31 去抖记账：音量拖动中置位，关屏（CloseSettings/ReturnToMenu）统一落盘
    private static bool volDirty = false;

    private static void FlushVolIfDirty()
    {
        if (!volDirty) return;
        volDirty = false;
        PersistSettings();
    }

    /// 落盘（含首开建档 version=1）。开关翻转/首开建档即时走全量；滑条拖动走
    /// RefreshSettingsUi 回显 + volDirty，停拖关屏才 Flush（#31）。
    private static void PersistSettings()
    {
        Save.SetString("version", "1", Save.Chan.Settings);
        Save.SetString("fx.text", GameMain.Settings.FxText ? "1" : "0", Save.Chan.Settings);
        Save.SetString("fx.bar", GameMain.Settings.FxBar ? "1" : "0", Save.Chan.Settings);
        Save.SetString("vol.master", Pct(GameMain.Settings.MasterVol), Save.Chan.Settings);
        Save.SetString("vol.bgm", Pct(GameMain.Settings.BgmVol), Save.Chan.Settings);
        Save.SetString("vol.sfx", Pct(GameMain.Settings.SfxVol), Save.Chan.Settings);
        Save.SetString("vol.ui", Pct(GameMain.Settings.UiVol), Save.Chan.Settings);
        Save.Flush();
    }

    /// 滑条 value 属性 + 开关/百分数回显（拖动路径即时调用，零磁盘 IO）。
    private static void RefreshSettingsUi()
    {
        UI.SetText(GameMain.SettingsDoc, "btn-fxtext", GameMain.Settings.FxText ? "开" : "关");
        UI.SetText(GameMain.SettingsDoc, "btn-fxbar", GameMain.Settings.FxBar ? "开" : "关");
        // 滑条 value 属性 + 右侧百分数（隐藏态可写——装载文档 DOM 常在，③d-2 先例）
        UI.SetAttr(GameMain.SettingsDoc, "vol-master", "value", Pct(GameMain.Settings.MasterVol));
        UI.SetAttr(GameMain.SettingsDoc, "vol-bgm", "value", Pct(GameMain.Settings.BgmVol));
        UI.SetAttr(GameMain.SettingsDoc, "vol-sfx", "value", Pct(GameMain.Settings.SfxVol));
        UI.SetAttr(GameMain.SettingsDoc, "vol-ui", "value", Pct(GameMain.Settings.UiVol));
        UI.SetText(GameMain.SettingsDoc, "vol-master-val", Pct(GameMain.Settings.MasterVol));
        UI.SetText(GameMain.SettingsDoc, "vol-bgm-val", Pct(GameMain.Settings.BgmVol));
        UI.SetText(GameMain.SettingsDoc, "vol-sfx-val", Pct(GameMain.Settings.SfxVol));
        UI.SetText(GameMain.SettingsDoc, "vol-ui-val", Pct(GameMain.Settings.UiVol));
        UI.Apply();
    }

    private static void SaveSettings()
    {
        PersistSettings();
        RefreshSettingsUi();
    }

    // 热重载状态迁移（流程态 + 设置 + 跨场种子——静态随域重建必须经包走）
    protected override void OnHotReloadOut(Lemon.StateBag bag)
    {
        bag.Set("st", (int)St);
        bag.Set("from", (int)settingsFrom);
        bag.Set("fxtext", GameMain.Settings.FxText);
        bag.Set("fxbar", GameMain.Settings.FxBar);
        bag.Set("vmaster", GameMain.Settings.MasterVol); // 批④：音量四路随包
        bag.Set("vbgm", GameMain.Settings.BgmVol);
        bag.Set("vsfx", GameMain.Settings.SfxVol);
        bag.Set("vui", GameMain.Settings.UiVol);
        bag.Set("booted", Booted); // 批⑨：种子守卫随包（域重建静态复位后恢复）
    }

    protected override void OnHotReloadIn(Lemon.StateBag bag)
    {
        if (bag.TryGet("st", out int st)) St = (State)st;
        if (bag.TryGet("from", out int from)) settingsFrom = (State)from;
        if (bag.TryGet("fxtext", out bool ft)) GameMain.Settings.FxText = ft;
        if (bag.TryGet("fxbar", out bool fb)) GameMain.Settings.FxBar = fb;
        if (bag.TryGet("vmaster", out float vm)) GameMain.Settings.MasterVol = vm;
        if (bag.TryGet("vbgm", out float vb)) GameMain.Settings.BgmVol = vb;
        if (bag.TryGet("vsfx", out float vs)) GameMain.Settings.SfxVol = vs;
        if (bag.TryGet("vui", out float vu)) GameMain.Settings.UiVol = vu;
        if (bag.TryGet("booted", out bool bo)) Booted = bo;
        ApplyVolumes(); // 引擎侧随域重建归默认——热进即回设
    }
}
