# bench-particles —— 10 万存活粒子压测（M1-S5 验收：GPU ≤ 4ms）
add_executable(lemon-bench-particles main.cpp)
target_link_libraries(lemon-bench-particles PRIVATE lemon-engine)

if(APPLE)
  set_target_properties(lemon-bench-particles PROPERTIES
    BUILD_RPATH "/usr/local/lib" INSTALL_RPATH "/usr/local/lib")
endif()
