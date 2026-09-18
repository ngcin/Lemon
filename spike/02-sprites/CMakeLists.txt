# M0-W2：实例化精灵压测 —— 单次 draw call 画 N 个动画精灵（默认 10 万）
# Go/No-Go 判据之二：本机（macOS + MoltenVK / RX 590）10 万动画精灵 ≥ 60fps
add_executable(lemon-spike-sprites main.cpp)

target_link_libraries(lemon-spike-sprites PRIVATE
  SDL3::SDL3
  Vulkan::Vulkan
  GPUOpen::VulkanMemoryAllocator
)

include(${LEMON_CMAKE_DIR}/CompileShaders.cmake)
lemon_embed_shader(lemon-spike-sprites shaders/sprite.vert)
lemon_embed_shader(lemon-spike-sprites shaders/sprite.frag)

if(APPLE)
  set_target_properties(lemon-spike-sprites PROPERTIES
    BUILD_RPATH "/usr/local/lib" INSTALL_RPATH "/usr/local/lib")
endif()
