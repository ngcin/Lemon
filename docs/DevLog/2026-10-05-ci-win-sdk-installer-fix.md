# CI win job 首跑热修 —— Vulkan SDK 安装器静默语法换代（2026-10-05，W7 首跑实抓）

事件：批⑦ push 后用户手动 dispatch `ci`，`win-build-test` 首跑在 Install Vulkan SDK
步失败。下载面绿（`latest` URL 直下 289MB @ ~90MB/s——批⑦ 观察项①"latest 断供
风险"排除）；安装面红：

```
Unknown option: mode, accept-all-licenses
[0] Warning: Found command line only option "default-answer". This will not
have any effect when running in graphical mode.
```

根因：LunarG 新版 Windows 安装器已从 BitRock InstallBuilder 换 **Qt Installer
Framework**——`--mode unattended --accept-all-licenses --default-answer yes` 是旧
Builder 语法，新框架不认 `--mode`/`--accept-all-licenses`（报文出自 IFW），且无
子命令时进图形模式（`--default-answer` 被警告"command line only"）。批⑦ 写 CI
时按 2025 前的 InstallBuilder 资料写的，没赶上换代。

修法（官方 getting-started 口径 + stable-diffusion.cpp 等 GH 工作流交叉实证）：

```
.\VulkanSDK.exe --accept-licenses --default-answer --confirm-command install
```

- `install` 子命令 = 命令行安装模式；三旗标 = 全默认应答免交互；默认落位不变
  （`C:\VulkanSDK\<version>\`），Resolve 步目录发现/`Include\vulkan\vulkan.h`
  校验零改动。
- 缓存面无需动：首跑安装步失败 → job 红 → `actions/cache` post 不保存，无脏缓存。
- ci.yml 两处注释同步纠正（InstallBuilder → Qt IFW），观察项①勾销。

观察项②（安装目录版本号发现）/③（长路径）随重跑 dispatch 验证；绿 = W7 收口，
07 §3.6 "机器门禁"半句勾销。
