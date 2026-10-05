# 2026-10-06 Windows 出包链闭环：W3 lavapipe + W4 真人验收通过

## 事件

继 [win-vm-local-build-green](./2026-10-06-win-vm-local-build-green.md) 后，同 VM
完成 M7a 批⑦ W3/W4 两项：**W3 lavapipe 软件 Vulkan 落位**、**W4 出包链真人验收**
（用户桌面双击包体进入游戏，2026-10-06）。

## W3：lavapipe（无 GPU VM 的 Vulkan）

- guest 直连 LunarG SDK 在线组件仓库（maintenancetool add lavapipe）与
  GitHub/dotnet CDN 同款卡死（进程 13min 0.156s CPU、零自连）——继续走
  "mac 下载 → lemonshare 投递"通道。
- 来源：mesa-dist-win 26.2.4 release-msvc（pal1000 GitHub release）。只取
  `x64/vulkan_lvp.dll` + `x64/lvp_icd.x86_64.json` 两件，落
  `C:\VulkanSDK\1.4.363.0\Bin\`（ICD json 的 library_path 为相对路径，须同目录），
  注册 `HKLM\SOFTWARE\Khronos\Vulkan\Drivers`。
- 验证：`vulkaninfoSDK --summary` → `GPU0 = llvmpipe (LLVM 23.1.2, 256 bits)
  api 1.4.354`（SDK 1.4.363 loader 兼容）。

## W4：出包链（vs-survivor 夹具 → 双击即玩）

1. 夹具：模板拷贝 + `dotnet build Game.csproj -o .lemon\bin`。
   **登记**：`Templates/vs-survivor/Game/Game.csproj` 的 HintPath 被本机使用后
   回写为 mac 绝对路径（`/Users/.../build/mac/...`）——Windows 侧需重锚到
   `C:\lemon-build\win\Scripting\dotnet\Lemon.SDK.dll` 才能编。模板 csproj 的
   占位符化/创建期锚定已有机制（ProjectWizard ReanchorSdkHintPath），仓库内
   模板残留本机路径属脏状态，归批⑧ 清理。
2. `lemon-packager --project --runtime --out --force`：
   烤制 7 音频 + 图集 1 页 7 精灵 + self-contained runtime（win-x64，
   publish 离线可用——同平台 SDK 自带 runtime pack）+ 闭包 vulkan-1.dll
   → `pkg-selfcheck: files=309 bytes=108MiB closureMiss=0 essentialMiss=0
   inventoryMiss=0 extra=0 => OK`。
3. 真人面：桌面双击包体进入游戏正常（用户验收 2026-10-06）。

## 产品 bug：CoreCLRHost 形态一 Windows 文件名（本轮修复）

包体 C# 宿主装配失败——hostfxr 从链尾 `DOTNET_ROOT=C:\dotnet` 装载（SDK 布局
形态二），再以 framework-dependent 语义找 hostpolicy 必败（fatal: hostpolicy.dll
not found）。根因：形态一（self-contained 平铺）探测名写死 `libhostfxr` + 平台
后缀 → Windows 拼成 `libhostfxr.dll`，而 publish 产物实为 **`hostfxr.dll`**
（无 lib 前缀，POSIX 命名差异；批⑤ mac 写就批⑦ 只改后缀未去前缀）→ 包内
runtime/ 永远探测落空。修：形态一按平台取 `hostfxr.dll` / `libhostfxr.dylib`。
修复后包内装载 + `initialize_for_dotnet_command_line` 回退链全通。

## 发现：Session 0 GDI present 限制（自动化 smoke 的边界）

`prlctl exec`（SYSTEM，Session 0 无交互桌面）下包体与 rhi-smoke 均 deterministic
崩于首帧 `vkQueuePresentKHR` 返回 -5（llvmpipe wsi_win32 走 GDI，服务会话不可
交互）；桌面会话正常。**对 CI 的启示**：windows runner 同为非交互会话——当前
CI 只跑 ctest（不含 pkg-smoke/渲染 present 路径）未踩；若未来 CI 加渲染/出包
smoke，需 present 路径的 headless 方案（offscreen surface 或软present）先行。

## 遗留

- 模板 csproj 本机路径残留（上记登记，批⑧）。
- CI 对 c26e0bf（MSVC 深水区修复）与本批 hostfxr 修复的状态待用户确认。
- W5 真机 GPU 验收仍需物理 Windows 机。
