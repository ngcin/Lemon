# lemon-engine — 引擎内核静态库
# 纪律（AGENTS / 01 文档）：Vulkan/VMA 类型只允许出现在 Renderer 的 .cpp；
#   头文件一律自有句柄。Core = 无渲染依赖的基础层；Platform = SDL3 封装（Pimpl）。
add_library(lemon-engine STATIC
  Core/Log.cpp
  Platform/Window.cpp
  Renderer/RHI.cpp
  Renderer/Atlas.cpp
  Renderer/Renderable.cpp
  Renderer/SpriteBatcher.cpp
  Renderer/Particles.cpp
  Renderer/BitmapFont.cpp
)

target_include_directories(lemon-engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(lemon-engine PUBLIC cxx_std_20)
target_link_libraries(lemon-engine
  PRIVATE SDL3::SDL3 Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator)

include(${LEMON_CMAKE_DIR}/CompileShaders.cmake)
lemon_embed_shader(lemon-engine Renderer/Shaders/sprite.vert)
lemon_embed_shader(lemon-engine Renderer/Shaders/sprite.frag)

if(MSVC)
  target_compile_options(lemon-engine PRIVATE /utf-8 /permissive-)
else()
  target_compile_options(lemon-engine PRIVATE -Wall -Wextra -Werror=return-type
                                              -Wno-missing-field-initializers)
endif()
