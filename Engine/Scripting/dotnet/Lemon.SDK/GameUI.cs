// Lemon.SDK — 游戏 UI 通道（M6b 批③c，ADR-014 D2：M1/M2/M3 波1 + D4 提交制）----
// 形态与既有纪律同构：ops 命令缓冲（SceneOps 先例）+ 事件队列（无回调跨边界）。
// 数据流：UI.* 入 staging → UI.Apply() 序列化（UTF-8 arena + 行块）→ ready 队列
// → 宿主每帧拉取（lemon_ui_ops_pull，TickBatch 尾 → UiSubsystem::ApplyOps 当帧可见）。
// 事件反向：引擎文档监听器产 UiEvent → 宿主 #16 头派发（lemon_ui_events_dispatch）
// → UI.Events 订阅者。
// 纪律：UI 状态不入 StateHash、UI 交互不入输入快照（基准场零调用零漂移）。
// 与既有 Lemon.Ui（RtUi 通道，ADR-014 D5 兼容层）并存——迁移完成后 ③d 起模板转 UI。
// 注：文件名 GameUI.cs 因 macOS 大小写不敏感（与 Ui.cs 撞名），类名 = UI。
// 典型用法（纸面验证 ⓪）：
//   UI.Show("upgrade_cards", modal: true);
//   UI.SetText("upgrade_cards", "title", "升级！三选一");
//   UI.SetItems("upgrade_cards", "cards", "card", rolls);
//   UI.Apply();
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace Lemon;

public enum UiOpType : byte
{
    Show = 0, Hide, SetText, SetAttr, SetClass, SetStyle, SetInnerRml, SetItems,
}

public enum UiEventKind : byte
{
    Click = 0, Change, Submit, Hover, DocumentReloaded,
}

/// <summary>UiEventC 镜像（172B，与 Engine/Ui/UiBridge.h 逐字节一致）。</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct UiEvent
{
    public byte Kind;
    public byte Modal;
    public ushort Reserved;
    public fixed byte Doc[32];
    public fixed byte Key[48];
    public fixed byte Ev[16];
    public fixed byte Payload[64];
    public float Wx, Wy;

    /// <summary>托管视图字段（dispatch 时惰性取串；低频点击级，取串分配可接受）。</summary>
    public string DocStr { get { fixed (byte* p = Doc) return Str(p); } }
    public string KeyStr { get { fixed (byte* p = Key) return Str(p); } }
    public string EvStr { get { fixed (byte* p = Ev) return Str(p); } }
    public string PayloadStr { get { fixed (byte* p = Payload) return Str(p); } }
    private static string Str(byte* p) => *p == 0 ? "" : new string((sbyte*)p);
}

/// <summary>UiOpC 镜像（32B，与 Engine/Ui/UiBridge.h 逐字节一致；宿主拉取用）。</summary>
[StructLayout(LayoutKind.Sequential)]
public struct UiOp
{
    public UiOpType Type;
    public byte Flags, StrCount, Reserved;
    public uint S0, S1, S2, S3;
    public uint I0, I1, Reserved2;
}

/// <summary>SetItems 条目：稳定 key（事件回传认它，不认下标）+ 字段集（模板
/// data-field / class {{}} 占位的填充值；字段不在模板集 = 引擎响亮失败）。</summary>
public sealed class UiItem
{
    public string Key = "";
    public Dictionary<string, string> Fields = new();
}

public static unsafe class UI
{
    // ---- staging（UI.* 写；Apply 前跨帧滞留合法——屏幕切换类低频操作建议当帧 Apply）----
    private sealed class RawOp
    {
        public UiOpType Type;
        public byte Flags;
        public string A = "", B = "", C = "", D = ""; // 依类型：doc/key/value/attr/…
        public uint I0;
        public List<UiItem>? Items;                    // SetItems
    }
    private static readonly List<RawOp> s_staging = new();
    private static readonly object s_lock = new();

    // ---- ready（Apply() 产物；宿主拉取）----
    private static readonly List<UiOp> s_ready = new();
    private static byte[] s_arena = new byte[4096];
    private static int s_arenaLen;

