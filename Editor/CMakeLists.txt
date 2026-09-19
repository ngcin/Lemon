# Lemon 编辑器（M4-Editor-Plan §3.1/§3.2）
# 纪律：
#   * ImGui 头文件只准出现在本目录（lemon-engine 零 ImGui 引用；tests/imgui_isolation.cmake 断言）
#   * App/ImGuiBackend.cpp 是本目录唯一含 Vulkan 头的 TU（后端 glue 豁免，07 例外登记）
#   * Vulkan 类型不出现在 Editor 其他文件；引擎资源一律经 rhi:: 自有句柄

if(NOT imgui_SOURCE_DIR)
  message(FATAL_ERROR "imgui dependency missing (cmake/Dependencies.cmake)")
endif()
if(NOT stb_SOURCE_DIR)
  message(FATAL_ERROR "stb dependency missing (cmake/Dependencies.cmake)")
endif()

# ---------------------------------------------------------------------------
# lemon-imgui — Dear ImGui 官方源包装（docking 分支 v1.92.9b-docking）
# third-party 源：警告全关（不进项目 -Werror 面）；backend 两 TU 链 SDL3/Vulkan
add_library(lemon-imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp)
target_include_directories(lemon-imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends
                                            ${imgui_SOURCE_DIR}/misc/cpp)
target_link_libraries(lemon-imgui PUBLIC SDL3::SDL3 Vulkan::Vulkan)
target_compile_features(lemon-imgui PUBLIC cxx_std_20)
if(NOT MSVC)
  target_compile_options(lemon-imgui PRIVATE -w)
endif()

# ---------------------------------------------------------------------------
# lemon-editor — 同源双入口的编辑器入口（ADR-005）
add_executable(lemon-editor
  EditorEntry.cpp
  App/EditorApp.cpp
  App/ImGuiBackend.cpp
  EditorContext.cpp
  Panels/Panels.cpp
  Panels/HierarchyPanel.cpp
  Panels/InspectorPanel.cpp
  Panels/ProfilerPanel.cpp
  Panels/ViewportPanels.cpp
  Interaction/ViewportRenderer.cpp
  Tooling/FilePicker.cpp
  Tooling/StbImpl.cpp
  Tooling/UndoStack.cpp)
target_include_directories(lemon-editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${stb_SOURCE_DIR})
target_link_libraries(lemon-editor PRIVATE lemon-engine lemon-imgui)
target_compile_features(lemon-editor PRIVATE cxx_std_20)

if(APPLE)
  # 开窗口目标必带（同 anim-smoke；DevLog P1）：brew 验证层 JSON 裸名 dlopen 靠此命中
  set_target_properties(lemon-editor PROPERTIES
    BUILD_RPATH "/usr/local/lib" INSTALL_RPATH "/usr/local/lib")
endif()
