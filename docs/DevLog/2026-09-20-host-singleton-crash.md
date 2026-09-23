# 2026-09-20 · 会话内二次装配闪退闭环（宿主进程单例化）

用户二次实测崩溃（SIGSEGV @0x19，栈 = ProfilerPanel → GcAllocated →
CoreCLRHost::GetExport → 垃圾函数指针）。根因三层（commit `3cb6b6f`）：

- **事实**：CoreCLR 进程单例——同进程二次 `ScriptHost::Initialize` 必失败
  （script-tests 永久探针钉板 `second-host init=0`，进程存活）。
- **缺陷**：`InitScriptHostFrom` 二次执行先 `make_unique` 毁旧宿主 →
  `ctx_.scripts_` 悬空 → 二次 Initialize 失败 → `host_.reset()` → 悬空永久化
  → Profiler 每帧 `ctx.Scripts()->GcAllocated()` 踩已释放内存（0x19 = 小整数
  垃圾，典型 use-after-free 形貌）。
- **修复**：宿主复用（已有 host → `HotReloadAssembly` A 线换装至新程序集；失败
  转无脚本态）+ 不变量"ctx 指针先清再动宿主"（管线无 Game 分支同步）。副产：
  `--script` 与项目 Game/ 并存原本二次 init 必失败（M4.5 起静默无脚本），现
  复用换装正确生效。

验证：env 临时探针端到端驱动二次装配（换装 ok + GcAllocated 返回 158056 +
exit=0）后移除；ctest 3/3；M4.4 冒烟 / `--final` / smoke-close 全绿。

**当日两连修教训（编辑器会话生命周期纪律）**：CoreCLR 宿主 = 进程单例，编辑器
会话内任何"再装配"都必须走换装而非重建；凡经 native 导出持有/回调的托管指针
（ctx 指针 / 缓存导出函数），销毁侧必须先摘引用。

---
