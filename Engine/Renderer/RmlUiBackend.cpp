// Lemon 引擎 — RmlUi Vulkan 呈现后端实现（M6a 批③a，ADR-014 D2/D3）
// 接入形态 = ADR-008 D2：自研 RenderInterface over 引擎 Vulkan 设备（经 RHI 内部桥）。
// 关键纪律（spike-04 两个上游黑屏根因的对策，DevLog 2026-09-22）：
//   * 帧首钉全幅 viewport+scissor（改造⑧）——管线启用动态 scissor，MoltenVK 上
//     无设置 = 空矩形裁掉全部绘制；
//   * 每帧显式 vmaFlushAllocation（改造⑫）——CPU_TO_GPU 池在 dGPU 可能选中非一致
//     Managed 内存，不 flush 则 CPU 写入对 GPU 不可见（一致内存上 no-op）。
// Vulkan/VMA/RmlUi 类型只准出现在 Engine/Renderer 的 .cpp（AGENTS 零泄漏纪律；
// RmlUi 类型不出头文件 = ADR-014 D3）。纹理留像素备份应对设备丢失重上传（ADR-008 D2）。
#include "Renderer/RmlUiBackend.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <unordered_set>
#include <utility>
#include <vector>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <RmlUi/Core.h>

#include "Core/Log.h"
#include "Renderer/EmbeddedShaders.h"

namespace lemon::renderer {
namespace {

constexpr VkDeviceSize kPoolBytes = 8ull << 20; // CompileGeometry 常驻子分配池
constexpr uint32_t kDeferFrames = 3;            // 在途帧 2 + 余量 1（spike 同款环深）
constexpr uint32_t kMaxTextureSets = 128;

#define LVK_CHECK(x)                                                          \
    do {                                                                      \
        const VkResult lr_ = (x);                                             \
        LEMON_ASSERT(lr_ == VK_SUCCESS, "vk result=%d (%s)", (int)lr_, #x);   \
    } while (0)

struct GeoAlloc {
    VmaVirtualAllocation vAlloc = VK_NULL_HANDLE; // 子分配句柄（释放用）
    VmaVirtualAllocation iAlloc = VK_NULL_HANDLE;
    VkDeviceSize vOffset = 0;                     // 池内偏移（绑定用）
    VkDeviceSize iOffset = 0;
    uint32_t numIndices = 0;
};

struct TextureRes {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> pixels; // 设备丢失重上传备份（GenerateTexture 契约 RGBA 预乘）
};

VkFormat ToVkFormat(rhi::Format f) {
    switch (f) {
        case rhi::Format::RGBA8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
        case rhi::Format::BGRA8UnormSrgb: return VK_FORMAT_B8G8R8A8_SRGB;
        case rhi::Format::RGBA8UnormSrgb: return VK_FORMAT_R8G8B8A8_SRGB;
        case rhi::Format::RGBA16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
        default: return VK_FORMAT_UNDEFINED;
    }
}

// 列主序 out = a * b
void MatMul(float* out, const float* a, const float* b) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + r] * b[c * 4 + k];
            out[c * 4 + r] = s;
        }
}

// 像素坐标（左上原点、y 向下）→ NDC。注意 Vulkan 与 GL 相反：**y=-1 是屏幕顶部**
// （真人目检 2026-09-28 抓到的纵向翻转根因：误用 GL 约定 y=0→+1）
void MakeOrtho(float* m, uint32_t w, uint32_t h) {
    std::memset(m, 0, sizeof(float) * 16);
    m[0] = 2.0f / (float)w;
    m[5] = 2.0f / (float)h;
    m[12] = -1.0f;
    m[13] = -1.0f;
    m[15] = 1.0f;
}

// 纹理上传的暂存参数（一次性提交回调载荷）
struct UploadCtx {
    VkImage image;
    VkBuffer staging;
    uint32_t w, h;
};

void RecordTextureUpload(void* cb, void* ud) {
    auto* ctx = (UploadCtx*)ud;
    VkCommandBuffer cmd = (VkCommandBuffer)cb;
    VkImageMemoryBarrier toDst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = ctx->image;
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toDst);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {ctx->w, ctx->h, 1};
    vkCmdCopyBufferToImage(cmd, ctx->staging, ctx->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    VkImageMemoryBarrier toRead = toDst;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toRead);
}

} // namespace

