# M3 脚本层 —— 托管程序集构建 + 消费接线
# CoreCLRHost.cpp 常规编入 lemon-engine（见 Engine/CMakeLists.txt）；本文件只管：
#   1. dotnet build（Lemon.SDK + Lemon.Entry → ${CMAKE_BINARY_DIR}/Scripting/dotnet）
#   2. lemon-csharp INTERFACE 目标：携带 .stamp 依赖与 LEMON_SCRIPT_DIR 宏，
#      消费者（tests/script、后续 bench-script）链接它即得程序集产物与路径。
find_program(LEMON_DOTNET dotnet)

if(NOT LEMON_DOTNET)
  message(FATAL_ERROR
    "LEMON_BUILD_SCRIPTING=ON 但未找到 dotnet（M3 脚本层需要 .NET SDK 10）。\n"
    "  安装: brew install dotnet-sdk\n"
    "  自定义位置: export LEMON_DOTNET_ROOT=/path/to/dotnet\n"
    "  或关闭脚本层: cmake -DLEMON_BUILD_SCRIPTING=OFF（引擎其余部分可构建，脚本目标不生成）")
endif()

set(LEMON_SCRIPT_SRC ${CMAKE_CURRENT_SOURCE_DIR}/dotnet)
set(LEMON_SCRIPT_DIR ${CMAKE_BINARY_DIR}/Scripting/dotnet CACHE INTERNAL
    "Lemon 托管程序集输出目录")

# CONFIGURE_DEPENDS：新增 .cs 文件时自动重跑 glob（否则 ninja 依赖表不含新文件——实测坑）
file(GLOB_RECURSE LEMON_CS_SOURCES CONFIGURE_DEPENDS ${LEMON_SCRIPT_SRC}/*.cs)

# 源码位于 UNC 路径（如 Parallels \\Mac 共享上构建）时，CMake 以 //server/share
# 正斜杠形式传参，dotnet 转发给 MSBuild 会把开头的 / 解析成开关（MSB1001）。
# 转 native 分隔符规避；posix 平台 TO_NATIVE_PATH 原样返回，不影响 mac 构建。
file(TO_NATIVE_PATH "${LEMON_SCRIPT_SRC}/Lemon.SDK"   LEMON_DOTNET_PROJ_SDK)
file(TO_NATIVE_PATH "${LEMON_SCRIPT_SRC}/Lemon.Entry" LEMON_DOTNET_PROJ_ENTRY)
file(TO_NATIVE_PATH "${LEMON_SCRIPT_SRC}/TestScript"  LEMON_DOTNET_PROJ_TEST)

add_custom_command(
  OUTPUT ${LEMON_SCRIPT_DIR}/.stamp
  COMMAND ${CMAKE_COMMAND} -E make_directory ${LEMON_SCRIPT_DIR}
  COMMAND ${LEMON_DOTNET} build ${LEMON_DOTNET_PROJ_SDK} -c $<CONFIG> -o ${LEMON_SCRIPT_DIR}
  COMMAND ${LEMON_DOTNET} build ${LEMON_DOTNET_PROJ_ENTRY} -c $<CONFIG> -o ${LEMON_SCRIPT_DIR}
  COMMAND ${LEMON_DOTNET} build ${LEMON_DOTNET_PROJ_TEST} -c $<CONFIG> -o ${LEMON_SCRIPT_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy
          ${LEMON_SCRIPT_SRC}/Lemon.Entry/Lemon.Entry.runtimeconfig.json
          ${LEMON_SCRIPT_DIR}/Lemon.Entry.runtimeconfig.json
  COMMAND ${CMAKE_COMMAND} -E touch ${LEMON_SCRIPT_DIR}/.stamp
  DEPENDS ${LEMON_CS_SOURCES}
          ${LEMON_SCRIPT_SRC}/Lemon.SDK/Lemon.SDK.csproj
          ${LEMON_SCRIPT_SRC}/Lemon.Entry/Lemon.Entry.csproj
          ${LEMON_SCRIPT_SRC}/TestScript/TestScript.csproj
  COMMENT "dotnet build (Lemon.SDK + Lemon.Entry + TestScript)"
  VERBATIM)

# custom_target 而非裸 custom_command：OUTPUT 规则跨目录消费不进生成图（实测坑），
# custom_target 强制生成且 ALL 默认构建；消费者 add_dependencies 即得。
add_custom_target(lemon-dotnet-asm ALL DEPENDS ${LEMON_SCRIPT_DIR}/.stamp)

add_library(lemon-csharp INTERFACE)
target_compile_definitions(lemon-csharp INTERFACE LEMON_SCRIPT_DIR="${LEMON_SCRIPT_DIR}")
target_link_libraries(lemon-csharp INTERFACE lemon-engine)
