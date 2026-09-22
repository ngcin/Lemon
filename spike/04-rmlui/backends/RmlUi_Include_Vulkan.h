#ifndef RMLUI_BACKENDS_INCLUDE_VULKAN_H
	#define RMLUI_BACKENDS_INCLUDE_VULKAN_H

	// [Lemon spike 改造] 原版在 RMLUI_PLATFORM_UNIX（含 macOS）定义
	// VK_USE_PLATFORM_XCB_KHR → vendored vulkan.h（实为 glad header-only loader）
	// 会 #include <xcb/xcb.h>——mac 没有（RmlUi 官方后端未覆盖 macOS）。mac 表面
	// 创建走 SDL_Vulkan_CreateSurface，无需任何 VK_USE_PLATFORM_* 头 → 仅删该宏，
	// 其余（glad loader + vendored VMA 配对）保持官方原样。
	#define VMA_STATIC_VULKAN_FUNCTIONS 0
	#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#endif

#if defined _MSC_VER
	#pragma warning(push, 0)
#elif defined __clang__
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wall"
	#pragma clang diagnostic ignored "-Wextra"
	#pragma clang diagnostic ignored "-Wnullability-extension"
	#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
	#pragma clang diagnostic ignored "-Wnullability-completeness"
#elif defined __GNUC__
	#pragma GCC system_header
#endif

#include "RmlUi_Vulkan/vulkan.h"
// Always include "vulkan.h" first, this comment prevents clang-format from reordering the includes.
#include "RmlUi_Vulkan/vk_mem_alloc.h"

#if defined _MSC_VER
	#pragma warning(pop)
#elif defined __clang__
	#pragma clang diagnostic pop
#endif
