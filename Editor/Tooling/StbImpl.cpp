// stb 实现 TU（编辑器专用；THIRD_PARTY.md 登记公有领域）。
// stb_image_write：截屏落盘（--screenshot）/ 冒烟 PNG 播种；
// stb_image：M4.4 资产导入器（PNG/JPG 解码，Editor/Assets/AssetGpuCache.cpp）。
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
