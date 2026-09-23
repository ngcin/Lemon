# 2026-09-24 M9 后续：编辑器拆毁序崩溃（收尾验证网抓住）

[M9 修复](./2026-09-24-engine-review-medium-m9.md)提交后，收尾复跑 `editor-regression
quick` 抓到 **1/6 全灭**（basic/close/drag/ui 全段 FAIL，手动复现 SIGSEGV 139）。macOS
崩溃报告栈：`RemoveRecreateCallback ← ~ViewportRenderer ← ~EditorApp`——M9 给
SpriteBatcher 加的析构反注册踩了死设备指针。

**根因**：编辑器 `Run()` 收尾**显式** `device_.reset()`（EditorApp.cpp:3996）早于
EditorApp 成员析构——`viewport_`（持 sceneBatcher_/gameBatcher_）在设备已亡之后才析构。
M9 首版 API 注释按"unique_ptr 成员逆序析构"推断注册者天然先亡，**错了**：显式 reset
不走成员序。修复前该拆毁序无害（ViewportRenderer 析构不触碰设备），M9 的主动反注册
把它变成活缺陷——恰是 M9 要修的 UAF 类别的镜像（回调不摘 = 亡主后重建 UAF；本 bug =
亡设备后摘除 UAF）。

**修**：`Run()` 收尾在 `device_.reset()` 前补 `viewport_.reset()`（消费方先于依赖方
拆毁，正确的所有权方向）；RHI.h 契约注释同步改写（显式 reset 不走成员序，反注册方
必须在 Device 存活期析构）。其余注册方（asset-gpu/editor-viewport/样例）无主动反注册，
设备亡后回调永不触发，被动安全不动。

**验证**：`--smoke --frames 120` exit 0 + `editor-smoke PASS`；quick **6/6**。

**教训两条**（本机坑候选）：①管道后 `$?` 是 tail 的退出码——抓真实退出码必须重定向后
单跑（首查时 EXIT=0 假象险些误判方向）；②改析构行为后"成员声明序"不可作为生命周期
依据，显式 reset/Shutdown 路径必须逐处核对——收尾全量回归（而非只跑改动面的 bench）
是唯一可靠的网。
