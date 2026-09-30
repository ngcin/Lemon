using System;
using Lemon;
using Lemon.Interop;

/// <summary>流程状态机（M6b 批③d-2 档1：单场景零引擎改动；svr-test 版——拷自
/// vs-survivor 模板同批，适配点：prefab GUID/清场 tag 集/RtUi HUD 行清屏/无
/// GameMain.Run 静态）。只提供流程原语：EnterRun（清场 + 重挂双 prefab）/
/// ShowResults / ReturnToMenu / SetPaused——**死亡策略归游戏侧**（本游戏 =
/// PlayerBehaviour.Die 首死复活对话（RtUi）/二死结算，见其分叉）。
/// 重开 = C# 自律清场：RunSweeper 按 tag 扫场销毁 run 实体（SceneOps 命令
/// 次帧首应用）→ Spawning 握手（SweepObserved）后重挂 Player/Director prefab。</summary>
public sealed class GameFlow : LemonBehaviour
{
    // svr-test 资产 GUID（批③d-2 新落；引用锚点，勿改）
    private const string kPlayerPrefab = "7e57410000000001";
    private const string kDirectorPrefab = "7e57410000000002";

    internal enum State { Menu, Spawning, Run, Paused, Results, Settings }

    // ---- 流程态（静态：UI 事件经 GameMain 单点订阅路由，不持实例）----
    internal static State St = State.Menu;
    private static State settingsFrom = State.Menu; // 设置屏返回目标（入口双源）
    private static bool prevPause;                  // Esc 边沿（按住只切一次）
    // M6c 竖切批：战斗 BGM（Assets/Audio/bgm.mp3 = yami watery_cave；Bgm 组单槽循环）
    private const string kBgm = "6a6d100000000001";

    // 清场握手：EnterRun/ReturnToMenu 置 Armed → RunSweeper 批扫置 Observed →
    // GameFlow 观察后清对、开局（命令入队与 spawn 间恒有 Essential 提交拍）
    internal static bool SweepArmed, SweepObserved;
    // 结算数据（ShowResults 落板 + 热重载重灌）
    private static string resTitle = "", resScore = "", resTime = "", resKills = "",
                         resBest = "";

    protected override void Start()
    {
        // 进 Play 即菜单。显式 re-Show：装载通道 Show 序随场景实体迭代序
        //（EnTT 逆序——先建者后显 = 置顶），不重排则流程屏压序漂移；显隐归本类
        Time.Scale = 0f;
        St = State.Menu;
        UI.Show(GameMain.MainDoc);
        UI.Apply();
        LoadSettings();
    }

    protected override void Update()
    {
        bool pauseEdge = Input.Pause && !prevPause; // 边沿语义（按住 Esc 不连切）
        switch (St) {
        case State.Menu:
            if (SweepObserved) { SweepArmed = false; SweepObserved = false; } // 回菜单清场收尾
            break;
        case State.Spawning:
            if (!SweepObserved) break;                 // 清场批未过——等握手
            SweepArmed = false;
            SweepObserved = false;
            Instantiate.Prefab(kPlayerPrefab, new Vec2(0f, 0f));
            Instantiate.Prefab(kDirectorPrefab, new Vec2(0f, 0f));
            Time.Scale = 1f;
            St = State.Run;                            // 入口屏已在 EnterRun 即隐
            break;
        case State.Run:
            if (pauseEdge) SetPaused(true);            // Esc/P（bit6，批③d-2 D4）
            break;
        case State.Paused:
            if (pauseEdge) SetPaused(false);
            break;
        }
        prevPause = Input.Pause;
    }

    // ---- 流程原语（PlayerBehaviour 死亡分叉 / UI 事件调用）----

