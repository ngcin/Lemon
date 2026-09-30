# 2026-09-30 CI 首跑修复：script-tests 在 setup-dotnet runner 上找不到 fxr —— CoreCLRHost 探测链补 DOTNET_ROOT

## 事件

M6c 提交上 CI 后 `script-tests` 失败（engine-tests/imgui-isolation 过）：

```
[lemon][warn] no dotnet fxr dir at /usr/local/share/dotnet/host/fxr
script-tests: FAIL script host init (CoreCLR + exports)
```

## 根因

ci.yml（M7 批⓪）用 `actions/setup-dotnet@v4` 装 .NET 10——它把 dotnet 装到**临时目录**并设官方变量 `DOTNET_ROOT` + PATH。构建阶段 `dotnet` CLI 走 PATH 正常（build 过了）；但 `CoreCLRHost::LoadHostfxr` 的根解析链只有 `LEMON_DOTNET_ROOT` → 硬编码 brew 默认 `/usr/local/share/dotnet`，不认 `DOTNET_ROOT` → runner 上 fxr 探测必失败。属 M7 批⓪ DevLog"runner 首跑调通前的观察项"的兑现。

## 修复

`CoreCLRHost.cpp`：解析链补 `DOTNET_ROOT`——**显式参 → `LEMON_DOTNET_ROOT`（引擎专用覆写）→ `DOTNET_ROOT`（dotnet 官方变量，CI/setup-dotnet 装配即设）→ `/usr/local/share/dotnet`（brew 默认）**；告警文案同步。收益面不止 CI：任何自定义安装位置（dev 机/M7a 独立运行时）设 `DOTNET_ROOT` 即可被识别。ci.yml 零改动（setup-dotnet 对后续 step 已导出 `DOTNET_ROOT`）。

## 验证

本机模拟 runner 条件（`env -u LEMON_DOTNET_ROOT DOTNET_ROOT=/usr/local/share/dotnet ctest`）→ **3/3 全绿**（此前该条件下 script-tests 必挂）；默认路径行为不变（本机 brew 装机直通）。