// ---------------------------------------------------------------- 实现 ----
struct RmlUiBackend::Impl final : public Rml::RenderInterface {
    rhi::Device* device = nullptr;
    VkDevice vkdev = VK_NULL_HANDLE;
    VmaAllocator vma = VK_NULL_HANDLE;
    rhi::Device::RecreateCallbackId recreateCb = 0;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;

    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipeLayout = VK_NULL_HANDLE;
    VkPipeline pipeColor = VK_NULL_HANDLE;
    VkPipeline pipeTexture = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorPool descPool = VK_NULL_HANDLE;

    VkBuffer poolBuf = VK_NULL_HANDLE;
    VmaAllocation poolAlloc = VK_NULL_HANDLE;
    void* poolMapped = nullptr;
    VmaVirtualBlock vblock = VK_NULL_HANDLE;

    uint64_t frameIndex = 0;
    std::array<std::vector<std::pair<VmaVirtualAllocation, VmaVirtualAllocation>>, kDeferFrames>
        pendingGeo;
    std::array<std::vector<TextureRes*>, kDeferFrames> pendingTex;
    std::unordered_set<TextureRes*> liveTex;

    // 帧态（BeginFrame/EndFrame 之间有效）
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    uint32_t vpW = 0, vpH = 0;
    float proj[16];
    float curMvp[16]; // proj ×（SetTransform 矩阵｜单位）
    bool warnedLoadTexture = false;
    bool warnedClipMask = false;
    uint32_t geoFails = 0;

    // 与 rmlui.vert 的 push constant 一致（72B）
    struct Pc {
        float mvp[16];
        float translate[2];
    };

    // ---- 创建/销毁 ----
    void CreateAll() {
        const rhi::Device::InternalBridge& bridge = device->GetInternalBridge();
        vkdev = (VkDevice)bridge.device;
        vma = (VmaAllocator)bridge.allocator;
        CreatePipelines();
        CreatePool();
        CreateDescPoolAndSampler();
        // 设备丢失路径：活纹理按像素备份重传（RmlUi 不感知设备丢失）
        for (TextureRes* t : liveTex) CreateTextureObjects(*t);
    }

    void CreatePipelines() {
        VkDescriptorSetLayoutBinding bind{};
        bind.binding = 0;
        bind.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bind.descriptorCount = 1;
        bind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo dsl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dsl.bindingCount = 1;
        dsl.pBindings = &bind;
        LVK_CHECK(vkCreateDescriptorSetLayout(vkdev, &dsl, nullptr, &setLayout));

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(Pc);
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &setLayout;
        pl.pushConstantRangeCount = 1;
        pl.pPushConstantRanges = &pcr;
        LVK_CHECK(vkCreatePipelineLayout(vkdev, &pl, nullptr, &pipeLayout));

        pipeColor = MakePipeline(lemon_spv_rmlui_vert, lemon_spv_rmlui_vert_count,
                                 lemon_spv_rmlui_color_frag, lemon_spv_rmlui_color_frag_count);
        pipeTexture = MakePipeline(lemon_spv_rmlui_vert, lemon_spv_rmlui_vert_count,
                                   lemon_spv_rmlui_texture_frag, lemon_spv_rmlui_texture_frag_count);
    }

