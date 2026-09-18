# M0-W1：SDL3 + Vulkan 1.x + VMA + 交换链 + 三角形
# Go/No-Go 判据之一：本机（macOS + MoltenVK）跑通完整 Vulkan 帧循环
add_executable(lemon-spike-triangle main.cpp)

target_link_libraries(lemon-spike-triangle PRIVATE
  SDL3::SDL3
  Vulkan::Vulkan
  GPUOpen::VulkanMemoryAllocator
)

include(${LEMON_CMAKE_DIR}/CompileShaders.cmake)
lemon_embed_shader(lemon-spike-triangle shaders/tri.vert)
lemon_embed_shader(lemon-spike-triangle shaders/tri.frag)

# macOS: 从 build 目录直接运行时找得到 libvulkan（brew 装在 /usr/local/lib）
if(APPLE)
  set_target_properties(lemon-spike-triangle PROPERTIES
    BUILD_RPATH "/usr/local/lib" INSTALL_RPATH "/usr/local/lib")
endif()
