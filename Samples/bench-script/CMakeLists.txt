# M3 验收场 bench-script（环绕弹幕；08 §3 / ADR-010 修订判据）
# 托管侧：BenchScript（用户程序集，独立 dotnet build 到本目录构建树）
find_program(LEMON_DOTNET dotnet REQUIRED)

set(BENCH_SCRIPT_SRC ${CMAKE_CURRENT_SOURCE_DIR}/script)
set(BENCH_SCRIPT_OUT ${CMAKE_CURRENT_BINARY_DIR}/script)

file(GLOB_RECURSE BENCH_CS_SOURCES CONFIGURE_DEPENDS ${BENCH_SCRIPT_SRC}/*.cs)

# UNC 源码路径（\\Mac 共享构建）下 //server/... 正斜杠形式会被 dotnet→MSBuild 当开关
# （MSB1001，同 Engine/Scripting/CMakeLists.txt）；posix 平台 TO_NATIVE_PATH 原样返回
file(TO_NATIVE_PATH "${BENCH_SCRIPT_SRC}" BENCH_SCRIPT_PROJ)

add_custom_command(
  OUTPUT ${BENCH_SCRIPT_OUT}/.stamp
  COMMAND ${CMAKE_COMMAND} -E make_directory ${BENCH_SCRIPT_OUT}
  COMMAND ${LEMON_DOTNET} build ${BENCH_SCRIPT_PROJ} -c $<CONFIG> -o ${BENCH_SCRIPT_OUT}
  COMMAND ${CMAKE_COMMAND} -E touch ${BENCH_SCRIPT_OUT}/.stamp
  DEPENDS ${BENCH_CS_SOURCES} ${BENCH_SCRIPT_SRC}/BenchScript.csproj
  COMMENT "dotnet build (BenchScript)"
  VERBATIM)

add_custom_target(lemon-benchscript-asm DEPENDS ${BENCH_SCRIPT_OUT}/.stamp)

add_executable(lemon-bench-script main.cpp)
add_dependencies(lemon-bench-script lemon-benchscript-asm lemon-dotnet-asm) # SDK/Entry 产物同依赖
target_link_libraries(lemon-bench-script PRIVATE lemon-csharp)
target_compile_definitions(lemon-bench-script PRIVATE
  LEMON_SCRIPT_DIR_BENCH="${BENCH_SCRIPT_OUT}")

# 程序集查找顺序：bench 目录（BenchScript.dll）优先，SDK 目录（Lemon.Entry）兜底——
# main.cpp 用 LEMON_SCRIPT_DIR_BENCH 拼 BenchScript.dll，用 LEMON_SCRIPT_DIR 拼 Entry
