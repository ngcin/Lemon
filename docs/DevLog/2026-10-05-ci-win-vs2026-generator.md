# CI win job 首跑热修② —— windows-2025 镜像已换代 VS2026，CI 生成器分叉（2026-10-05，W7 迭代）

事件：SDK 安装热修后重跑，`win-build-test` 过了 SDK 三步，在 Configure 步红：

```
CMake Error at CMakeLists.txt:3 (project):
  Generator
    Visual Studio 17 2022
  could not find any instance of Visual Studio.
```

根因：GitHub `windows-2025` 镜像 **2026-06-15 官方迁移后内置 VS2026**（VS2026-only
变体，VS2022 不再随镜像）——`win` preset 的 `"Visual Studio 17 2022"` 生成器在镜像
上无实例可寻。写批⑦ CI 时按"windows-2025 = VS2022"的旧认知选的镜像，撞上换代
（node-libcurl 等项目同期同症）。

拍板：**CI 换 VS2026 生成器，真机口径不动**——

- 新 `win-ci` preset（inherits `win`，仅覆写 `generator = "Visual Studio 18 2026"`
  ，binaryDir 同 build/win）+ 配套 build preset；ci.yml Configure/Build 两步切
  `win-ci`，ctest 步不变。生成器需 CMake ≥ 4.2（4.2 起支持，runner 自带版本满足）。
- `win` preset 保持 VS2022 = 真机 W1 口径（用户真机装的就是 VS2022）。CI（VS2026
  toolset）/真机（VS2022 toolset）双 MSVC 版本各自把门——07 §3.6 "MSVC 实际编译"
  的机器门禁由 CI 承担，真机清单另证 VS2022 面。
- 备选记录（不取）：装 VS Build Tools 2022（数 GB、慢且引入 choco 抖动面）；改
  `windows-2022` 镜像（退役窗口已过，不可依赖）。回答用户"必须要 Visual Studio 吗"：
  硬需求是 MSVC 工具链（cl + Windows SDK）而非 VS IDE，但镜像现成可用的是内置
  VS 实例，自装不划算。

观察项状态：①latest URL ✅（前条）；②安装目录发现 ✅（SDK 三步全绿）；③长路径
未触发。下一关 = MSVC 全目标首编 + ctest 4/4。
