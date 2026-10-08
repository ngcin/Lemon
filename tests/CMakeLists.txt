# 引擎纯逻辑单测（无窗口/无 Vulkan，可进 CI 冒烟）
# M4.4 起：lemon-editor-core（EditorContext/AssetDatabase 等无 ImGui 编辑器逻辑）入测
# M7c 批⓪ T2：单 TU engine_tests.cpp（7,939 行/139 函数）按域拆 tests/engine/ 七 TU +
# TestMain.cpp；函数体原样搬运，checks 总数不变（34,346 硬门）
add_executable(lemon-tests
  engine/TestMain.cpp
  engine/CoreTests.cpp
  engine/RendererTests.cpp
  engine/EcsTests.cpp
  engine/SceneTests.cpp
  engine/GameplayTests.cpp
  engine/AssetsTests.cpp
  engine/EditorTests.cpp
  engine/AudioTests.cpp)
target_link_libraries(lemon-tests PRIVATE lemon-engine)
# M7c 批①：字体烘焙 golden 单测的 TTF 源（引擎随仓 Noto Sans SC；编辑器字体链同源）
target_compile_definitions(lemon-tests PRIVATE
  LEMON_ENGINE_FONT_DIR="${CMAKE_SOURCE_DIR}/Engine/Ui/Fonts")
# stb 头面（M7a 批⑥）：LAT1 单测播种 PNG 用 stb_image_write（实现符号已在
# lemon-engine 的 StbImage.cpp 单 TU，此处只补头路径——无第二实现 TU）
if(NOT stb_SOURCE_DIR)
  message(FATAL_ERROR "stb dependency missing (cmake/Dependencies.cmake)")
endif()
target_include_directories(lemon-tests PRIVATE ${stb_SOURCE_DIR})
if(LEMON_BUILD_EDITOR)
  target_link_libraries(lemon-tests PRIVATE lemon-editor-core)
  target_compile_definitions(lemon-tests PRIVATE LEMON_EDITOR_CORE=1)
endif()

enable_testing()
# TIMEOUT 护栏：挂死快速失败而非无限等（2026-09-21 JobSystem 丢唤醒死锁实抓教训——
# ctest 干等 25 分钟；正常 <30s）
add_test(NAME engine-tests COMMAND lemon-tests)
set_tests_properties(engine-tests PROPERTIES TIMEOUT 300)

# M4 纪律断言：ImGui 头文件只准出现在 Editor/（M4.md §5 M4.0 验收项）
add_test(NAME imgui-isolation
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/imgui_isolation.cmake)

# M3 脚本桥测试（需要 dotnet）
if(LEMON_BUILD_SCRIPTING)
  add_subdirectory(script)
endif()
