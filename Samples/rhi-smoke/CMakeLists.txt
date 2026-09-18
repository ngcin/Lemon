# rhi-smoke —— RHI 全链路冒烟（动态渲染/bindless/SSBO/管线缓存/时间戳/设备丢失）
add_executable(lemon-rhi-smoke main.cpp)
target_link_libraries(lemon-rhi-smoke PRIVATE lemon-engine)

if(APPLE)
  set_target_properties(lemon-rhi-smoke PROPERTIES
    BUILD_RPATH "/usr/local/lib" INSTALL_RPATH "/usr/local/lib")
endif()
