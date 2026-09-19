# 引擎纯逻辑单测（无窗口/无 Vulkan，可进 CI 冒烟）
add_executable(lemon-tests engine_tests.cpp)
target_link_libraries(lemon-tests PRIVATE lemon-engine)

enable_testing()
add_test(NAME engine-tests COMMAND lemon-tests)

# M4 纪律断言：ImGui 头文件只准出现在 Editor/（M4-Editor-Plan §5 M4.0 验收项）
add_test(NAME imgui-isolation
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/imgui_isolation.cmake)

# M3 脚本桥测试（需要 dotnet）
if(LEMON_BUILD_SCRIPTING)
  add_subdirectory(script)
endif()
