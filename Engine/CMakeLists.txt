# lemon-engine — 引擎内核静态库
# 纪律（AGENTS / 01 文档）：Vulkan/VMA 类型只允许出现在 Renderer 的 .cpp；
#   头文件一律自有句柄。Core = 无渲染依赖的基础层；Platform = SDL3 封装（Pimpl）。
add_library(lemon-engine STATIC
  Core/FileOps.cpp
  Core/JobSystem.cpp
  Core/Log.cpp
  Assets/AssetTypes.cpp
  Assets/AssetIndex.cpp
  Assets/ProjectFile.cpp
  Assets/SpriteRefs.cpp
  Assets/StbImage.cpp
  Assets/TextureStore.cpp
  Assets/AnimAsset.cpp
  Assets/ControllerAsset.cpp
  Assets/TableAsset.cpp
  Assets/PrefabCache.cpp
  Assets/SaveStore.cpp
  Audio/AudioChannel.cpp
  Audio/AudioEngine.cpp
  Audio/AudioMount.cpp
  Audio/BakedClip.cpp
  Audio/MiniAudioImpl.c
  ECS/Hierarchy.cpp
  ECS/ClipTable.cpp
  ECS/ControllerTable.cpp
  ECS/FxChannel.cpp
  ECS/TableStore.cpp
  ECS/SaveChannel.cpp
  ECS/TweenTable.cpp
  ECS/Scene.cpp
  ECS/StateHash.cpp
  ECS/SystemPipeline.cpp
  ECS/World.cpp
  Components/ComponentCatalog.cpp
  Serialization/SceneArchive.cpp
  Physics2D/SpatialHash.cpp
  Systems/Systems.cpp
  Scripting/CoreCLRHost.cpp
  Scripting/ScriptHost.cpp
  Platform/Window.cpp
  Renderer/RHI.cpp
  Renderer/RmlUiBackend.cpp
  Renderer/Atlas.cpp
  Renderer/Renderable.cpp
  Renderer/SpriteBatcher.cpp
  Renderer/Particles.cpp
  Renderer/BitmapFont.cpp
  Renderer/CameraFollow.cpp
  Renderer/SceneExtractor.cpp
  Ui/SdlTextInputHandler.cpp
  Ui/UiMount.cpp
  Ui/UiSubsystem.cpp
)

if(NOT stb_SOURCE_DIR)
  message(FATAL_ERROR "stb dependency missing (cmake/Dependencies.cmake)")
endif()

target_include_directories(lemon-engine
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Scripting/host # hostfxr 头只供 CoreCLRHost.cpp
  PRIVATE ${stb_SOURCE_DIR}) # stb 头只供 Assets/StbImage.cpp 实现 TU

# vendor 代码警告静音（M6c 批⓪/竖切批）：第三方编译噪音不进构建输出。
# MiniAudioImpl.c = 实现 TU（stb_vorbis.c 全量 + miniaudio 实现）；
# StbImage.cpp = stb 三实现单 TU（M7a 批② 自 Editor/Tooling/StbImpl.cpp 迁入）。
set_source_files_properties(Audio/MiniAudioImpl.c Assets/StbImage.cpp PROPERTIES COMPILE_OPTIONS "-w")
target_compile_features(lemon-engine PUBLIC cxx_std_20)
target_link_libraries(lemon-engine
  PRIVATE SDL3::SDL3 Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator nlohmann_json::nlohmann_json
          RmlUi::RmlUi ${CMAKE_DL_LIBS}
  PUBLIC EnTT::EnTT)

# M3 脚本层（dotnet 程序集构建 + lemon-csharp 接口目标；ADR-010）
if(LEMON_BUILD_SCRIPTING)
  add_subdirectory(Scripting)
endif()

include(${LEMON_CMAKE_DIR}/CompileShaders.cmake)
lemon_embed_shader(lemon-engine Renderer/Shaders/sprite.vert)
lemon_embed_shader(lemon-engine Renderer/Shaders/sprite.frag)
# RmlUi 呈现后端三变体（M6a 批③a，ADR-014）
lemon_embed_shader(lemon-engine Renderer/Shaders/rmlui.vert)
lemon_embed_shader(lemon-engine Renderer/Shaders/rmlui_color.frag)
lemon_embed_shader(lemon-engine Renderer/Shaders/rmlui_texture.frag)

# 常量同源护栏（评审 D10）：sprite.frag 的 uAtlases 数组长度必须与 RHI.h 的
# kMaxTextureSlots 相等——slot ≥ 数组长度的采样是未定义行为，两侧漂移此前无任何
# 编译期信号（64 vs 256 静默越界）。改任一侧不同步 = configure 期 FATAL_ERROR
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/Renderer/Shaders/sprite.frag LEMON_SPRITE_FRAG)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/Renderer/RHI.h LEMON_RHI_H)
string(REGEX MATCH "uAtlases\\[([0-9]+)\\]" LEMON_ATLAS_DECL "${LEMON_SPRITE_FRAG}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR "D10 guard: uAtlases array not found in sprite.frag")
endif()
set(LEMON_ATLAS_N "${CMAKE_MATCH_1}")
string(REGEX MATCH "kMaxTextureSlots = ([0-9]+)" LEMON_SLOTS_DECL "${LEMON_RHI_H}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR "D10 guard: kMaxTextureSlots not found in RHI.h")
endif()
if(NOT "${LEMON_ATLAS_N}" STREQUAL "${CMAKE_MATCH_1}")
  message(FATAL_ERROR "D10 guard: sprite.frag uAtlases[${LEMON_ATLAS_N}] != RHI.h kMaxTextureSlots = ${CMAKE_MATCH_1}")
endif()

if(MSVC)
  target_compile_options(lemon-engine PRIVATE /utf-8 /permissive-)
else()
  target_compile_options(lemon-engine PRIVATE -Wall -Wextra -Werror=return-type
                                              -Wno-missing-field-initializers)
endif()
