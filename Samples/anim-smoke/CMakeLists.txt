# M3.5 集成冒烟 anim-smoke（ECS→Extract→Renderable→窗口 首次打通；C# 档① 换帧视觉验收）
# 托管侧：AnimSmoke（用户脚本程序集，独立 dotnet build 到本目录构建树，同 bench-script 分层）
find_program(LEMON_DOTNET dotnet REQUIRED)

set(ANIM_SCRIPT_SRC ${CMAKE_CURRENT_SOURCE_DIR}/script)
set(ANIM_SCRIPT_OUT ${CMAKE_CURRENT_BINARY_DIR}/script)

file(GLOB_RECURSE ANIM_CS_SOURCES CONFIGURE_DEPENDS ${ANIM_SCRIPT_SRC}/*.cs)

add_custom_command(
  OUTPUT ${ANIM_SCRIPT_OUT}/.stamp
  COMMAND ${CMAKE_COMMAND} -E make_directory ${ANIM_SCRIPT_OUT}
  COMMAND ${LEMON_DOTNET} build ${ANIM_SCRIPT_SRC} -c $<CONFIG> -o ${ANIM_SCRIPT_OUT}
  COMMAND ${CMAKE_COMMAND} -E touch ${ANIM_SCRIPT_OUT}/.stamp
  DEPENDS ${ANIM_CS_SOURCES} ${ANIM_SCRIPT_SRC}/AnimSmoke.csproj
  COMMENT "dotnet build (AnimSmoke)"
  VERBATIM)

add_custom_target(lemon-anims-asm DEPENDS ${ANIM_SCRIPT_OUT}/.stamp)

add_executable(lemon-anim-smoke main.cpp)
add_dependencies(lemon-anim-smoke lemon-anims-asm lemon-dotnet-asm) # SDK/Entry 产物同依赖
target_link_libraries(lemon-anim-smoke PRIVATE lemon-csharp)
target_compile_definitions(lemon-anim-smoke PRIVATE
  LEMON_SCRIPT_DIR_ANIM="${ANIM_SCRIPT_OUT}")

if(APPLE)
  # 开窗口的 sample 必带（同 rhi-smoke/bench-sprites）：brew 验证层 JSON 的 library_path
  # 是裸名，loader 按裸名 dlopen，靠这条 RPATH 命中 /usr/local/lib 下 brew 的 dylib 符号
  # 链接；缺它 = vkCreateInstance 报 VK_ERROR_LAYER_NOT_PRESENT(-6)。headless sample 无此需。
  set_target_properties(lemon-anim-smoke PROPERTIES
    BUILD_RPATH "/usr/local/lib" INSTALL_RPATH "/usr/local/lib")
endif()

