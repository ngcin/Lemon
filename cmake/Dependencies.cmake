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

# ---------------------------------------------------------------------------
# nlohmann/json — 唯一数据格式 JSON（01 选型 / 06 §3；M2 场景序列化起）
CPMAddPackage(
  NAME nlohmann_json
  GITHUB_REPOSITORY nlohmann/json
  GIT_TAG v3.11.3
  OPTIONS "JSON_BuildTests OFF" "JSON_Install OFF"
)

# ---------------------------------------------------------------------------
# Dear ImGui — 编辑器 UI（docking 分支，ADR-005；M4.md §3.1 锁 tag）
# 纪律：ImGui 头文件只准出现在 Editor/（M4.md §3.2）；包装目标 lemon-imgui
# 定义在 Editor/CMakeLists.txt（含 SDL3/Vulkan backend 两个 TU），不进 lemon-engine。
CPMAddPackage(
  NAME imgui
  GITHUB_REPOSITORY ocornut/imgui
  GIT_TAG v1.92.9b-docking
  EXCLUDE_FROM_ALL YES
  DOWNLOAD_ONLY YES
)

# ---------------------------------------------------------------------------
# stb — 图像解码（06 §2.2 PNG 导入器；公有领域）
CPMAddPackage(
  NAME stb
  GITHUB_REPOSITORY nothings/stb
  GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20  # master 锁 commit（2026-09 实查）
  EXCLUDE_FROM_ALL YES
  DOWNLOAD_ONLY YES
)

# ---------------------------------------------------------------------------
# RmlUi — 运行时 UI（M5 ADR-008 spike；锁 tag 6.3）
# 纪律（AGENTS）：已登记 THIRD_PARTY.md + 07 移植矩阵。spike/04-rmlui 消费
# rmlui_backend_SDL_VK（官方 SDL3+Vulkan 后端，MIT 头须保留版权声明）。
# FreeType 用系统 brew 件（2.14.3）。spike 未过验收前不进 Engine/ 正式依赖。
CPMAddPackage(
  NAME RmlUi
  GITHUB_REPOSITORY mikke89/RmlUi
  GIT_TAG 6.3
  OPTIONS
    "BUILD_SHARED_LIBS OFF"
    "RMLUI_SAMPLES OFF"
    "RMLUI_TESTS OFF"
    "RMLUI_SHELL ON"           # 拉入 Backends/ 目标（rmlui_backend_SDL_VK）
    "RMLUI_BACKEND SDL_VK"     # 后端自动选择（auto→GL3 会要 SDL3_image）
    "RMLUI_PRECOMPILED_HEADERS OFF"
    "RMLUI_COMPILER_OPTIONS OFF"
    "RMLUI_THIRDPARTY_CONTAINERS ON"
  EXCLUDE_FROM_ALL YES
)