    // ---- staging 同值去重（批③c-2）：幂等 op 的 setter 级等值早退 ----
    // 主流 retained GUI 同位标配（UGUI Text / Godot Label / cocos setString 的
    // if(same) return）——RmlUi SetInnerRML/SetAttribute 无此早退（源码双证），
    // 每帧同值写全量拆建文本元素。键 = 类型前缀|doc|key|限定名，值 = 载荷串
    // （原始串比较——引擎侧 EscapeText/GUID 转换是纯函数，同原始串同转换结果）。
    // 只收幂等值 op：Show/Hide/SetItems 不入表（Show 重复 = D1 层序提顶语义，
    // SetItems 全量替换非幂等）。失效五处 = 各 s_lastSent.Clear() 点——漏一处
    // = 静默丢写。
    private static readonly Dictionary<string, string> s_lastSent = new();

    /// <summary>同值跳过判定（调用方须持 s_lock）：与上次实际入列载荷相同 → true
    /// （跳过本次入列）；否则记新值返回 false。缓存记的是"已入列"而非"已应用"——
    /// 凡载荷可能未达引擎的路径（拉取丢弃/重装载）必须整表失效。</summary>
    private static bool DedupSkip(string cacheKey, string payload)
    {
        if (s_lastSent.TryGetValue(cacheKey, out var v) && v == payload) return true;
        s_lastSent[cacheKey] = payload;
        return false;
    }

    // ---- 事件订阅（GC 纪律：静态表；实例订阅用 LemonBehaviour.Subscribe 退订）----
    private static readonly List<Action<UiEvent>> s_handlers = new();
    private static readonly object s_evLock = new();
    internal static int ReceivedCount;

    // ------------------------------------------------------------ M1/M2 写口 ----
    /// <summary>显示屏幕文档（doc = 文档名/资产 relPath）。modal = true：模态标记
    /// （事件携带 + 引擎键盘让出面扩大——"菜单打开时脚本让出输入"）。</summary>
    public static void Show(string doc, bool modal = false)
    { lock (s_lock) s_staging.Add(new RawOp { Type = UiOpType.Show, A = doc,
                                              Flags = (byte)(modal ? 1 : 0) }); }

    public static void Hide(string doc)
    { lock (s_lock) s_staging.Add(new RawOp { Type = UiOpType.Hide, A = doc }); }

    /// <summary>纯文本填充（key = 元素 id 或 "容器id/条目key" 路径；文本自动转义）。
    /// 同值跳过（批③c-2）：与上次同串不再入列——HUD 每帧直写即标准姿势。</summary>
    public static void SetText(string doc, string key, string text)
    { lock (s_lock) {
          if (DedupSkip("t|" + doc + "|" + key, text)) return;
          s_staging.Add(new RawOp { Type = UiOpType.SetText, A = doc, B = key, C = text });
      } }

    /// <summary>属性直写（attr = "src" 时 16 位 GUID hex 自动转 "guid:" 资产协议）。
    /// 同值跳过（progress value/max 每帧驱动的稳态吸收）。</summary>
    public static void SetAttr(string doc, string key, string attr, string value)
    { lock (s_lock) {
          if (DedupSkip("a|" + doc + "|" + key + "|" + attr, value)) return;
          s_staging.Add(new RawOp { Type = UiOpType.SetAttr, A = doc, B = key, C = attr, D = value });
      } }

    /// <summary>样式类增删（on = true 加 / false 删；品级色/置灰等状态类）。同态跳过。</summary>
    public static void SetClass(string doc, string key, string cls, bool on)
    { lock (s_lock) {
          if (DedupSkip("c|" + doc + "|" + key + "|" + cls, on ? "1" : "0")) return;
          s_staging.Add(new RawOp { Type = UiOpType.SetClass, A = doc, B = key, C = cls,
                                    Flags = (byte)(on ? 1 : 0) });
      } }

    /// <summary>单条内联样式（prop/value = RCSS 属性名/值）。同值跳过。</summary>
    public static void SetStyle(string doc, string key, string prop, string value)
    { lock (s_lock) {
          if (DedupSkip("s|" + doc + "|" + key + "|" + prop, value)) return;
          s_staging.Add(new RawOp { Type = UiOpType.SetStyle, A = doc, B = key, C = prop, D = value });
      } }

