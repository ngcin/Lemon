# CI win job 首跑热修⑥ —— stb_vorbis 单字母宏污染 Windows 头（MiniAudioImpl.c）（2026-10-05，W7 迭代）

事件：热修⑤ 后重跑，音频 TU 收尾时炸：`MiniAudioImpl.c`（stb_vorbis.c 实现 +
miniaudio 实现同 TU——0.11.25 官方粘合约定，不可拆）在 Windows SDK `winnt.h`
21699-21705 级联 C2059/C2143，miniaudio.h:23409 C2143 "missing ')' before
'string'"。

根因（本地取证 + 机制实证）：stb_vorbis.c **:5131-5133 定义单字母常量宏
`L`/`C`/`R`**（声道映射表 `channel_definitions` 专用）——同 TU 后续 include 的
windows.h 族全被污染：

- `L"..."` 宽字符串字面量前缀被宏吞 → `L"text"` 预处理成 `(常量) "text"`，
  miniaudio WASAPI 回环代码 `MA_COPY_MEMORY(..., MA_VIRTUAL_AUDIO_DEVICE_
  PROCESS_LOOPBACK /* L"..." */, ...)` 即 23409 的 "missing ')' before
  'string'"；winnt.h 段级联同源。
- mac 不可见：miniaudio macOS 走 CoreAudio 路径不拉 windows.h，第三方 vendored
  约束"mac 单平台验证"的盲区第一例（07 §3.6 批⑦ 表类别："宏污染面"）。

修法：两 include 之间 `#undef L/C/R`（stb 的使用点全在自身 .c 内，上方 include
展开完成即无主；TRUE/FALSE 与 Windows 头同值重定义无害不undef）。stb_vorbis.c
全部 `#define` 复查过，其余均 STB 前缀或非 SDK 令牌，无第二颗。

mac 复验：构建绿 + ctest 4/4（该 TU mac 同编，undef 位于 stb 展开后零影响）。

顺带登记（未动）：D9025 "overriding '/W1' with '/w'"——源文件级 `-w`（压 vendored
 件告警）与 VS 工程 /W1 的旗标序警告，无害噪音；CS9196 ×2 维持前条登记。
