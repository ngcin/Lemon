// Lemon.SDK — 音频播放门面（M6c 竖切批，ADR-015；引擎侧 AudioEngine + AudioHooks）
// -------------------------------------------------------------------------------
// 语义：clip 引用 = 资产 GUID（16 位 hex，prefab 同款口径）；竖切批四槽 =
// Play/Stop/SetGroupVolume/StopAll——非空间、无淡入淡出（批② 正式化补 PlayAt
// 衰减声像 + BGM 交叉淡出 D4 + AudioChannel 命令表）。旧宿主未注册尾槽 = 判空
// 降级 no-op（同 Lemon.Tween 口径）。音频状态不入 StateHash——金回放零重录。
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
    private static uint _bgm; // BGM 单槽（新 PlayBgm 顶停旧曲；批② 换交叉淡出）

    /// <summary>按资产 GUID 播放。返回 voiceId（0 = 失败：未装载/池满/旧宿主）。
    /// loop = true 时循环区间 = clip 全曲（竖切批口径）。</summary>
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

    /// <summary>BGM 单槽循环（Bgm 组；新曲顶停旧曲——竖切批硬切，批② 淡出）。</summary>
    public static void PlayBgm(string clipGuidHex, float volume = 0.55f)
    {
        if (_bgm != 0) Stop(_bgm);
        _bgm = Play(clipGuidHex, volume, AudioGroup.Bgm, 0f, true);
    }

    /// <summary>停 BGM（幂等）。</summary>
    public static void StopBgm()
    {
        if (_bgm != 0) { Stop(_bgm); _bgm = 0; }
    }

    /// <summary>停指定声部（Play 返回的 voiceId）。返回是否停到。</summary>
    public static unsafe bool Stop(uint voiceId)
        => voiceId != 0 && Native.Api.AudioStop != null && Native.Api.AudioStop(voiceId) != 0;

    /// <summary>组音量 0..1（Bgm/Sfx/Ui；设置屏滑条消费）。</summary>
    public static unsafe void SetGroupVolume(AudioGroup group, float volume)
    {
        if (Native.Api.AudioSetGroupVolume != null)
            Native.Api.AudioSetGroupVolume((int)group, volume);
    }

    /// <summary>全停（场景切换/结算清场；编辑器 StopPlay 引擎侧同款）。</summary>
    public static unsafe void StopAll()
    {
        _bgm = 0;
        if (Native.Api.AudioStopAll != null) Native.Api.AudioStopAll();
    }
}