    /// <summary>富文本内嵌（RML 片段——彩色段/内嵌图标；不转义，文档作者负责）。
    /// 同值跳过。</summary>
    public static void SetInnerRml(string doc, string key, string rml)
    { lock (s_lock) {
          if (DedupSkip("r|" + doc + "|" + key, rml)) return;
          s_staging.Add(new RawOp { Type = UiOpType.SetInnerRml, A = doc, B = key, C = rml });
      } }

    /// <summary>模板克隆填充（M2 SetItems）：container = data-template 容器 id；
    /// template = template data-name。全量语义（先清旧行再建）。img 字段值 = 16 位
    /// GUID hex（→ "guid:" 资产协议）或相对路径。</summary>
    public static void SetItems(string doc, string container, string template,
                                IEnumerable<UiItem> items)
    {
        var list = items as List<UiItem> ?? new List<UiItem>(items);
        lock (s_lock) s_staging.Add(new RawOp { Type = UiOpType.SetItems, A = doc, B = container,
                                                C = template, Items = list, I0 = (uint)list.Count });
    }

    /// <summary>提交（M2 单一批量口）：staging → ready（UTF-8 arena + 行块编码）。
    /// 不 Apply = 不生效（引擎拉取的是 ready）。</summary>
    public static void Apply()
    {
        lock (s_lock) {
            if (s_staging.Count == 0) return;
            EnsureArena(s_arenaLen + EstimateBytes());
            foreach (var op in s_staging) {
                var o = new UiOp { Type = op.Type, Flags = op.Flags };
                if (op.Items != null) {
                    o.S0 = PutStr(op.A);
                    o.S1 = PutStr(op.B);
                    o.S2 = PutStr(op.C);
                    o.S3 = (uint)s_arenaLen; // 行块起点
                    o.I0 = op.I0;
                    PutRowBlock(op.Items);
                    o.StrCount = 4;
                } else {
                    // 恒 PutStr（空串也写独立 NUL）：取"当前 arenaLen 不写字节"的捷径会让
                    // 空串偏移别名到后续串/上一批残字节——引擎读出脏文本
                    o.S0 = PutStr(op.A);
                    o.S1 = PutStr(op.B);
                    o.S2 = PutStr(op.C);
                    o.S3 = PutStr(op.D);
                    o.StrCount = op.Type switch {
                        UiOpType.Show => 1, UiOpType.Hide => 1, UiOpType.SetText => 3,
                        UiOpType.SetAttr => 4, UiOpType.SetClass => 3, UiOpType.SetStyle => 4,
                        UiOpType.SetInnerRml => 3, _ => 0,
                    };
                }
                s_ready.Add(o);
            }
            s_staging.Clear();
        }
    }

    // ------------------------------------------------------------ 事件订阅 ----
    /// <summary>UI 事件订阅（M3：click/change/submit/DocumentReloaded；低频点击级）。
    /// Click：ev = data-event 语义名、key = 元素 id 或 "容器/条目key"；
    /// Change：payload = 控件值（提交制文本输入的值回传）；DocumentReloaded = 热重载
    /// 重灌信号（doc 定位，key 空）。</summary>
    public static class Events
    {
        public static void Subscribe(Action<UiEvent> handler)
        { lock (s_evLock) s_handlers.Add(handler); }

        public static void Unsubscribe(Action<UiEvent> handler)
        { lock (s_evLock) s_handlers.Remove(handler); }

        /// <summary>诊断：累计派发数（测试用）。</summary>
        public static int Received => ReceivedCount;
    }

