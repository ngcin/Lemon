# M0-W3：CoreCLR 宿主闭环 + Collectible ALC 热重载 + EnTT 固定步长
# Go/No-Go 判据之三：C# 批量通道开销可接受 + 热重载（卸载域）可行
find_program(DOTNET dotnet REQUIRED)
find_package(Threads REQUIRED)

set(DOTNET_SRC ${CMAKE_CURRENT_SOURCE_DIR}/dotnet)
set(DOTNET_OUT ${CMAKE_CURRENT_BINARY_DIR}/dotnet)

file(GLOB_RECURSE LEMON_CS_SOURCES ${DOTNET_SRC}/*.cs)

add_custom_command(
  OUTPUT ${DOTNET_OUT}/.stamp
  COMMAND ${CMAKE_COMMAND} -E make_directory ${DOTNET_OUT}
  COMMAND ${DOTNET} build ${DOTNET_SRC}/Script -c Release -o ${DOTNET_OUT}
  COMMAND ${DOTNET} build ${DOTNET_SRC}/LemonSpike -c Release -o ${DOTNET_OUT}
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/LemonSpike.runtimeconfig.json ${DOTNET_OUT}/LemonSpike.runtimeconfig.json
  COMMAND ${CMAKE_COMMAND} -E touch ${DOTNET_OUT}/.stamp
  DEPENDS ${LEMON_CS_SOURCES} ${DOTNET_SRC}/Script/Script.csproj ${DOTNET_SRC}/LemonSpike/LemonSpike.csproj
  COMMENT "dotnet build (Script + LemonSpike)"
  VERBATIM)

add_executable(lemon-spike-csharp main.cpp ${DOTNET_OUT}/.stamp)
target_include_directories(lemon-spike-csharp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lemon-spike-csharp PRIVATE EnTT::EnTT Threads::Threads ${CMAKE_DL_LIBS})
target_compile_definitions(lemon-spike-csharp PRIVATE LEMON_DOTNET_DIR="${DOTNET_OUT}")
