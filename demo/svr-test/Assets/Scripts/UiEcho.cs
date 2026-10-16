// svr-test — UI 事件回显（UiTest.scene 专用：③c 真人验收两件的观测面）
//
// 面板 = Assets/UI/main.rml（文档级监听：doc 内所有 Click/Change/Submit 均产
// UiEvent，data-event 缺省时 ev 为空、key = 元素 id——main.rml 的按钮无
// data-event，日志按 key=start/quit 区分）。
//
// 判据对照（main.rml 面板自述）：
//   点「开始游戏/退出演示」→ 终端各一条 [ui-echo] Click（事件回传判据）；
//   起名框打中文 → 字上屏 + 候选窗贴光标（IME 判据，视觉面）；输入触发
//   Change 事件 = 提交制文本输入的值回传链路（payload = 控件值）。
//
// 订阅生命周期：UI.Events 订阅表跨局保留（play reset 只清待发不清订阅），
// 故 OnDestroy 必须退订，否则二局起双份日志。
using System;
using Lemon;

public sealed class UiEcho : LemonBehaviour
{
    protected override void Awake()
    {
        UI.Events.Subscribe(OnUiEvent);
    }

    protected override void OnDestroy()
    {
        UI.Events.Unsubscribe(OnUiEvent);
    }

    void OnUiEvent(UiEvent e)
    {
        string payload = e.PayloadStr;
        Console.WriteLine($"[ui-echo] {((UiEventKind)e.Kind)} doc='{e.DocStr}'"
                          + $" key='{e.KeyStr}' ev='{e.EvStr}'"
                          + (payload.Length > 0 ? $" payload='{payload}'" : ""));
    }
}