    // ------------------------------------------------------------ 编码内部 ----
    /// <summary>arena 预分配估算。乘 3 是 UTF-8 每 UTF-16 char 的字节上界：BMP 内
    /// 单 char ≤ 3B；增补面字符（4B UTF-8）由一对代理表示 = 2 char → 仍 2B/char；
    /// 孤立代理经默认替换回退 ≤ 1B。PutStr/PutRowBlock 的实际写入恒 ≤ 本估算
    /// （+16/+8 冗余覆盖 NUL 与行块头），故编码期不会中途扩容/越界。</summary>
    private static int EstimateBytes()
    {
        int n = 0;
        foreach (var op in s_staging) {
            n += (op.A.Length + op.B.Length + op.C.Length + op.D.Length) * 3 + 16;
            if (op.Items != null)
                foreach (var it in op.Items) {
                    n += it.Key.Length * 3 + 8;
                    foreach (var kv in it.Fields) n += (kv.Key.Length + kv.Value.Length) * 3 + 8;
                }
        }
        return n;
    }

    private static void EnsureArena(int need)
    {
        if (need <= s_arena.Length) return;
        int cap = s_arena.Length;
        while (cap < need) cap *= 2;
        Array.Resize(ref s_arena, cap);
    }

    private static uint PutStr(string s)
    {
        uint at = (uint)s_arenaLen;
        if (!string.IsNullOrEmpty(s)) {
            int n = Encoding.UTF8.GetBytes(s.AsSpan(), s_arena.AsSpan(s_arenaLen));
            s_arenaLen += n;
        }
        s_arena[s_arenaLen++] = 0; // NUL 终止契约（UiBridge.h）
        return at;
    }

    // 行块：每行 = u8 keyLen | key | u16 字段数 | (u8 名长 | u16 值长 | 字节)*
    private static void PutRowBlock(List<UiItem> items)
    {
        foreach (var it in items) {
            PutStr8(it.Key); // review 2026-10-02 #22：长度=字节数（原写 char 数）
            PutU16((ushort)Math.Min(it.Fields.Count, ushort.MaxValue));
            foreach (var kv in it.Fields) {
                PutStr8(kv.Key);
                PutU16(0); // 值长占位 → 写值后回填（值长理论 ≤64K，UI 语义远低）
                int lenAt = s_arenaLen - 2;
                PutBytes(kv.Value, ushort.MaxValue);
                ushort len = (ushort)(s_arenaLen - lenAt - 2);
                s_arena[lenAt] = (byte)(len & 0xFF);
                s_arena[lenAt + 1] = (byte)(len >> 8);
            }
        }
    }

    /// <summary>行块 u8 长度前缀串（key/字段名）。长度 = 实际 UTF-8 **字节数**、
    /// 超 255B 截断退码点边界——原实现写 char 数而字节按 UTF-8 落盘：非 ASCII
    /// （如中文 key）时引擎按字节解码即失步 = 行块整体错位（review 2026-10-02
    /// #22；UiBridge.h 行块契约明文字节语义）。ASCII 输出与旧版逐字节相同。</summary>
    private static void PutStr8(string s)
    {
        PutU8(0); // 占位回填（字节长在截断边界定后才知道）
        int at = s_arenaLen;
        int n = Encoding.UTF8.GetBytes(s.AsSpan(),
            s_arena.AsSpan(at, Math.Min(255, s_arena.Length - at)));
        while (n > 0 && (s_arena[at + n - 1] & 0xC0) == 0x80) n--; // 退到码点边界
        s_arena[at - 1] = (byte)n;
        s_arenaLen = at + n;
    }

    private static void PutU8(byte v) { s_arena[s_arenaLen++] = v; }
    private static void PutU16(ushort v) { s_arena[s_arenaLen++] = (byte)(v & 0xFF);
                                           s_arena[s_arenaLen++] = (byte)(v >> 8); }
    private static void PutBytes(string s, int maxBytes)
    {
        // 短串直写（值 ≤64K）；越界截断退码点边界（不产非法 UTF-8 半串——
        // review 2026-10-02 #60；引擎侧响亮失败可见半串）
        int n = Encoding.UTF8.GetBytes(s.AsSpan(),
            s_arena.AsSpan(s_arenaLen, Math.Min(maxBytes, s_arena.Length - s_arenaLen)));
        while (n > 0 && (s_arena[s_arenaLen + n - 1] & 0xC0) == 0x80) n--;
        s_arenaLen += n;
    }

