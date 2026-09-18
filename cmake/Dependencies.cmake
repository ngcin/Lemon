# Lemon 依赖治理（设计文档 01 §4 / 07 §0）
# 新增第三方库必须同步登记 THIRD_PARTY.md

# ---------------------------------------------------------------------------
# Vulkan 头与加载器：系统提供
#   macOS: brew install vulkan-headers vulkan-loader molten-vk
#   Windows: Vulkan SDK (LunarG)
# 不经 CPM 拉取（loader 必须与运行时 ICD 匹配，属系统件）
find_package(Vulkan REQUIRED)

# ---------------------------------------------------------------------------
# SDL3 — 窗口/输入/手柄（锁 tag，07 文档纪律）
CPMAddPackage(
  NAME SDL3
  GITHUB_REPOSITORY libsdl-org/SDL
  GIT_TAG release-3.2.14
  OPTIONS
    "SDL_TESTS OFF"
    "SDL_INSTALL OFF"
    "SDL_SHARED OFF"
    "SDL_STATIC ON"
    "SDL_TEST_LIBRARY OFF"
)

# ---------------------------------------------------------------------------
# VulkanMemoryAllocator — GPU 内存分配（锁 tag）
# 用法：一个 TU 里 #define VMA_IMPLEMENTATION 后 #include <vk_mem_alloc.h>
CPMAddPackage(
  NAME VulkanMemoryAllocator
  GITHUB_REPOSITORY GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
  GIT_TAG v3.4.0
)
if(NOT TARGET GPUOpen::VulkanMemoryAllocator AND TARGET VulkanMemoryAllocator)
  add_library(GPUOpen::VulkanMemoryAllocator ALIAS VulkanMemoryAllocator)
endif()

# ---------------------------------------------------------------------------
# EnTT — ECS（M0-W3 spike 使用；锁 tag）
CPMAddPackage(
  NAME EnTT
  GITHUB_REPOSITORY skypjack/entt
  GIT_TAG v3.15.0
  EXCLUDE_FROM_ALL YES
)
