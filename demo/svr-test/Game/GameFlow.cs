using System;
using Lemon;

/// <summary>流程状态机（M7c 批⑨ 多场景形态；前身 = M6b 批③d-2 档1 单场景版——
/// RunSweeper 清场补丁退役，引擎换场清场（批⑦ D2「除 DDOL 系外全清」）接管）。
/// 跨场壳（批⑨ D1）= 本实体 DDOL（Awake 自标 + MainMenu 重装新种子 Booted 守卫
/// 自毁）+ 四屏流程文档 code-mount（UI.Show 通道 B 现载 = origin=CSharp 跨场
/// 幸存——换场 sweep 只卸 origin=Scene，场景声明式 UIDocument 不跨场）。
/// 换场门面（批⑨ D5）：进战斗 = LoadingScreen.Begin（LoadSceneAsync + 加载屏，
/// 批⑧ 样例消费）；回菜单 = 同步 LoadScene("MainMenu")。
/// 键盘位（批⑨ D3，批⑦ 敞口② 修法）：菜单 R=草地 / 空格=火山；结算 R=重开 /
/// Esc=回菜单——语义位入 InputState = 可回放；鼠标点击路径为人用（非回放口径）。
/// 死亡策略归游戏侧（PlayerBehaviour.Die 首死复活对话（RtUi）/二死结算）不变。
/// 战斗场自含 Player/Director（批⑨ D2）——装载即开局，无 spawn 握手。</summary>
public sealed class GameFlow : LemonBehaviour
{
    // M6c 竖切批：战斗 BGM（Assets/Audio/bgm.mp3 = yami watery_cave；Bgm 组单槽循环）
    private const string kBgm = "6a6d100000000001";
    private const string kMenuScene = "MainMenu";

    internal enum State { Menu, Loading, Run, Paused, Results, Settings }

    // ---- 流程态（静态：UI 事件经 GameMain 单点订阅路由，不持实例）----
    internal static State St = State.Menu;
    private static State settingsFrom = State.Menu; // 设置屏返回目标（入口双源）
    private static bool prevPause, prevConfirm, prevAttack; // 边沿（按住只触发一次）
    private static string battleScene = "Grass";    // 当前战斗场（结算重开用）

    // 跨场种子守卫（D1）：MainMenu 重装的新种子标记 dup、首 Update 自毁。
    // 跨局复位：静态随域存活（Stop→Play 不清）——经 Events.PlayResetHook 每局清
    // （否则二次 Play 新种子被旧局守卫误杀 = 全灭，批⑨ 真人走查实报）；热重载
    // 仍走 StateBag（OnHotReloadOut/In）
    private static bool Booted;
    private bool dup;

