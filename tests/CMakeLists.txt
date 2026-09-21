# 引擎纯逻辑单测（无窗口/无 Vulkan，可进 CI 冒烟）
# M4.4 起：lemon-editor-core（EditorContext/AssetDatabase 等无 ImGui 编辑器逻辑）入测
add_executable(lemon-tests engine_tests.cpp)
target_link_libraries(lemon-tests PRIVATE lemon-engine)
if(LEMON_BUILD_EDITOR)
  target_link_libraries(lemon-tests PRIVATE lemon-editor-core)
  target_compile_definitions(lemon-tests PRIVATE LEMON_EDITOR_CORE=1)
endif()

enable_testing()
# TIMEOUT 护栏：挂死快速失败而非无限等（2026-09-21 JobSystem 丢唤醒死锁实抓教训——
# ctest 干等 25 分钟；正常 <30s）
add_test(NAME engine-tests COMMAND lemon-tests)
set_tests_properties(engine-tests PROPERTIES TIMEOUT 300)

# M4 纪律断言：ImGui 头文件只准出现在 Editor/（M4-Editor-Plan §5 M4.0 验收项）
add_test(NAME imgui-isolation
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/imgui_isolation.cmake)

# M3 脚本桥测试（需要 dotnet）
if(LEMON_BUILD_SCRIPTING)
  add_subdirectory(script)
endif()
