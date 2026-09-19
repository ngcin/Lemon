// Lemon.Entry — 档② 批量系统帧分发（04 §2.2 / ADR-010 D1）
// C++ 侧（管线线程）构造 BatchSystemFrame[] → lemon_batch_tick（UCO）→ Post 到域线程
// → 逐系统逐块回调 IForEachSystem.ForEach（原地写回零拷贝）。
// 异常隔离（04 §7）：单系统单帧异常 → stderr 红字 + 本帧跳过该系统剩余块；
// 连续 60 帧异常 → 自动禁用（引擎进程永不因脚本异常崩溃）。
using System;
using System.Runtime.InteropServices;

namespace Lemon.Entry;

/// <summary>与 C++ ScriptHost 逐字节一致的帧描述符（两侧同步改，改动 = 破回放）。</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BatchBlock
{
    public Lemon.Interop.EntityHandle* Entities; // Length 个
    public void** Comps;                 // [slot * Stride + i] = 实例指针
    public int Length;
    public int Stride;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct BatchSystemFrame
{
    public int SystemIndex;              // Lemon.Scripting 注册序
    public BatchBlock* Blocks;
    public int BlockCount;
    public ulong RngSeed;                // 世界种子（脚本 RNG 子流见 Lemon.Scripting.Rng）
    public float Dt;
    public int Disabled;                 // 回写：异常禁用后 C++ 侧跳过构造（省死块）
}

internal static unsafe class Batch
{
    private const int kDisableAfterFrames = 60;

    private sealed class SysState
    {
        public int ConsecutiveBadFrames;
        public bool Disabled;
    }

    private static SysState[] s_states = Array.Empty<SysState>();

    public static int SystemCount() => Lemon.Scripting.SystemCount;

    /// <summary>注册表快照：组件 id 拷入调用方缓冲，返回实际拷贝数（≤cap）。</summary>
    public static int CopyQuery(int systemIndex, byte* dst, int cap)
    {
        byte[] q = Lemon.Scripting.GetQuery(systemIndex);
        int n = System.Math.Min(q.Length, cap);
        for (int i = 0; i < n; i++) dst[i] = q[i];
        return n;
    }

    public static void Tick(BatchSystemFrame* frames, int count)
    {
        int n = Lemon.Scripting.SystemCount;
        if (s_states.Length != n) {
            s_states = new SysState[n];
            for (int i = 0; i < n; i++) s_states[i] = new SysState();
        }

        bool badThisFrame = false;
        for (int f = 0; f < count; f++) {
            BatchSystemFrame* fr = frames + f;
            int idx = fr->SystemIndex;
            if ((uint)idx >= (uint)n) continue;
            var sys = Lemon.Scripting.Get(idx);
            var st = s_states[idx];
            if (st.Disabled) continue;

            byte* compIds = stackalloc byte[8];
            int compCount = System.Math.Min(Lemon.Scripting.GetQuery(idx).Length, 8);
            for (int c = 0; c < compCount; c++) compIds[c] = Lemon.Scripting.GetQuery(idx)[c];

            badThisFrame = false;
            for (int b = 0; b < fr->BlockCount; b++) {
                BatchBlock* blk = fr->Blocks + b;
                try {
                    var chunk = new Lemon.Chunk(blk->Length, blk->Entities, blk->Comps,
                                                compIds, compCount, blk->Stride);
                    sys.ForEach(in chunk);
                } catch (Exception e) {
                    // 红字 + 本帧该系统剩余块跳过（04 §7 异常隔离）
                    Console.Error.WriteLine($"[lemon][error] script system '{sys.Name}' " +
                                            $"block {b}: {e.GetType().Name}: {e.Message}");
                    badThisFrame = true;
                    break;
                }
            }

            if (badThisFrame) {
                if (++st.ConsecutiveBadFrames >= kDisableAfterFrames) {
                    st.Disabled = true;
                    fr->Disabled = 1;
                    Console.Error.WriteLine($"[lemon][error] script system '{sys.Name}' disabled " +
                                            $"after {kDisableAfterFrames} consecutive failing frames");
                }
            } else {
                st.ConsecutiveBadFrames = 0;
            }
        }
    }
}
