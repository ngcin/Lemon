# 2026-10-06 W6 行为表收口：codepage 实抓三修 + ①③④ 机器面全过

## 事件

[批⑦ 真机清单](./2026-10-05-m7a-b7-windows-closure.md) W6：07 §3.5 行为表四项 +
中文路径 codepage。环境 = Parallels zh-CN VM（Win11 LTSC，ACP=936/GBK）+ lavapipe
（[VM 构建链](./2026-10-06-win-vm-local-build-green.md)、
[W3/W4](./2026-10-06-win-vm-package-verify.md) 同场）。执行通道：prlctl exec
（Session 0）做夹具/构建/收证 + 计划任务（`Register-ScheduledTask -LogonType
Interactive`）投 GUI 到 `ai` 交互会话 + `prlctl capture` 截屏。夹具 =
`C:\lemon-work\中文测试`（svr-test 拷贝 595 文件，csproj HintPath 重锚
`C:\lemon-build\win\Scripting\dotnet\Lemon.SDK.dll`——模板 mac 路径残留为已知批⑧ 项）。

## 四项结果

| 项 | 结果 |
|---|---|
| ① CJK 窗口标题 | **功能通、显示残**：编辑器完整可用（263 资产/19fps llvmpipe/全面板），标题 = `untitled.scene — ◆?◆\U?◆? — Lemon`——场景名（数据文件 UTF-8）正常，**项目名乱码**：窄 argv GBK 字节被 SDL（期望 UTF-8）解码。显示层已知限制归 M8（07 §3.6 伴生项） |
| ② 拖 PNG / FilePicker 手输 `C:\` | **真人全过 2026-10-06**：拖入 ASCII 导入 ✓（guid `afe4b8…`）/ CJK 名红字拒绝不崩 ✓（首拖即崩揪出两层闪退链，下节）；FilePicker 手输 `C:\` 回车直达 ✓ |
| ③ 关闭按钮 | `--smoke-close clean` **OK**（exitedEarly=1 confirmShown=0，30 帧退出，与 mac 无差异） |
| ④ 中文路径 codepage | **dotnet 编译通、装载曾断、修后全通**——见下 |

## codepage 实测结论（zh-CN/GBK）

**GBK 窄链整体往返成立**：`GetCommandLineA`→窄 argv(GBK)→`fs::path(std::string)`
（win 按 ACP 解窄串）→目录迭代/写文件全通；`_popen("dotnet build \"…中文…\"")`
经 CRT ACP→`CreateProcessW` 宽往返同样成立（5.2–6.7s 编译，Game.dll 落盘正确）。
07 阻断项④ 的"行为面未清"就此收口（zh-CN 口径；en-US+中文路径仍 M8）。

破口全在**"窄 ACP 字节流进 UTF-8 假设层"**的三个边界，`--play`（EnterPlay→
dotnet build→CoreCLR 装载）一次全暴露：

### 修复①（⑬）：CoreCLRHost 路径宽化 ACP 化

批⑦/review 的 `Widen(CP_UTF8)` 假设输入 UTF-8；win 路径入参（argv/fs 派生）实为
ACP。新增 `WidenAcp`（CP_ACP）用于四路径位（LoadLibraryW/wCfg/wEntry/wAsm），
`Widen` 保留给 ASCII 标识符位。本夹具四处全 ASCII = 不可见位（防御性一致性修正，
证据在②③）。

### 修复②（⑭）：FileOps RenameReplace + FsyncFile

- `RenameReplace` widen CP_UTF8→**CP_ACP**（同病根）：修前中文项目
  `manifest.json 写入失败`（tmp 写成功、终名原子换名失败）→ 修后消失。
- `FsyncFile` `_O_RDONLY`→**`_O_RDWR`**：`_commit`=FlushFileBuffers 要求写句柄，
  只读 fd 必 ACCESS_DENIED（POSIX `fsync(O_RDONLY)` 合法 → mac 永不暴露）。修前
  durable 写恒告警 → 修后静默通过。
- 新增 `AcpToUtf8()`（FileOps.h；win 双跳转换 / POSIX 恒等）。

### 修复③（⑮）：C++/C# 边界路径归一

`Exports.cs lemon_dm_load/reload` 按 `Encoding.UTF8.GetString(byte*)` 解码（边界
契约 = UTF-8，mac 天然满足）；win GBK 路径直传 → 托管解出 `???` →
`LoadFromAssemblyPath` FileNotFound。修 = `ScriptHost.cpp` 两调用点过
`AcpToUtf8()` 归一再过界（**契约不动、C# 零改动**）。diag 三导出同款留待需要。

### 证据链（三轮 --play 同夹具）

| 轮 | 二进制 | 症状 |
|---|---|---|
| 1 | 修复前 | manifest 写失败 + fsync 告警 + `脚本装配失败 FileNotFound '???????'` |
| 2 | +⑬⑭ | manifest 写失败消失；fsync 告警 + 装配失败仍在（⑭ 换名位阳性、⑮ 阴性） |
| 3 | +⑮+fsync | **`脚本宿主就绪：…/中文测试/.lemon/bin/Game.dll（类型 8 个）`**，300 帧 Play 完整退出，告警双清 |

## W6-② 拖入闪退链（真人首拖即崩，两层叠加；⑯⑰）

首拖中文名 PNG → 编辑器闪退（NT 堆 `0xc0000374`）。修一层露一层，共两层 + 一个
入口归一，全部机器/真人双验证：

### 层一（⑯ 全平台 UB）：SDL2 遗风双 free

`Window.cpp` 对 `SDL_EVENT_DROP_FILE.data` 调 `SDL_free`——SDL2 的规矩；SDL3 改为
`SDL_CreateTemporaryString` 临时串（`SDL_dropevents.c:64`，事件队列排水时 SDL 经
TLS 临时内存机制自回收，`SDL_events.c:1436`）。应用再 free = 双 free：win NT 堆即
报 `0xc0000374`；macOS 分配器对小对象双 free 常不报 = M4.6b 起一直"侥幸绿"（实为
UB）。修 = 删 SDL_free（即拷即存保留）。

### 层二（⑰ win CJK 资产名）：manifest 序列化炸死编辑器

层一修后重拖：`0xc0000409`（fastfail/abort）且**启动重扫同崩**（脏文件已在盘）。
取证 = WER LocalDumps（注册表 DumpType=2）+ 链接器 `/MAP`（`Editor/CMakeLists.txt`
MSVC 分支常挂）→ mac 侧 python 解 minidump：异常线程 RIP `exe+0x60a241`、栈符号化
= `SaveManifest+0xc78 → nlohmann dump()/dump_escaped → _CxxThrowException →
terminate → abort`；dump 内存中错误串 `[json.exception.type_error.316] invalid
UTF-8 byte at index 11: 0xCF`——`Assets/W6-拖入测试.png` 的 GBK 编码（`拖`=CDCF）
分毫吻合。因果：中文文件名经 fs 窄链（ACP）入库为 GBK 字节 → manifest `dump()`
严格 UTF-8 校验抛异常未捕获 → terminate。修：

- `ImportFile` win 侧入口拒绝非 ASCII `relDest`（红字指路；CJK 资产名全链归 M8，
  07 既有登记——半吊子"能导入"只会制造 GBK 脏数据）；
- `SaveManifest` dump 包 catch：序列化失败红字跳过落盘——**数据不允许杀死编辑器**
  （`.meta` 不含路径字段无同款面）；
- 拖入路径 `Utf8ToAcp` 归一（`EditorAppActions.cpp`）：SDL drop 按 UTF-8 契约进，
  win `fs::path(窄串)` 按 ACP 解——不归一中文源连 `is_regular_file` 都误判。

### 验证

- 真人（2026-10-06）：`W6-ascii.png` 拖入导入 ✓（`拖入导入：Assets/W6-ascii.png
  (guid afe4b8fb3d961204)`）；`W6-拖入测试.png` 拖入三次均红字拒绝（控制台面板）
  不崩 ✓。曾致的启动重扫崩溃循环 = 夹具删脏文件清场。
- 门格：mac ctest 4/4；VM 全量重建零错 + ctest 4/4。

## 门格

- mac：构建 ✓ + ctest 4/4 + **回归 full 19/19 首跑全绿**
- VM：全目标重建 ✓ + ctest 4/4（engine-tests/imgui-isolation/script-tests/pkg-pe-selftest）
- 单测零增（转换函数为平台边界件，POSIX 恒等分支无断言价值；验证 = 端到端三轮）

## 顺带观察（登记不扩项）

- 编辑器 llvmpipe 首帧冷管线编译 ~90s 烧 3 核（CPU 196→349 核秒/45s），之后 19fps
  稳态——W4 包体 `--smoke` 428fps 是无 UI 直渲染面，编辑器面首次量化。
- cwd 在不可写目录（计划任务默认 system32）时 pipeline-cache 写仅告警无害——
  编辑器 cwd 相对路径已知项（现状盘点 #6，包形态已显式传参不受影响）。
- 系统字体链 win 侧 mac 路径三连 error 后回退引擎 Noto（共享路径）正常。
- 崩溃恢复弹窗（夹具带 Autosave）在 `--frames>0` 门控下正确跳过（M6b 热修④ 语义
  在 win 侧同表现）。

## 遗留

- 窗口标题/日志项目名显示乱码：M8 宽入口（wmain + fs::u8path 全链）一并收口。
- en-US 系统 + 中文路径：argv 窄化即丢字，同归 M8。
- CJK 资产名（拖入/FilePicker/资产浏览器全链）：入口拒绝为过渡口径，全链支持 M8。
- W7 CI：run #10 = SUCCESS（`63a842e` win-build-test 首绿，2026-10-06 14:33 UTC）。
- 取证装备沿用：LocalDumps 注册表项 + `/MAP` 常挂（下次崩溃免调试器出栈）。
- 回归抖动观察（本日 6 轮）：失败位单探针轮换（drag/ui → anim multiadd → final
  hr），与改动无耦合；final "FAIL 项"行印反已修（`EditorAppFinal.cpp`，1=挂）。
  smoke 注入抖动机器化收口归批⑧（§8 既有登记扩面到 anim/final 位）。
