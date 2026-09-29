# Lemon 编辑器（M4.md §3.1/§3.2）
# 纪律：
#   * ImGui 头文件只准出现在本目录（lemon-engine 零 ImGui 引用；tests/imgui_isolation.cmake 断言）
#   * App/ImGuiBackend.cpp 是本目录唯一含 Vulkan 头的 TU（后端 glue 豁免，07 例外登记）
#   * Vulkan 类型不出现在 Editor 其他文件；引擎资源一律经 rhi:: 自有句柄
# 分层（M4.4）：
#   * lemon-editor-core —— 无 ImGui/无窗口的编辑器逻辑（EditorContext/AssetDatabase/
#     FileWatcher/AssetGpuCache/Undo），lemon-tests 链接它做纯逻辑单测
#   * lemon-editor —— 面板/App 壳（ImGui），链接 core + lemon-imgui

if(NOT imgui_SOURCE_DIR)
  message(FATAL_ERROR "imgui dependency missing (cmake/Dependencies.cmake)")
endif()
if(NOT stb_SOURCE_DIR)
  message(FATAL_ERROR "stb dependency missing (cmake/Dependencies.cmake)")
endif()

# ---------------------------------------------------------------------------
# lemon-editor-core —— 编辑器逻辑层（ImGui-free；AssetGpuCache 仅经 ImGuiBackend.h
# 纯净头调用注册接口，实现链接在 lemon-editor）
add_library(lemon-editor-core STATIC
  EditorContext.cpp
  Assets/AssetDatabase.cpp
  Assets/AssetGpuCache.cpp
  Assets/ClipEdit.cpp
  Assets/ControllerEdit.cpp
  Assets/Csv.cpp
  Assets/FileWatcher.cpp
  Assets/ProjectWizard.cpp
  Tooling/StbImpl.cpp
  Tooling/ThumbCache.cpp
  Tooling/UndoStack.cpp)
target_include_directories(lemon-editor-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(lemon-editor-core PRIVATE ${stb_SOURCE_DIR})
target_link_libraries(lemon-editor-core
  # json 头路径随 PUBLIC 传给上层（M4.6：recent.json 在 App 层读写）
  PUBLIC lemon-engine nlohmann_json::nlohmann_json)
target_compile_features(lemon-editor-core PUBLIC cxx_std_20)

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
  App/EditorAppActions.cpp
  App/EditorAppChrome.cpp
  App/EditorAppScripts.cpp
  App/RecentProjects.cpp
  App/ImGuiBackend.cpp
  Panels/Panels.cpp
  Panels/AnimationPanel.cpp
  Panels/AssetBrowserPanel.cpp
  Panels/HierarchyPanel.cpp
  Panels/InspectorPanel.cpp
  Panels/ProfilerPanel.cpp
  Panels/ViewportPanels.cpp
  Interaction/ViewportRenderer.cpp
  Templates/VsTemplateGen.cpp
  Tooling/FilePicker.cpp
  Tooling/Theme.cpp)
target_include_directories(lemon-editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${stb_SOURCE_DIR})
target_link_libraries(lemon-editor PRIVATE lemon-editor-core lemon-imgui)
target_compile_features(lemon-editor PRIVATE cxx_std_20)

# M4.4 脚本宿主：--script 需要 LEMON.Entry 产物路径宏（无脚本层构建 = 该参数降级告警）
if(LEMON_BUILD_SCRIPTING)
  target_link_libraries(lemon-editor PRIVATE lemon-csharp)
  add_dependencies(lemon-editor lemon-dotnet-asm)
endif()

# M5 批④：模板项目源根（向导 vs-survivor 分支 / --gen-vs-template / --smoke-template）
target_compile_definitions(lemon-editor PRIVATE
  LEMON_TEMPLATE_DIR="${CMAKE_SOURCE_DIR}/Templates")

# M6a 批③b（ADR-014）：引擎 UI 正字随仓库（Noto Sans SC，OFL——Engine/Ui/Fonts；
# EditorApp 经此宏装载为 RmlUi fallback 字体；打包 runtime 的字体分发形态归 M8）
target_compile_definitions(lemon-editor PRIVATE
  LEMON_ENGINE_FONT_DIR="${CMAKE_SOURCE_DIR}/Engine/Ui/Fonts")

if(APPLE)
  # 开窗口目标必带（同 anim-smoke；DevLog P1）：brew 验证层 JSON 裸名 dlopen 靠此命中
  set_target_properties(lemon-editor PROPERTIES
    BUILD_RPATH "/usr/local/lib" INSTALL_RPATH "/usr/local/lib")
endif()