    /// <summary>开始/重开一局：清场 →（握手后）重挂双 prefab → Run。入口屏即隐
    ///（菜单/结算——点击即走，不留残屏盖在新局上）；Spawning 中重入忽略。</summary>
    internal static void EnterRun()
    {
        if (St == State.Spawning) return;
        SweepArmed = true;
        SweepObserved = false;
        St = State.Spawning;
        Time.Scale = 0f; // 清场期冻结（无玩家在场防导演空转）
        UI.Hide(GameMain.MainDoc);
        UI.Hide(GameMain.ResultsDoc);
        UI.Apply();
        Audio.PlayBgm(kBgm, 0.55f); // M6c 竖切批：开战 BGM（单槽顶停旧曲；清场冻结不挂起——BGM 循环声部级暂停待批②）
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

    /// <summary>回主菜单：清场 + 实底菜单 + RtUi HUD 行收屏（本游戏战斗 HUD 走
    /// RtUi 兼容层——ADR-014 D5；玩家已销毁无人再写，残留行在此清）。</summary>
    internal static void ReturnToMenu()
    {
        SweepArmed = true;
        SweepObserved = false;
        Time.Scale = 0f;
        UI.Hide(GameMain.PauseDoc);
        UI.Hide(GameMain.SettingsDoc);
        UI.Hide(GameMain.ResultsDoc);
        UI.Show(GameMain.MainDoc);
        UI.Apply();
        Ui.Clear("hp"); Ui.Clear("xp"); Ui.Clear("time"); Ui.Clear("kills");
        Ui.Clear("best"); Ui.Clear("wave"); Ui.Clear("over");
        St = State.Menu;
    }

    /// <summary>暂停对（Run↔Paused；死亡冻结期无输入消费，天然不响应）。
    /// M6c 批② D5：显式音频暂停（循环/BGM 挂起、UI 组免疫）——引擎不自动映射
    /// Time.Scale，菜单/选卡/清场等流程冻结不误停 BGM。</summary>
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
        UI.Hide(GameMain.SettingsDoc);
        UI.Apply();
        St = settingsFrom; // 底层屏（菜单实底/暂停 scrim）未动——回即见
    }

    private static void LoadSettings()
    {
        GameMain.Settings.FxText = Save.GetString("fx.text", Save.Chan.Settings) != "0";
        GameMain.Settings.FxBar = Save.GetString("fx.bar", Save.Chan.Settings) != "0";
        SaveSettings(); // 首开建档（version=1）+ 标签刷新
    }

    private static void SaveSettings()
    {
        Save.SetString("version", "1", Save.Chan.Settings);
        Save.SetString("fx.text", GameMain.Settings.FxText ? "1" : "0", Save.Chan.Settings);
        Save.SetString("fx.bar", GameMain.Settings.FxBar ? "1" : "0", Save.Chan.Settings);
        Save.Flush();
        UI.SetText(GameMain.SettingsDoc, "btn-fxtext", GameMain.Settings.FxText ? "开" : "关");
        UI.SetText(GameMain.SettingsDoc, "btn-fxbar", GameMain.Settings.FxBar ? "开" : "关");
        UI.Apply();
    }

    // 热重载状态迁移（流程态 + 设置——静态随域重建必须经包走）
    protected override void OnHotReloadOut(Lemon.StateBag bag)
    {
        bag.Set("st", (int)St);
        bag.Set("from", (int)settingsFrom);
        bag.Set("fxtext", GameMain.Settings.FxText);
        bag.Set("fxbar", GameMain.Settings.FxBar);
    }

    protected override void OnHotReloadIn(Lemon.StateBag bag)
    {
        if (bag.TryGet("st", out int st)) St = (State)st;
        if (bag.TryGet("from", out int from)) settingsFrom = (State)from;
        if (bag.TryGet("fxtext", out bool ft)) GameMain.Settings.FxText = ft;
        if (bag.TryGet("fxbar", out bool fb)) GameMain.Settings.FxBar = fb;
    }
}

/// <summary>清场批量系统（档②；svr-test tag 集——比模板多 FastMob/FlySword/
/// ScatterBullet 弹种与 "spawned"（Instantiate.Spawn 的残影/盟友等无 prefab 态））。
/// 常驻注册 + 门控早退：非 armed 拍 C# 零工作。</summary>
public sealed class RunSweeper : IForEachSystem
{
    private static readonly string[] kRunTags = {
        "Player", "Director", "Mob", "BossMob", "FastMob", "Gem", "Bullet",
        "PierceBullet", "ScatterBullet", "FlySword", "Blade", "spawned",
    };

    public string Name => "RunSweeper";
    public Query Query => Query.With<Meta>();

    public unsafe void ForEach(ref readonly Chunk chunk)
    {
        if (!GameFlow.SweepArmed) return;
        var meta = chunk.Span<Meta>();
        for (int i = 0; i < chunk.Length; ++i) {
            if (TagIs(ref meta[i], kRunTags)) SceneOps.Destroy(chunk.Entities[i]);
        }
        GameFlow.SweepObserved = true; // 握手位（GameFlow 观察后清对——本批全块扫完）
    }

    private static unsafe bool TagIs(ref Meta m, string[] tags)
    {
        foreach (string t in tags) {
            fixed (byte* p = m.Tag) {
                bool same = true;
                for (int i = 0; i < t.Length && i < 24; ++i)
                    if (p[i] != (byte)t[i]) { same = false; break; }
                if (same && (t.Length >= 24 || p[t.Length] == 0)) return true;
            }
        }
        return false;
    }
}