    VkPipeline MakePipeline(const unsigned int* vsWords, size_t vsCount, const unsigned int* fsWords,
                            size_t fsCount) {
        VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        sm.codeSize = vsCount * 4;
        sm.pCode = vsWords;
        VkShaderModule vs = VK_NULL_HANDLE;
        LVK_CHECK(vkCreateShaderModule(vkdev, &sm, nullptr, &vs));
        sm.codeSize = fsCount * 4;
        sm.pCode = fsWords;
        VkShaderModule fs = VK_NULL_HANDLE;
        LVK_CHECK(vkCreateShaderModule(vkdev, &sm, nullptr, &fs));

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs;
        stages[1].pName = "main";

        // Rml::Vertex 原样三属性（零转换——与 spike 一致）
        VkVertexInputBindingDescription vib{};
        vib.binding = 0;
        vib.stride = sizeof(Rml::Vertex);
        vib.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputAttributeDescription via[3]{};
        via[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, (uint32_t)offsetof(Rml::Vertex, position)};
        via[1] = {1, 0, VK_FORMAT_R8G8B8A8_UNORM, (uint32_t)offsetof(Rml::Vertex, colour)};
        via[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, (uint32_t)offsetof(Rml::Vertex, tex_coord)};
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &vib;
        vi.vertexAttributeDescriptionCount = 3;
        vi.pVertexAttributeDescriptions = via;

        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vsState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vsState.viewportCount = 1;
        vsState.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.lineWidth = 1.0f;
        rs.cullMode = VK_CULL_MODE_NONE;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

        // 预乘 alpha：src=ONE / dst=ONE_MINUS_SRC_ALPHA（GenerateTexture 像素恒预乘）
        VkPipelineColorBlendAttachmentState att{};
        att.blendEnable = VK_TRUE;
        att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        att.colorBlendOp = VK_BLEND_OP_ADD;
        att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        att.alphaBlendOp = VK_BLEND_OP_ADD;
        att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1;
        cb.pAttachments = &att;

        VkDynamicState dyns[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates = dyns;

        // 动态渲染（引擎无 VkRenderPass 对象；格式 = gameRT 的唯一依赖）
        VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        ri.colorAttachmentCount = 1;
        ri.pColorAttachmentFormats = &colorFormat;

        VkGraphicsPipelineCreateInfo gi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gi.pNext = &ri;
        gi.stageCount = 2;
        gi.pStages = stages;
        gi.pVertexInputState = &vi;
        gi.pInputAssemblyState = &ia;
        gi.pViewportState = &vsState;
        gi.pRasterizationState = &rs;
        gi.pMultisampleState = &ms;
        gi.pDepthStencilState = &ds;
        gi.pColorBlendState = &cb;
        gi.pDynamicState = &dyn;
        gi.layout = pipeLayout;
        VkPipeline out = VK_NULL_HANDLE;
        LVK_CHECK(vkCreateGraphicsPipelines(vkdev, VK_NULL_HANDLE, 1, &gi, nullptr, &out));
        vkDestroyShaderModule(vkdev, vs, nullptr);
        vkDestroyShaderModule(vkdev, fs, nullptr);
        return out;
    }

    void CreatePool() {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = kPoolBytes;
        bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU; // spike 改造⑩：显式 HOST_ACCESS 组合
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                   VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info{};
        LVK_CHECK(vmaCreateBuffer(vma, &bi, &ai, &poolBuf, &poolAlloc, &info));
        poolMapped = info.pMappedData;
        VmaVirtualBlockCreateInfo vb{};
        vb.size = kPoolBytes;
        LVK_CHECK(vmaCreateVirtualBlock(&vb, &vblock));
    }

    void CreateDescPoolAndSampler() {
        VkDescriptorPoolSize ps{};
        ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ps.descriptorCount = kMaxTextureSets;
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // 纹理逐集释放（验证层 VUID-00312）
        pi.poolSizeCount = 1;
        pi.pPoolSizes = &ps;
        pi.maxSets = kMaxTextureSets;
        LVK_CHECK(vkCreateDescriptorPool(vkdev, &pi, nullptr, &descPool));

        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = 0.0f;
        LVK_CHECK(vkCreateSampler(vkdev, &si, nullptr, &sampler));
    }

    void CreateTextureObjects(TextureRes& t) {
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = VK_FORMAT_R8G8B8A8_UNORM;
        ii.extent = {t.w, t.h, 1};
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        LVK_CHECK(vmaCreateImage(vma, &ii, &ai, &t.image, &t.alloc, nullptr));

        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = t.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R8G8B8A8_UNORM;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        LVK_CHECK(vkCreateImageView(vkdev, &vi, nullptr, &t.view));

        // staging → 一次性提交（上传后即弃，长活纹理无需环形）
        const VkDeviceSize bytes = (VkDeviceSize)t.w * t.h * 4;
        VkBufferCreateInfo sb{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        sb.size = bytes;
        sb.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo sai{};
        sai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        sai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VkBuffer staging = VK_NULL_HANDLE;
        VmaAllocation stagingAlloc = VK_NULL_HANDLE;
        VmaAllocationInfo sinfo{};
        LVK_CHECK(vmaCreateBuffer(vma, &sb, &sai, &staging, &stagingAlloc, &sinfo));
        std::memcpy(sinfo.pMappedData, t.pixels.data(), (size_t)bytes);
        vmaFlushAllocation(vma, stagingAlloc, 0, VK_WHOLE_SIZE); // Managed 内存显式刷（改造⑫同因）

        UploadCtx ctx{t.image, staging, t.w, t.h};
        device->InternalImmediateSubmit(&RecordTextureUpload, &ctx);
        vmaDestroyBuffer(vma, staging, stagingAlloc);

        VkDescriptorSetAllocateInfo dsi{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dsi.descriptorPool = descPool;
        dsi.descriptorSetCount = 1;
        dsi.pSetLayouts = &setLayout;
        if (vkAllocateDescriptorSets(vkdev, &dsi, &t.set) != VK_SUCCESS) {
            t.set = VK_NULL_HANDLE;
            LEMON_ERROR("rmlui-backend: 纹理描述符集耗尽（%u 上限，kMaxTextureSets）",
                        kMaxTextureSets);
            return;
        }
        VkDescriptorImageInfo dii{};
        dii.sampler = sampler;
        dii.imageView = t.view;
        dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = t.set;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &dii;
        vkUpdateDescriptorSets(vkdev, 1, &w, 0, nullptr);
    }

    void DestroyTextureObjects(TextureRes& t) {
        if (t.set) vkFreeDescriptorSets(vkdev, descPool, 1, &t.set);
        if (t.view) vkDestroyImageView(vkdev, t.view, nullptr);
        if (t.image || t.alloc) vmaDestroyImage(vma, t.image, t.alloc);
        t.set = VK_NULL_HANDLE;
        t.view = VK_NULL_HANDLE;
        t.image = VK_NULL_HANDLE;
        t.alloc = VK_NULL_HANDLE;
    }

    void DestroyAll() {
        for (TextureRes* t : liveTex) DestroyTextureObjects(*t);
        for (auto& v : pendingTex)
            for (TextureRes* t : v) {
                DestroyTextureObjects(*t);
                delete t;
            }
        for (auto& v : pendingGeo) v.clear();
        for (auto& v : pendingTex) v.clear();
        if (vblock) vmaDestroyVirtualBlock(vblock);
        if (poolBuf || poolAlloc) vmaDestroyBuffer(vma, poolBuf, poolAlloc);
        if (descPool) vkDestroyDescriptorPool(vkdev, descPool, nullptr);
        if (sampler) vkDestroySampler(vkdev, sampler, nullptr);
        if (pipeColor) vkDestroyPipeline(vkdev, pipeColor, nullptr);
        if (pipeTexture) vkDestroyPipeline(vkdev, pipeTexture, nullptr);
        if (pipeLayout) vkDestroyPipelineLayout(vkdev, pipeLayout, nullptr);
        if (setLayout) vkDestroyDescriptorSetLayout(vkdev, setLayout, nullptr);
        vblock = VK_NULL_HANDLE;
        poolBuf = VK_NULL_HANDLE;
        poolAlloc = VK_NULL_HANDLE;
        poolMapped = nullptr;
        descPool = VK_NULL_HANDLE;
        sampler = VK_NULL_HANDLE;
        pipeColor = VK_NULL_HANDLE;
        pipeTexture = VK_NULL_HANDLE;
        pipeLayout = VK_NULL_HANDLE;
        setLayout = VK_NULL_HANDLE;
    }    /// 设备丢失路径：句柄已随设备销毁（不得再 vkDestroy）——清空后原位重建；
    /// 活纹理按像素备份重传；几何由 UiSubsystem 重载文档重编译（其回调注册在后必随后跑）
    void RecreateAfterLoss() {
        for (auto& v : pendingTex)
            for (TextureRes* t : v) delete t;
        for (auto& v : pendingTex) v.clear();
        for (auto& v : pendingGeo) v.clear();
        for (TextureRes* t : liveTex) {
            t->set = VK_NULL_HANDLE;
            t->view = VK_NULL_HANDLE;
            t->image = VK_NULL_HANDLE;
            t->alloc = VK_NULL_HANDLE;
        }
        vblock = VK_NULL_HANDLE;
        poolBuf = VK_NULL_HANDLE;
        poolAlloc = VK_NULL_HANDLE;
        poolMapped = nullptr;
        descPool = VK_NULL_HANDLE;
        sampler = VK_NULL_HANDLE;
        pipeColor = VK_NULL_HANDLE;
        pipeTexture = VK_NULL_HANDLE;
        pipeLayout = VK_NULL_HANDLE;
        setLayout = VK_NULL_HANDLE;
        geoFails = 0;
        CreateAll();
    }

    void DrainDeferred(uint32_t slot) {
        for (auto [vA, iA] : pendingGeo[slot]) {
            vmaVirtualFree(vblock, vA);
            vmaVirtualFree(vblock, iA);
        }
        pendingGeo[slot].clear();
        for (TextureRes* t : pendingTex[slot]) {
            liveTex.erase(t);
            DestroyTextureObjects(*t);
            delete t;
        }
        pendingTex[slot].clear();
    }

    // ---- Rml::RenderInterface ----
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override {
        const VkDeviceSize vSize = (VkDeviceSize)vertices.size() * sizeof(Rml::Vertex);
        const VkDeviceSize iSize = (VkDeviceSize)indices.size() * sizeof(int);
        if (vSize == 0 || iSize == 0) return 0;
        VmaVirtualAllocationCreateInfo va{};
        va.size = vSize;
        va.alignment = 16;
        VmaVirtualAllocation vA = VK_NULL_HANDLE;
        VkDeviceSize vOff = 0;
        if (vmaVirtualAllocate(vblock, &va, &vA, &vOff) != VK_SUCCESS) {
            ++geoFails;
            LEMON_ERROR("rmlui-backend: 几何池耗尽（%llu MB，kPoolBytes）——几何跳过",
                        (unsigned long long)(kPoolBytes >> 20));
            return 0;
        }
        va.size = iSize;
        va.alignment = 4;
        VmaVirtualAllocation iA = VK_NULL_HANDLE;
        VkDeviceSize iOff = 0;
        if (vmaVirtualAllocate(vblock, &va, &iA, &iOff) != VK_SUCCESS) {
            vmaVirtualFree(vblock, vA);
            ++geoFails;
            LEMON_ERROR("rmlui-backend: 几何池耗尽（索引段）");
            return 0;
        }
        std::memcpy((uint8_t*)poolMapped + vOff, vertices.data(), (size_t)vSize);
        std::memcpy((uint8_t*)poolMapped + iOff, indices.data(), (size_t)iSize);
        auto* g = new GeoAlloc{};
        g->vAlloc = vA;
        g->iAlloc = iA;
        g->vOffset = vOff;
        g->iOffset = iOff;
        g->numIndices = (uint32_t)indices.size();
        return (Rml::CompiledGeometryHandle)g;
    }

    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override {
        if (!cmd) return; // 渲染块外（理论不可达，防御）
        auto* g = (GeoAlloc*)geometry;
        auto* t = (TextureRes*)texture;
        const bool textured = texture != 0 && t && t->set;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, textured ? pipeTexture : pipeColor);
        Pc pc;
        std::memcpy(pc.mvp, curMvp, sizeof pc.mvp);
        pc.translate[0] = translation.x;
        pc.translate[1] = translation.y;
        vkCmdPushConstants(cmd, pipeLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(Pc), &pc);
        VkDeviceSize vOff = g->vOffset;
        vkCmdBindVertexBuffers(cmd, 0, 1, &poolBuf, &vOff);
        vkCmdBindIndexBuffer(cmd, poolBuf, g->iOffset, VK_INDEX_TYPE_UINT32);
        if (textured) vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout, 0, 1, &t->set, 0, nullptr);
        vkCmdDrawIndexed(cmd, g->numIndices, 1, 0, 0, 0);
    }

    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override {
        auto* g = (GeoAlloc*)geometry;
        pendingGeo[frameIndex % kDeferFrames].emplace_back(g->vAlloc, g->iAlloc);
        delete g;
    }

    Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions,
                                   const Rml::String& source) override {
        (void)texture_dimensions;
        if (!warnedLoadTexture) {
            warnedLoadTexture = true;
            LEMON_WARN("rmlui-backend: 图片纹理暂不支持（'%s'）——③b 资产桥接上"
                       "（CSS 底色/边框/文字不受影响）", source.c_str());
        }
        return 0;
    }

    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                       Rml::Vector2i source_dimensions) override {
        const size_t want = (size_t)source_dimensions.x * source_dimensions.y * 4;
        if (source_dimensions.x <= 0 || source_dimensions.y <= 0 || source.size() < want) {
            LEMON_ERROR("rmlui-backend: GenerateTexture 非法参数（%dx%d, %zu 字节）",
                        source_dimensions.x, source_dimensions.y, source.size());
            return 0;
        }
        auto* t = new TextureRes{};
        t->w = (uint32_t)source_dimensions.x;
        t->h = (uint32_t)source_dimensions.y;
        t->pixels.assign(source.data(), source.data() + want);
        CreateTextureObjects(*t);
        liveTex.insert(t);
        return (Rml::TextureHandle)t;
    }

    void ReleaseTexture(Rml::TextureHandle texture) override {
        pendingTex[frameIndex % kDeferFrames].push_back((TextureRes*)texture);
    }

    void EnableScissorRegion(bool enable) override {
        if (!cmd) return;
        if (enable) return; // 等 SetScissorRegion 给出具体矩形
        VkRect2D full{{0, 0}, {vpW, vpH}};
        vkCmdSetScissor(cmd, 0, 1, &full);
    }

    void SetScissorRegion(Rml::Rectanglei region) override {
        if (!cmd) return;
        // 6.3 语义：窗口坐标（与 transform 无关）——clamp 后直接 vkCmdSetScissor
        const int32_t x = std::clamp(region.Left(), 0, (int)vpW);
        const int32_t y = std::clamp(region.Top(), 0, (int)vpH);
        const int32_t x1 = std::clamp(region.Right(), 0, (int)vpW);
        const int32_t y1 = std::clamp(region.Bottom(), 0, (int)vpH);
        VkRect2D r{{x, y}, {(uint32_t)(x1 - x), (uint32_t)(y1 - y)}};
        vkCmdSetScissor(cmd, 0, 1, &r);
    }

    void SetTransform(const Rml::Matrix4f* transform) override {
        if (transform) {
            float t[16];
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r) t[c * 4 + r] = (*transform)[c][r];
            MatMul(curMvp, proj, t);
        } else {
            std::memcpy(curMvp, proj, sizeof proj);
        }
    }

    // v1 红线外（ADR-014 D7）：clip-mask / 层合成 / filter——一次性告警后 no-op
    void EnableClipMask(bool enable) override {
        if (enable && !warnedClipMask) {
            warnedClipMask = true;
            LEMON_WARN("rmlui-backend: clip-mask 未实现（③a 范围外，ADR-014 D7）");
        }
    }
};

