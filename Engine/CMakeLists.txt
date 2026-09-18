# lemon-engine — 引擎内核静态库
# 纪律（AGENTS / 01 文档）：Vulkan/VMA 类型只允许出现在 Renderer 的 .cpp；
#   头文件一律自有句柄。Core = 无渲染依赖的基础层。
add_library(lemon-engine STATIC
  Core/Log.cpp
)

target_include_directories(lemon-engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(lemon-engine PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(lemon-engine PRIVATE /utf-8 /permissive-)
else()
  target_compile_options(lemon-engine PRIVATE -Wall -Wextra -Werror=return-type
                                              -Wno-missing-field-initializers)
endif()