    // ------------------------------------------------- Lemon.Entry 域线程侧 ----
    /// <summary>宿主拉取 ready（拷入调用方缓冲后清空）。返回 op 数；缓冲不足 =
    /// **整批丢弃**返回 -1（宿主红字——256 op/64KB 上限按 UiBridge.h，超容量属
    /// 契约违规应响亮暴露）。capOps &lt;= 0 / dst = null = 弃置模式（无钩子宿主）。</summary>
    internal static int PullOps(UiOp* dst, int capOps, byte* dstArena, int capArena,
                                int* arenaBytes)
    {
        lock (s_lock) {
            if (capOps <= 0 || dst == null) {
                int dropped = s_ready.Count;
                s_ready.Clear();
                s_arenaLen = 0;
                s_lastSent.Clear(); // 载荷未达引擎（无钩子宿主）——"已入列"全数作废
                if (arenaBytes != null) *arenaBytes = 0;
                return dropped;
            }
            if (s_ready.Count > capOps || s_arenaLen > capArena) {
                s_ready.Clear();
                s_arenaLen = 0;
                s_lastSent.Clear(); // 整批丢弃（-1 契约路径）——同上，缓存不得记谎言
                if (arenaBytes != null) *arenaBytes = 0;
                return -1;
            }
            int n = s_ready.Count;
            for (int i = 0; i < n; i++) dst[i] = s_ready[i];
            if (s_arenaLen > 0) {
                // fixed 固定（GetArrayDataReference 裸 ref 转指针 = GC 可移动对象——
                // 拷贝窗口外无分配故"通常没事"，但契约上必须钉死）
                fixed (byte* src = &s_arena[0])
                    Buffer.MemoryCopy(src, dstArena, (ulong)capArena, (ulong)s_arenaLen);
            }
            if (arenaBytes != null) *arenaBytes = s_arenaLen;
            s_ready.Clear();
            s_arenaLen = 0;
            return n;
        }
    }

    /// <summary>宿主派发 UI 事件（#16 头部；异常隔离同 Events 派发纪律）。</summary>
    internal static void DispatchEvents(UiEvent* src, int n)
    {
        Action<UiEvent>[] handlers;
        lock (s_evLock) {
            ReceivedCount += n;
            handlers = s_handlers.ToArray();
        }
        for (int i = 0; i < n; i++) {
            var e = src[i];
            // 批③c-2：DOM 已重灌（热重载）——同值缓存全数作废，重灌写必须可达
            // （整表清：热重载为开发期低频事件，宁多送一拍；清在 handler 前，
            // 使 OnUiEvent → Refill 序列通过）
            if (e.Kind == (byte)UiEventKind.DocumentReloaded)
                lock (s_lock) s_lastSent.Clear();
            foreach (var h in handlers) {
                try { h(e); }
                catch (Exception ex) {
                    Console.Error.WriteLine($"[lemon][error] ui event handler: {ex.Message}");
                }
            }
        }
    }

    /// <summary>换域清空（DomainManager 调，与 Events.Reset 同点位）。</summary>
    internal static void Reset()
    {
        lock (s_lock) { s_staging.Clear(); s_ready.Clear(); s_arenaLen = 0;
                        s_lastSent.Clear(); }
        lock (s_evLock) { s_handlers.Clear(); ReceivedCount = 0; }
    }

    /// <summary>导出异常路径的强制丢弃（lemon_ui_ops_pull catch：-1 语义 = 整批已
    /// 丢弃，把契约做实——防止毒 ready 批每帧重试）。</summary>
    internal static void DiscardPending()
    {
        lock (s_lock) { s_ready.Clear(); s_arenaLen = 0;
                        s_lastSent.Clear(); } // 载荷未达引擎，缓存作废
    }

    /// <summary>进 Play 域复位（lemon_play_reset）：清待发与计数，保留订阅表
    /// （静态订阅跨局存活——Events.PlayReset 同语义）。</summary>
    internal static void PlayReset()
    {
        lock (s_lock) { s_staging.Clear(); s_ready.Clear(); s_arenaLen = 0;
                        s_lastSent.Clear(); } // 新局重灌从零（帧计数归零场景）
        lock (s_evLock) ReceivedCount = 0;
    }
}
