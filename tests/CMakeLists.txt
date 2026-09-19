# 引擎纯逻辑单测（无窗口/无 Vulkan，可进 CI 冒烟）
add_executable(lemon-tests engine_tests.cpp)
target_link_libraries(lemon-tests PRIVATE lemon-engine)

enable_testing()
add_test(NAME engine-tests COMMAND lemon-tests)

# M3 脚本桥测试（需要 dotnet）
if(LEMON_BUILD_SCRIPTING)
  add_subdirectory(script)
endif()
