# M5 ADR-008 spike：RmlUi 官方 SDL3+Vulkan 后端拷贝改造（官方文档推荐姿势）
# 验收：① 文档渲染 ② 中文字体（系统 PingFang）③ 按钮事件回调
# 改造点（backends/ 自官方 6.3 拷贝，仅 RmlUi_Include_Vulkan.h 一处——见其内注释）：
# mac 上 vendored vulkan.h 走 XCB 分支（官方后端未覆盖 macOS）→ 改系统
# vulkan-headers。正式接入（spike 过后）将把 Renderer_VK 思路适配引擎 RHI。
add_executable(lemon-spike-rmlui
  main.cpp
  backends/RmlUi_Backend_SDL_VK.cpp
  backends/RmlUi_Platform_SDL.cpp
  backends/RmlUi_Renderer_VK.cpp
)
target_include_directories(lemon-spike-rmlui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/backends)

target_compile_definitions(lemon-spike-rmlui PRIVATE RMLUI_SDL_VERSION_MAJOR=3)

target_link_libraries(lemon-spike-rmlui PRIVATE
  RmlUi::RmlUi
  SDL3::SDL3
)

if(APPLE)
  set_target_properties(lemon-spike-rmlui PROPERTIES
    BUILD_RPATH "/usr/local/lib" INSTALL_RPATH "/usr/local/lib")
endif()
