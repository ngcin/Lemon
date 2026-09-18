# bench-mow —— M1 终验收官场（08 §3）：10 万精灵 + 5 万粒子 + HUD 文本
add_executable(lemon-bench-mow main.cpp)
target_link_libraries(lemon-bench-mow PRIVATE lemon-engine)

if(APPLE)
  set_target_properties(lemon-bench-mow PROPERTIES
    BUILD_RPATH "/usr/local/lib" INSTALL_RPATH "/usr/local/lib")
endif()
