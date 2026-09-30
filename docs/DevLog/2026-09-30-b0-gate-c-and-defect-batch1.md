# M7 批⓪：Gate C 清障 + 缺陷第一批修复 + demo 入 git（回归 16/16）

- **日期**：2026-09-30；批文件与任务分解见 [Plans/M7/2026-09-30-b0-gate-c-and-defect-batch1.md](../Plans/M7/2026-09-30-b0-gate-c-and-defect-batch1.md)（本条目记事件与实测）。

## 实测

- 构建：`cmake --preset mac` + build 零错误（唯一 warning = `EditorAppSmoke.cpp:1787` unused parameter，改动前既存）。
- 回归：`tools/editor-regression.sh full` **16/16 PASS**（ctest 3/3 + 编辑器冒烟 13 步；修复后复跑二次确认）。
- D10 CMake 护栏阴性验证：`uAtlases[128]` → configure 期 `D10 guard: sprite.frag uAtlases[128] != RHI.h kMaxTextureSlots = 256` FATAL；还原后配置通过。

## 事件与坑

- **SDL 3.2.14 无 `SDL_GetPID`**（3.4 起才有）——Windows 阻断① 第一版用 SDL 方案编译失败（`use of undeclared identifier`），改自带 `Engine/Core/Process.h`（POSIX `getpid` / Win `GetCurrentProcessId` 头文件内联）。教训：07 §3.6 的"SDL/条件宏"处置方向在当前 SDL 版本不可行。
- **D3 修复时序确认**：batcher 自登记回调的注册序在 `editor-viewport` 回调之前（`ViewportRenderer::Init` :337→:343），设备丢失时先自重建、后进 `OnDeviceRecreated`——后者重 Init 纯属重复（覆盖活资源），直接删除该两行 + `Init()` 幂等化双保险。
- **D2 修法**：调用点"重建成功也 `continue`"为主修（六处同构），RHI 层 `imageIndex=UINT32_MAX` 哨兵 + `EndFrameAndPresent` 断言为防御（新调用方误用时响亮失败而非静默 UB）。
- **demo/ 入 git 的 ignore 手术比预期干净**：`.lemon/`、`[Bb]in/[Oo]bj/`、`.DS_Store` 既有规则已覆盖全部生成物（saves/manifest/dotnet 中间态），摘除整目录忽略一行即收口；`demo/test` 内还有项目自带 `.gitignore`。

## 尾巴（移交后续批）

1. Windows 真机编译验证待首次（MSVC 侧只保证了 macOS 不回归）；
2. CI runner 首跑调通待 push（brew/CPM/ctest 无 GPU 形态按 09 §9 预期为逻辑面门禁）；
3. IME 预编辑"可达"冒烟断言未做（归语义句柄改造合并考虑）；
4. 每日回归 + 性能基线门禁 + Windows CI runner（09 §9 全量口径）归 M7a 批。