// ------------------------------------------------------------ 公开接口 ----
RmlUiBackend::RmlUiBackend() = default;
RmlUiBackend::~RmlUiBackend() { LEMON_ASSERT(!impl_, "RmlUiBackend 未 Shutdown 即析构"); }

void RmlUiBackend::Init(rhi::Device& device, rhi::Format colorFormat) {
    impl_ = std::make_unique<Impl>();
    Impl& i = *impl_;
    i.device = &device;
    i.colorFormat = ToVkFormat(colorFormat);
    if (i.colorFormat == VK_FORMAT_UNDEFINED) {
        LEMON_ERROR("rmlui-backend: 不支持的 RT 格式（%d）", (int)colorFormat);
        impl_.reset();
        return;
    }
    i.CreateAll();
    i.recreateCb = device.AddRecreateCallback("rmlui-backend", [this](rhi::Device&) {
        impl_->RecreateAfterLoss();
    });
}

void RmlUiBackend::Shutdown() {
    if (!impl_) return;
    if (impl_->recreateCb) impl_->device->RemoveRecreateCallback(impl_->recreateCb);
    impl_->device->WaitIdle(); // 在途帧可能引用池/纹理
    impl_->DestroyAll();
    for (auto* t : impl_->liveTex) delete t;
    impl_->liveTex.clear();
    impl_.reset();
}

