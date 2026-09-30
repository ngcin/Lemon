// svr-test — 波次表数据化载入器（M6a 批② T1/T2 使用范例；ADR-012 D1）
//
// 作者路径（加波次 = 编辑表，不改代码）：
//   1. Excel/Numbers 编辑 CSV（导出 UTF-8）→ 拖入 AssetBrowser → 生成
//      Assets/tables/waves.tab（csv 源不拷入；改完重拖 = 覆盖再导入）；
//   2. 表的 GUID（AssetBrowser 右键「复制 GUID」）填到下方 kWavesTable；
//   3. Director 实体挂本脚本（Main.scene scripts[]）。Main.scene 的
//      WaveDirector.waves 已清零——waves.tab 为唯一权威（ADR-012 D2 的用户项目
//      例外：引擎模板波次留组件，用户项目按需外置；载入发生在 Awake = 首 Step
//      前，引擎 WaveDirectorSystem 消费链零改动）。
//
// 列契约（首行 = 列头；列序敏感，与 waves.tab 生成列一致）：
//   startTime rampMult entryCount
//   e0prefab e0count e0interval e0range ×4（e0..e3）
// prefab 列填 prefab 资产 16 位 GUID（Inspector 裸数字 = 其低 32 位）——比数字
// 可读且改名稳定；空串 = 条目空置。
using System;
using Lemon;
using Lemon.Interop;

public sealed class WaveTableLoader : LemonBehaviour
{
    /// <summary>waves.tab 资产 GUID（右键「复制 GUID」粘贴处）。</summary>
    const string kWavesTable = "7e57100000100001";

    protected override void Awake()
    {
        if (!gameObject.TryGetComponent<WaveDirector>(out var wd)) {
            Console.Error.WriteLine("[lemon][warn] WaveTableLoader：实体无 WaveDirector 组件");
            return;
        }
        if (!Table.Has(kWavesTable)) {
            Console.Error.WriteLine("[lemon][warn] WaveTableLoader：waves.tab 缺失/未导入"
                                    + "（kWavesTable GUID 对不对？）——波次为空");
            return;
        }
        // 数据行自 1 起（第 0 行 = 列头）；上限 16 = WaveDirector.waves 定长
        int n = Math.Min(Table.Rows(kWavesTable) - 1, 16);
        for (int i = 0; i < n; i++) {
            int r = i + 1;
            var def = wd.GetWave(i);
            def.StartTime = Table.Float(kWavesTable, r, 0);
            def.RampMult = Table.Float(kWavesTable, r, 1);
            def.EntryCount = (byte)Math.Clamp(Table.Int(kWavesTable, r, 2), 0, 4);
            for (int e = 0; e < 4; e++) {
                var en = def.GetEntry(e);
                en.PrefabId = GuidLow32(Table.Str(kWavesTable, r, 3 + e * 4));
                en.Count = (ushort)Table.Int(kWavesTable, r, 4 + e * 4);
                en.Interval = Table.Float(kWavesTable, r, 5 + e * 4);
                en.Range = Table.Float(kWavesTable, r, 6 + e * 4);
                def.SetEntry(e, en);
            }
            wd.SetWave(i, def);
        }
        wd.WaveCount = (byte)n;
        gameObject.SetComponent(wd);
        Console.Error.WriteLine($"[lemon] waves.tab 载入：{n} 波（首波 t={wd.GetWave(0).StartTime}）");
    }

    /// <summary>16 位 GUID hex → 低 32 位（引擎 prefabId 口径）；空/坏 = 0。</summary>
    static uint GuidLow32(string hex)
        => string.IsNullOrEmpty(hex) || hex.Length < 8
               ? 0
               : Convert.ToUInt32(hex.Substring(hex.Length - 8), 16);
}
