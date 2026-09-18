# bench-sprites —— 提取/排序/合批/录制完整流水线压测（M1-S3 验收）
add_executable(lemon-bench-sprites main.cpp)
target_link_libraries(lemon-bench-sprites PRIVATE lemon-engine)

if(APPLE)
  set_target_properties(lemon-bench-sprites PROPERTIES
    BUILD_RPATH "/usr/local/lib" INSTALL_RPATH "/usr/local/lib")
endif()