void RmlUiBackend::BeginFrame(rhi::CommandList& cl, uint32_t rtW, uint32_t rtH) {
    if (!impl_) return;
    Impl& i = *impl_;
    ++i.frameIndex;
    i.DrainDeferred((uint32_t)(i.frameIndex % kDeferFrames));
    i.cmd = (VkCommandBuffer)cl.NativeCommandBuffer();
    i.vpW = rtW;
    i.vpH = rtH;
    MakeOrtho(i.proj, rtW, rtH);
    std::memcpy(i.curMvp, i.proj, sizeof i.proj);
    VkViewport vp{0.0f, 0.0f, (float)rtW, (float)rtH, 0.0f, 1.0f};
    vkCmdSetViewport(i.cmd, 0, 1, &vp);
    VkRect2D full{{0, 0}, {rtW, rtH}};
    vkCmdSetScissor(i.cmd, 0, 1, &full); // spike 改造⑧：帧首钉默认 scissor
}

void RmlUiBackend::EndFrame() {
    if (!impl_) return;
    Impl& i = *impl_;
    if (i.poolAlloc) vmaFlushAllocation(i.vma, i.poolAlloc, 0, VK_WHOLE_SIZE); // 改造⑫
    i.cmd = VK_NULL_HANDLE;
}

void* RmlUiBackend::RenderInterfacePtr() { return impl_.get(); }

} // namespace lemon::renderer
