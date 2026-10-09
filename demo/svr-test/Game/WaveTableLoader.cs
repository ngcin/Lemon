// svr-test — 波次表数据化载入器（M6a 批② T1/T2 使用范例；ADR-012 D1）
//
// 作者路径（加波次 = 编辑表，不改代码）：
//   1. Excel/Numbers 编辑 CSV（导出 UTF-8）→ 拖入 AssetBrowser → 生成
//      Assets/tables/waves.tab（csv 源不拷入；改完重拖 = 覆盖再导入）；
//   2. 表的 GUID（AssetBrowser 右键「复制 GUID」）填到下方 TableGuid；
//   3. Director 实体挂本脚本（Grass.scene scripts[]）。场景的
//      WaveDirector.waves 已清零——表为唯一权威（ADR-012 D2 的用户项目
//      例外：引擎模板波次留组件，用户项目按需外置；载入发生在 Awake = 首 Step
//      前，引擎 WaveDirectorSystem 消费链零改动）。
//   批⑨：TableGuid 虚属性化——变体场子类换表即可（VolcanoTableLoader →
//   waves_volcano.tab，火山场 Director 挂子类）。
//
// 列契约（首行 = 列头；列序敏感，与 waves.tab 生成列一致）：
//   startTime rampMult entryCount
//   e0prefab e0count e0interval e0range ×4（e0..e3）
// prefab 列填 prefab 资产 16 位 GUID（Inspector 裸数字 = 其低 32 位）——比数字
// 可读且改名稳定；空串 = 条目空置。
using System;
using Lemon;
using Lemon.Interop;

public class WaveTableLoader : LemonBehaviour
{
    /// <summary>waves.tab 资产 GUID（右键「复制 GUID」粘贴处；变体场子类覆写）。</summary>
    protected virtual string TableGuid => "7e57100000100001";

    protected override void Awake()
    {
        if (!gameObject.TryGetComponent<WaveDirector>(out var wd)) {
            Console.Error.WriteLine("[lemon][warn] WaveTableLoader：实体无 WaveDirector 组件");
            return;
        }
        if (!Table.Has(TableGuid)) {
            Console.Error.WriteLine("[lemon][warn] WaveTableLoader：波次表缺失/未导入"
                                    + "（TableGuid 对不对？）——波次为空");
            return;
        }
        // 数据行自 1 起（第 0 行 = 列头）；上限 16 = WaveDirector.waves 定长
        int n = Math.Min(Table.Rows(TableGuid) - 1, 16);
        for (int i = 0; i < n; i++) {
            int r = i + 1;
            var def = wd.GetWave(i);
            def.StartTime = Table.Float(TableGuid, r, 0);
            def.RampMult = Table.Float(TableGuid, r, 1);
            def.EntryCount = (byte)Math.Clamp(Table.Int(TableGuid, r, 2), 0, 4);
            for (int e = 0; e < 4; e++) {
                var en = def.GetEntry(e);
                en.PrefabId = GuidLow32(Table.Str(TableGuid, r, 3 + e * 4));
                en.Count = (ushort)Table.Int(TableGuid, r, 4 + e * 4);
                en.Interval = Table.Float(TableGuid, r, 5 + e * 4);
                en.Range = Table.Float(TableGuid, r, 6 + e * 4);
                def.SetEntry(e, en);
            }
            wd.SetWave(i, def);
        }
        wd.WaveCount = (byte)n;
        gameObject.SetComponent(wd);
        Console.Error.WriteLine($"[lemon] 波次表 {TableGuid} 载入：{n} 波（首波 t={wd.GetWave(0).StartTime}）");
    }

    /// <summary>16 位 GUID hex → 低 32 位（引擎 prefabId 口径）；空/坏 = 0。</summary>
    static uint GuidLow32(string hex)
        => string.IsNullOrEmpty(hex) || hex.Length < 8
               ? 0
               : Convert.ToUInt32(hex.Substring(hex.Length - 8), 16);
}

/// <summary>火山场波次表载入器（M7c 批⑨；Assets/tables/waves_volcano.tab——
/// 更难变体：快节奏/FastMob 提前/双 Boss 收口；数值为占位，用户可改表调参）。</summary>
public sealed class VolcanoTableLoader : WaveTableLoader
{
    protected override string TableGuid => "7e57100000100011";
}
