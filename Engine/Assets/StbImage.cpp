// stb 实现 TU（M7a 批② 自 Editor/Tooling/StbImpl.cpp 迁入引擎；THIRD_PARTY.md
// 登记公有领域，只动 TU 归属无新登记项）。
//   * stb_image：资产导入解码（PNG/JPG → 纹理页；编辑器 AssetGpuCache/ThumbCache
//     与运行时 TextureStore 共用）；
//   * stb_image_write：截屏落盘（--screenshot）/ 冒烟 PNG 播种 / 模板生成；
//   * stb_image_resize2：缩略图缩放（同仓库同许可）。
// 全部消费者经链接树（lemon-editor-core/lemon-editor → lemon-engine）解析符号。
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"
