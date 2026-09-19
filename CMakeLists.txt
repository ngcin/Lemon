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
else()
  # -Wno-missing-field-initializers：Vulkan 惯用 {VK_STRUCTURE_TYPE_X} 部分初始化
  add_compile_options(-Wall -Wextra -Werror=return-type -Wno-missing-field-initializers)
endif()

set(LEMON_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake" CACHE INTERNAL "")

option(LEMON_BUILD_SPIKES "Build M0 spike programs" ON)
option(LEMON_BUILD_TESTS "Build engine unit tests" ON)
option(LEMON_BUILD_SAMPLES "Build engine samples/benches" ON)
option(LEMON_BUILD_SCRIPTING "M3 C# 脚本层（需要 dotnet SDK 10；OFF 时引擎其余部分照常）" ON)
option(LEMON_BUILD_EDITOR "M4 编辑器（ImGui docking；lemon-editor + lemon-imgui）" ON)

include(${LEMON_CMAKE_DIR}/CPM.cmake)
include(${LEMON_CMAKE_DIR}/Dependencies.cmake)

add_subdirectory(Engine)

# ctest 入口（enable_testing 必须在顶层目录才对全树生效）
enable_testing()

if(LEMON_BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(LEMON_BUILD_SAMPLES)
  add_subdirectory(Samples)
endif()

if(LEMON_BUILD_EDITOR)
  add_subdirectory(Editor)
endif()

if(LEMON_BUILD_SPIKES)
  add_subdirectory(spike)
endif()
