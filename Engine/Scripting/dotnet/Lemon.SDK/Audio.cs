// Lemon.SDK — 音频播放门面（M6c 批② 正式化，ADR-015；引擎侧 AudioChannel 命令表
// + AudioSystem 提交） ------------------------------------------------------------------------------
// 语义：clip 引用 = 资产 GUID（16 位 hex，prefab 同款口径）；Play/PlayAt 返回逻辑
// voiceId（单调不回收；0 = 失败：未装载/旧宿主）。命令当帧 staging、AudioSystem
// 统一提交——BGM 单槽在引擎侧 World.AudioChannel（热重载换域不丢，竖切 C# 静态
// _bgm 孤儿声部已根除）。暂停 = 显式 Audio.Paused（D5：引擎不自动映射 TimeScale
//——菜单/选卡/清场等流程冻结不误停 BGM）。音频状态不入 StateHash——金回放零重录。
using System;

namespace Lemon;

/// <summary>混音组（ADR-015 D3 固定三组；暂停语义：BGM/循环挂起、Ui 组永不挂起）。</summary>
public enum AudioGroup : int
{
    Bgm = 0,
    Sfx = 1,
    Ui = 2,
}

public static class Audio
{
    /// <summary>按资产 GUID 播放（非空间）。返回逻辑 voiceId（单调不回收；0 = 未装载/
    /// 坏 GUID/旧宿主——review 2026-10-02 #21 勘误：池满/同 clip 节流等提交期引擎拒绝
    /// 返回**非零**但声部即刻失效，勿以 id==0 判池满，存活判定跨帧用 Alive 语义/回读）。
    /// loop = true 时循环区间 = clip 烤制期 loop 点（meta importer）。</summary>
    public static unsafe uint Play(string clipGuidHex, float volume = 1f,
                                   AudioGroup group = AudioGroup.Sfx, float pan = 0f,
                                   bool loop = false)
    {
        // 防御解析（review 2026-09-30）：Convert.ToUInt64 对非 hex 字符抛异常——
        // 常量手误不该崩游戏；SDK 其他入口同款 TryParse 口径，坏串 = 0 静默降级
        if (Native.Api.AudioPlay == null || clipGuidHex == null)
            return 0;
        if (!ulong.TryParse(clipGuidHex,
                            System.Globalization.NumberStyles.HexNumber,
                            System.Globalization.CultureInfo.InvariantCulture, out ulong guid))
            return 0;
        return Native.Api.AudioPlay(guid, (int)group, volume, pan, loop ? 1 : 0);
    }

    /// <summary>一次性音效（SFX 组默认；命中/拾取/技能等事件音的标准入口）。</summary>
    public static void PlayOneShot(string clipGuidHex, float volume = 1f,
                                   AudioGroup group = AudioGroup.Sfx)
        => Play(clipGuidHex, volume, group, 0f, false);

    /// <summary>2D 空间播放（ADR-015 M5）：worldPos 触发时快照定衰减/声像（D7：
    /// 静态位，Unity PlayClipAtPoint 同构；移动声源用 AudioSource 组件）。衰减线性
    /// refDist 全增益 → maxDist 归零；默认 256/1024 对表模板相机。</summary>
    public static unsafe uint PlayAt(string clipGuidHex, Vec2 worldPos, float volume = 1f,
                                     AudioGroup group = AudioGroup.Sfx,
                                     float refDist = 256f, float maxDist = 1024f,
                                     bool loop = false)
    {
        if (Native.Api.AudioPlayAt == null || clipGuidHex == null)
            return 0;
        if (!ulong.TryParse(clipGuidHex,
                            System.Globalization.NumberStyles.HexNumber,
                            System.Globalization.CultureInfo.InvariantCulture, out ulong guid))
            return 0;
        return Native.Api.AudioPlayAt(guid, worldPos.X, worldPos.Y, volume, (int)group,
                                      refDist, maxDist, loop ? 1 : 0);
    }

    /// <summary>BGM 单槽换曲（Bgm 组循环；D4：旧曲与新曲 fadeSec 交叉淡出，0 = 硬切）。
    /// 槽在引擎侧——热重载/连播不叠曲。</summary>
    public static unsafe void PlayBgm(string clipGuidHex, float volume = 0.55f,
                                      float fadeSec = 0.5f)
    {
        if (Native.Api.AudioBgm == null || clipGuidHex == null)
            return;
        if (!ulong.TryParse(clipGuidHex,
                            System.Globalization.NumberStyles.HexNumber,
                            System.Globalization.CultureInfo.InvariantCulture, out ulong guid))
            return;
        Native.Api.AudioBgm(guid, volume, fadeSec);
    }

    /// <summary>停 BGM（幂等；D4 fadeSec 淡出，0 = 硬切）。</summary>
    public static unsafe void StopBgm(float fadeSec = 0.5f)
    {
        if (Native.Api.AudioBgmStop != null) Native.Api.AudioBgmStop(fadeSec);
    }

    /// <summary>停指定声部（Play/PlayAt 返回的逻辑 voiceId）。返回是否停到。</summary>
    public static unsafe bool Stop(uint voiceId)
        => voiceId != 0 && Native.Api.AudioStop != null && Native.Api.AudioStop(voiceId) != 0;

    /// <summary>组音量 0..1（Bgm/Sfx/Ui；设置屏滑条消费）。</summary>
    public static unsafe void SetGroupVolume(AudioGroup group, float volume)
    {
        if (Native.Api.AudioSetGroupVolume != null)
            Native.Api.AudioSetGroupVolume((int)group, volume);
    }

    /// <summary>主音量 0..1（D6 get/set 对称）。写走 staging（当帧提交期落地引擎），
    /// get 直读引擎现值——同帧写读 = 旧值（异步语义，跨帧往返为准）。</summary>
    public static unsafe float MasterVolume
    {
        get => Native.Api.AudioMasterVolGet != null ? Native.Api.AudioMasterVolGet() : 1f;
        set
        {
            if (Native.Api.AudioMasterVol != null) Native.Api.AudioMasterVol(value);
        }
    }

    /// <summary>显式暂停（D5）：true = 循环声源/BGM 声部级挂起（一次性放完、Ui 组
    /// 免疫——ADR-015 M4）。游戏暂停态自调；引擎不自动映射 Time.Scale（流程冻结
    /// 误停防线）。</summary>
    public static unsafe bool Paused
    {
        set
        {
            if (Native.Api.AudioSetPaused != null) Native.Api.AudioSetPaused(value ? 1 : 0);
        }
    }

    /// <summary>全停（场景切换/结算清场；编辑器 StopPlay 引擎侧同款）。</summary>
    public static unsafe void StopAll()
    {
        if (Native.Api.AudioStopAll != null) Native.Api.AudioStopAll();
    }
}
