cmake_minimum_required(VERSION 3.24)

project(Lemon VERSION 0.1.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Lemon — C++20 / Vulkan 2D 引擎（纯 2D，目标品类：ARPG/塔防/幸存者/增量）
# 设计文档：../docs/EngineDesign/
# 当前阶段：M0 技术验证（spike/）
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  add_compile_options(/utf-8 /permissive-)
  # windows.h 卫生（批⑦ review 实锤②）：min/max 宏会咬 std::min/std::max
  # （GameEntry 1 处/engine_tests 8 处——Process.h 公共头把 windows.h 带进消费方）；
  # LEAN_AND_MEAN 缩小 windows.h 面。全局定义覆盖自有 TU + CPM 子项目（SDL3 的
  # SDL_vulkan.h 在 win 侧也引 windows.h）
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
  # 静态 CRT（M7a 批⑦）：/MT 替默认 /MD——干净 Win 机零 VC redist（vcruntime140.dll
  # 不进包闭包）；CMP0091 NEW = 经 CMAKE_MSVC_RUNTIME_LIBRARY 而非 /M* 裸旗标，
  # CPM 子项目（SDL3/RmlUi/freetype）同步跟随
  cmake_policy(SET CMP0091 NEW)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  # -Wno-missing-field-initializers：Vulkan 惯用 {VK_STRUCTURE_TYPE_X} 部分初始化
  add_compile_options(-Wall -Wextra -Werror=return-type -Wno-missing-field-initializers)
endif()

set(LEMON_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake" CACHE INTERNAL "")

option(LEMON_BUILD_SPIKES "Build M0 spike programs" ON)
option(LEMON_BUILD_TESTS "Build engine unit tests" ON)
option(LEMON_BUILD_SAMPLES "Build engine samples/benches" ON)
option(LEMON_BUILD_TOOLS "M7a 出包工具 lemon-packager（出包时需要 dotnet SDK）" ON)
option(LEMON_BUILD_SCRIPTING "M3 C# 脚本层（需要 dotnet SDK 10；OFF 时引擎其余部分照常）" ON)
option(LEMON_BUILD_EDITOR "M4 编辑器（ImGui docking；lemon-editor + lemon-imgui）" ON)

include(${LEMON_CMAKE_DIR}/CPM.cmake)
include(${LEMON_CMAKE_DIR}/Dependencies.cmake)

add_subdirectory(Engine)

# 编辑器先于 tests：lemon-tests 链接 lemon-editor-core（M4.4 纯逻辑单测面）
if(LEMON_BUILD_EDITOR)
  add_subdirectory(Editor)
endif()

# ctest 入口（enable_testing 必须在顶层目录才对全树生效）
enable_testing()

if(LEMON_BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(LEMON_BUILD_SAMPLES)
  add_subdirectory(Samples)
endif()

# 独立工具目标（M7a 批⑤ lemon-packager——链 lemon-engine，出包时才需 dotnet）
if(LEMON_BUILD_TOOLS)
  add_subdirectory(tools) # 目录名 = git 权威小写 tools/（批⑦核对过，非笔误）
endif()

if(LEMON_BUILD_SPIKES)
  add_subdirectory(spike)
endif()