    static GameFlow() => Lemon.Events.PlayResetHook += () => Booted = false;

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
        battleScene = "Grass";
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
        bool pauseEdge = Input.Pause && !prevPause;         // Esc/P（bit6）
        bool confirmEdge = Input.Confirm && !prevConfirm;   // R（bit5「确认/重开」）
        bool attackEdge = Input.Attack && !prevAttack;      // 空格（bit4；菜单态无冲刺语义）
        switch (St) {
        case State.Menu:
            if (confirmEdge) EnterRun("Grass");       // 键盘路径（D3：入 InputState 可回放）
            else if (attackEdge) EnterRun("Volcano");
            break;
        case State.Loading:
            break;                                    // 加载屏期间无输入消费
        case State.Run:
            if (pauseEdge) SetPaused(true);
            break;
        case State.Paused:
            if (pauseEdge) SetPaused(false);
            break;
        case State.Results:
            if (confirmEdge) EnterRun(battleScene);   // R = 重开当前战斗场
            else if (pauseEdge) ReturnToMenu();       // Esc = 回菜单
            break;
        }
        prevPause = Input.Pause;
        prevConfirm = Input.Confirm;
        prevAttack = Input.Attack;
    }

    // ---- 换场与场景事件（订阅面归 GameMain.Configure = UI.Events 同生命周期，
    //      热重载随域重建重订）----

    /// <summary>开始/重开一局：隐入口屏 + 加载屏 + 异步装载（激活帧引擎清场接管
    /// RunSweeper 职责；sceneLoaded(battle) → Run）。Loading 中重入忽略。</summary>
    internal static void EnterRun(string scene)
    {
        if (St == State.Loading) return;
        battleScene = scene;
        St = State.Loading;
        Time.Scale = 0f; // 装载期冻结（sceneLoaded 置 1）
        UI.Hide(GameMain.MainDoc);
        UI.Hide(GameMain.ResultsDoc);
        UI.Apply();
        Audio.Paused = false; // 暂停残留防御（换场亦强制清——b6b D6；先归位再开局）
        Audio.PlayBgm(kBgm, 0.55f); // 开战 BGM（单槽顶停旧曲；菜单静场）
        // review F1：场景不可解析（改名/删档）= op 无效且 Begin 未挂屏——不回滚
        // 则 St 恒 Loading、入口屏已隐、BGM 在播 = 软锁；回菜单态 + 静场自愈
        if (!LoadingScreen.Begin(scene).isValid) {
            Audio.StopBgm(0.5f);
            St = State.Menu;
            UI.Show(GameMain.MainDoc);
            UI.Apply();
        }
    }

    /// <summary>sceneLoaded 路由（GameMain.Configure 订阅；同步/异步两门面共用）。
    /// 初始 entryScene 装载不推事件（oldHandle==0）——Start 已覆盖菜单首显。</summary>
    internal static void OnSceneLoaded(Scene scene, LoadSceneMode mode)
    {
        if (scene.name == kMenuScene) {
            St = State.Menu;
            Time.Scale = 0f;
            UI.Show(GameMain.MainDoc); // 重装副本种子已自毁——DDOL 壳独占流程
            UI.Apply();
        } else if (scene.name == "Grass" || scene.name == "Volcano") {
            Time.Scale = 1f;
            St = State.Run; // 战斗场自含 Player/Director（D2）——装载即开局
        } else {
            UI.Hide(GameMain.MainDoc); // 未知场不认领——菜单只属于菜单场（与模板同款）
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

    /// <summary>回主菜单：同步换场（D5：小场景无屏闪）+ 静场 + RtUi HUD 行收屏
    ///（本游戏战斗 HUD 走 RtUi 兼容层——ADR-014 D5；玩家已销毁无人再写，残留行
    /// 在此清；换场 Fx 整场清为双保险）。激活帧 sceneLoaded(MainMenu) 显菜单。</summary>
    internal static void ReturnToMenu()
    {
        // review 2026-10-02 #2：暂停中回菜单必须解挂——Audio.Paused 残留会使下局
        // PlayBgm 新声部生而挂起（整局静音）；StopBgm 只停曲不清全局暂停态
        Audio.Paused = false;
        Audio.StopBgm(0.5f); // M6c 批④：回菜单静场（0.5s 淡出）
        Time.Scale = 0f;
        UI.Hide(GameMain.PauseDoc);
        UI.Hide(GameMain.SettingsDoc);
        UI.Hide(GameMain.ResultsDoc);
        UI.Apply();
        Ui.Clear("hp"); Ui.Clear("xp"); Ui.Clear("time"); Ui.Clear("kills");
        Ui.Clear("best"); Ui.Clear("wave"); Ui.Clear("over");
        SceneManager.LoadScene(kMenuScene); // 同步门面：下帧激活 → OnSceneLoaded
    }

    /// <summary>暂停对（Run↔Paused；死亡冻结期无输入消费，天然不响应）。
    /// M6c 批② D5：显式音频暂停（循环/BGM 挂起、UI 组免疫）——引擎不自动映射
    /// Time.Scale，菜单/选卡/装载等流程冻结不误停 BGM。</summary>
    internal static void SetPaused(bool on)
    {
        Audio.Paused = on; // 先挂起再冻结（恢复反向：先解冻再续响）
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
            if (e.EvStr == "start") EnterRun("Grass");
            else if (e.EvStr == "start-volcano") EnterRun("Volcano");
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
            if (e.EvStr == "restart") EnterRun(battleScene);
            else if (e.EvStr == "tomenu") ReturnToMenu();
        }
    }

    /// <summary>热重载重放（DocumentReloaded——shown 态重放，隐藏态不重放）。</summary>
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

    // ---- 设置（批③d-2 D3：两真实开关 + M6c 批④ 音量四路；Settings 档版本化 KV）----

    private static void OpenSettings(State from)
    {
        settingsFrom = from;
        UI.Show(GameMain.SettingsDoc);
        UI.Apply();
        St = State.Settings;
    }

    private static void CloseSettings()
    {
        FlushVolIfDirty(); // review 2026-10-02 #32：音量落盘收口到关屏（拖动去抖）
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
        // review 2026-10-02 #32：拖动每步只应用 + 回显，落盘去抖到关屏——此前每个
        // 步进 Change 都 Save.Flush 全档 + 10 条回显 op（拖动路径同步 IO 未去抖）
        RefreshSettingsUi();
        volDirty = true;
    }

    private static string Pct(float v) => ((int)Math.Round(v * 100f)).ToString(IC);
    private static readonly System.Globalization.CultureInfo IC =
        System.Globalization.CultureInfo.InvariantCulture;

    // #32 去抖记账：音量拖动中置位，关屏统一落盘
    private static bool volDirty = false;

    private static void FlushVolIfDirty()
    {
        if (!volDirty) return;
        volDirty = false;
        PersistSettings();
    }

    /// 落盘（含首开建档 version=1）。开关翻转/首开建档即时走全量；滑条拖动走
    /// RefreshSettingsUi 回显 + volDirty，停拖关屏才 Flush（#32）。
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
        // 滑条 value 属性 + 右侧百分数（隐藏态可写——通道 B 装载文档 DOM 常在，③d-2 先例）
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

    // 热重载状态迁移（流程态 + 设置 + 跨场种子/战斗场——静态随域重建必须经包走）
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
        bag.Set("battle", battleScene == "Volcano" ? 1 : 0); // StateBag 只收值类型
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
        if (bag.TryGet("battle", out int bs)) battleScene = bs == 1 ? "Volcano" : "Grass";
        ApplyVolumes(); // 引擎侧随域重建归默认——热进即回设
    }
}
