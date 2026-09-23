# 2026-09-20 · 向导创建后闪退闭环（相对路径 × 误触发 × 异常逃逸 三连缺陷）

用户实测报错（`Unhandled exception. System.ArgumentException: Path "./demo/test/.lemon/bin/test.dll" is not an absolute path` → abort）无头复现 exit=134 后逐层闭环（commit `35c4c3a`）：

1. **触发层**：FileWatcher"首拍不算"条件写反（`!snap.empty()` 分支——有文件的目录
   首拍必置脏）+ `lastHandledCsWrite_` 从 0 起步把"项目自带 .cs"当"变更"——两个
   误触发源叠加 = **每次打开项目必做一次无谓换装**（泄漏一个旧域；冷启 3151→1874ms）。
   修：primed_ 首拍基线 + 开项目时 ScriptSourceChanged() 基线化。
2. **路径层**：向导父目录手敲 `./demo` → 项目根相对 → 初始装载（InitScriptHostFrom）
   有绝对化、热重载（HotReloadAssembly）没有 → `LoadFromAssemblyPath` 抛参数异常。
   修：DB 入库即绝对化 root_ + 向导父目录绝对化 + HotReloadAssembly 兜底绝对化。
3. **拦截层（致命一环）**：`UnmanagedCallersOnly` 导出未 try/catch——托管异常逃逸到
   native = coreclr 直接 abort 整个编辑器。修：dm_load/unload/reload 导出 +
   DomainManager 全域生命周期方法 try/catch 转 false；`weak.Target!` 空解引用一并
   防护。**纪律沉淀：经 native 导出直通的托管代码，异常就地转返回值，永不逃逸。**
4. **回归层**：script-tests 永久用例（相对路径 dm_reload 返回 0 且进程存活 → 真路径
   恢复换装 → tick 复常）。

排查过程记档：崩溃栈 unresolved managed 帧 → 先无头复现（相对路径 + 带帧限跑）拿到
同栈 → Post:81 `throw cmd.Error` 定位逃逸口。recent.json 曾写入相对条目（崩溃前
管线已成功）——改记 DB 侧绝对 root_，用户文件已修正。

**回归**：ctest 3/3（含新用例）；M4.4 冒烟 600 帧 errors=0（中点 PNG 热替换不受
基线化影响）；--final 全 OK（stateBag 66/66 fps 59）；smoke-close 两模式 OK；
相对路径开项目零误触发零崩溃、中途 touch 真实换装 1 次正常。

---
