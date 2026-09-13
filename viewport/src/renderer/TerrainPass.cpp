#include "infraforge/viewport/renderer/TerrainPass.hpp"

#include "infraforge/runtime/Logging.hpp"
#include "infraforge/viewport/renderer/Vulkan.hpp"

#include <ViewportShaderSource.h>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <shaderc/shaderc.hpp>
#include <utility>

namespace infraforge::viewport {
namespace {

constexpr std::size_t kPosFloats = 3;
constexpr std::size_t kVertexFloats = 6;

[[nodiscard]] bool isNoData(double value) noexcept {
    return std::isnan(value);
}

// Returns SPIR-V words; shaderc result iterators yield 32-bit words, and the
// byte length passed to vkCreateShaderModule is words * sizeof(word).
std::vector<std::uint32_t> compileShader(
    const std::string_view source,
    const shaderc_shader_kind kind,
    const std::string& name) {
    const shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    const auto result = compiler.CompileGlslToSpv(
        source.data(), source.size(), kind, name.c_str(), options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        throw RendererError(
            std::string{"shader compilation failed for "} + name + ": " + result.GetErrorMessage(),
            VK_ERROR_INITIALIZATION_FAILED);
    }
    return {result.cbegin(), result.cend()};
}

std::uint32_t findMemoryType(
    const VkPhysicalDevice physical,
    const std::uint32_t requiredBits,
    const VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &memoryProperties);
    for (std::uint32_t type = 0; type < memoryProperties.memoryTypeCount; ++type) {
        const bool typeAllowed = (requiredBits & (1U << type)) != 0;
        const bool hasProperties =
            (memoryProperties.memoryTypes[type].propertyFlags & properties) == properties;
        if (typeAllowed && hasProperties) {
            return type;
        }
    }
    throw RendererError("no suitable Vulkan memory type found", VK_ERROR_INITIALIZATION_FAILED);
}

} // namespace

void TerrainPass::create(
    const VkPhysicalDevice physical,
    const VkDevice device,
    const VkQueue queue,
    const std::uint32_t queueFamily,
    const VkRenderPass renderPass) {
    physical_ = physical;
    device_ = device;
    queue_ = queue;
    queueFamily_ = queueFamily;
    renderPass_ = renderPass;

    const std::vector<std::uint32_t> vertexSpirv = compileShader(
        shaders::kTerrainVertex, shaderc_glsl_vertex_shader, "terrain.vert");
    const std::vector<std::uint32_t> fragmentSpirv = compileShader(
        shaders::kTerrainFragment, shaderc_glsl_fragment_shader, "terrain.frag");

    VkShaderModuleCreateInfo vertexInfo{};
    vertexInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertexInfo.codeSize = vertexSpirv.size() * sizeof(std::uint32_t);
    vertexInfo.pCode = vertexSpirv.data();
    VkShaderModule rawVertex = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(device_, &vertexInfo, nullptr, &rawVertex),
        "terrain vertex shader module creation");
    UniqueVulkan<VkShaderModule> vertexShader{rawVertex, [device](VkShaderModule module) {
        vkDestroyShaderModule(device, module, nullptr);
    }};

    VkShaderModuleCreateInfo fragmentInfo{};
    fragmentInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragmentInfo.codeSize = fragmentSpirv.size() * sizeof(std::uint32_t);
    fragmentInfo.pCode = fragmentSpirv.data();
    VkShaderModule rawFragment = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(device_, &fragmentInfo, nullptr, &rawFragment),
        "terrain fragment shader module creation");
    UniqueVulkan<VkShaderModule> fragmentShader{rawFragment, [device](VkShaderModule module) {
        vkDestroyShaderModule(device, module, nullptr);
    }};

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = 64; // mat4 viewProj

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VkPipelineLayout rawLayout = VK_NULL_HANDLE;
    VK_CHECK(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &rawLayout),
        "terrain pipeline layout creation");
    pipelineLayout_ = UniqueVulkan<VkPipelineLayout>{rawLayout, [device](VkPipelineLayout layout) {
        vkDestroyPipelineLayout(device, layout, nullptr);
    }};

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexShader.get();
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentShader.get();
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = static_cast<std::uint32_t>(kVertexFloats * sizeof(float));
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attributes{};
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = 0;
    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = static_cast<std::uint32_t>(kPosFloats * sizeof(float));

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0F;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.minDepthBounds = 0.0F;
    depthStencil.maxDepthBounds = 1.0F;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_FALSE;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    constexpr std::array<VkDynamicState, 2> dynamicStates{
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_.get();
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &rawPipeline),
        "terrain pipeline creation");
    pipeline_ = UniqueVulkan<VkPipeline>{rawPipeline, [device](VkPipeline pipeline) {
        vkDestroyPipeline(device, pipeline, nullptr);
    }};
}

void TerrainPass::destroy() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    for (auto& [id, payload] : payloads_) {
        if (payload.gpu.has_value()) {
            destroyGpu(*payload.gpu);
        }
    }
    payloads_.clear();
    pipeline_.reset();
    pipelineLayout_.reset();
    queue_ = VK_NULL_HANDLE;
    renderPass_ = VK_NULL_HANDLE;
    physical_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
}

void TerrainPass::setScene(const TerrainScene& scene) {
    std::lock_guard lock{sceneMutex_};
    pendingScene_ = scene;
}

TerrainPass::TilePayload TerrainPass::buildPayload(
    const TerrainSceneTile& tile, const std::string& filePath, const std::uint32_t lod) const {
    // Decode + validate: a tile whose embedded provenance does not match the
    // manifest (regenerated or stale file) is rejected, never interpreted.
    const domain::terrain::TerrainTileFile decoded = domain::terrain::decodeTerrainTileFromFile(filePath);
    if (decoded.datasetUuid != tile.datasetUuid || decoded.datasetRevision != tile.datasetRevision) {
        throw domain::terrain::TerrainTileFormatError(
            "terrain tile provenance mismatch for " + filePath + " (file revision "
            + std::to_string(decoded.datasetRevision) + " vs manifest revision "
            + std::to_string(tile.datasetRevision) + ")");
    }
    if (lod >= decoded.lods.size()) {
        throw domain::terrain::TerrainTileFormatError("terrain tile LOD level out of range");
    }
    const domain::terrain::TerrainTileLodGrid& grid = decoded.lods[lod];
    const std::uint32_t dim = grid.dim;

    TilePayload payload;
    payload.datasetUuid = tile.datasetUuid;
    payload.datasetRevision = tile.datasetRevision;
    payload.lod = lod;

    // Render-local conversion in double precision through the shared
    // RenderLocalFrame; only the converted result becomes float. Northing is
    // flipped so north renders up under the camera's screen-space Y-down
    // convention.
    const auto renderLocal = [this](double easting, double northing, double height) {
        const domain::geo::RenderLocalPosition local = renderFrame_->toRenderLocal(
            domain::geo::ProjectGlobalPosition{.easting = easting, .northing = northing, .height = height});
        return std::array<float, 3>{
            static_cast<float>(local.x), static_cast<float>(-local.y), static_cast<float>(local.z)};
    };

    const std::size_t vertexCount = static_cast<std::size_t>(dim) * dim;
    payload.vertices.reserve(vertexCount * kVertexFloats);
    std::vector<char> valid(vertexCount, 0);
    for (std::uint32_t row = 0; row < dim; ++row) {
        for (std::uint32_t col = 0; col < dim; ++col) {
            const std::size_t gridIndex = static_cast<std::size_t>(row) * dim + col;
            const double height = grid.heights[gridIndex];
            const bool ok = !isNoData(height);
            valid[gridIndex] = ok ? 1 : 0;

            const double easting = grid.originEasting + static_cast<double>(col) * grid.cellEasting;
            const double northing = grid.originNorthing - static_cast<double>(row) * grid.cellNorthing;
            if (!ok) {
                for (std::size_t i = 0; i < kVertexFloats; ++i) {
                    payload.vertices.push_back(std::numeric_limits<float>::quiet_NaN());
                }
                continue;
            }
            const std::array<float, 3> position = renderLocal(easting, northing, height);

            // Per-vertex normal via central differences over the height
            // grid; edges and NoData neighbours fall back to flat-up so the
            // vertex still shades plausibly.
            const auto sampleOrNaN = [&](std::int64_t c, std::int64_t r) {
                if (c < 0 || r < 0 || c >= static_cast<std::int64_t>(dim)
                    || r >= static_cast<std::int64_t>(dim)) {
                    return std::numeric_limits<double>::quiet_NaN();
                }
                return grid.heights[static_cast<std::size_t>(r) * dim + static_cast<std::size_t>(c)];
            };
            const double west = sampleOrNaN(static_cast<std::int64_t>(col) - 1, static_cast<std::int64_t>(row));
            const double east = sampleOrNaN(static_cast<std::int64_t>(col) + 1, static_cast<std::int64_t>(row));
            const double south = sampleOrNaN(static_cast<std::int64_t>(col), static_cast<std::int64_t>(row) - 1);
            const double north = sampleOrNaN(static_cast<std::int64_t>(col), static_cast<std::int64_t>(row) + 1);
            double dhde = 0.0;
            double dhdn = 0.0;
            if (!isNoData(west) && !isNoData(east)) {
                dhde = (east - west) / (2.0 * grid.cellEasting);
            }
            if (!isNoData(south) && !isNoData(north)) {
                dhdn = (north - south) / (2.0 * grid.cellNorthing);
            }
            // Surface normal of z = h(e, n); northing is flipped with the
            // positions, so its slope contribution flips sign as well.
            const double nx = -dhde;
            const double ny = dhdn;
            const double nz = 1.0;
            const double length = std::sqrt(nx * nx + ny * ny + nz * nz);

            payload.vertices.push_back(position[0]);
            payload.vertices.push_back(position[1]);
            payload.vertices.push_back(position[2]);
            payload.vertices.push_back(static_cast<float>(nx / length));
            payload.vertices.push_back(static_cast<float>(ny / length));
            payload.vertices.push_back(static_cast<float>(nz / length));
        }
    }

    // Indices: quads with any NoData corner are dropped so NoData regions
    // become holes instead of fabricated surfaces.
    const auto vertexIndex = [&](std::uint32_t row, std::uint32_t col) {
        return static_cast<std::uint32_t>(static_cast<std::size_t>(row) * dim + col);
    };
    for (std::uint32_t row = 0; row + 1 < dim; ++row) {
        for (std::uint32_t col = 0; col + 1 < dim; ++col) {
            const std::uint32_t v00 = vertexIndex(row, col);
            const std::uint32_t v10 = vertexIndex(row, col + 1);
            const std::uint32_t v01 = vertexIndex(row + 1, col);
            const std::uint32_t v11 = vertexIndex(row + 1, col + 1);
            if (valid[v00] == 0 || valid[v10] == 0 || valid[v01] == 0 || valid[v11] == 0) {
                continue;
            }
            payload.indices.insert(payload.indices.end(), {v00, v10, v01, v10, v11, v01});
        }
    }

    // Vertical skirts around the tile boundary hide LOD seams between
    // differently-resolved neighbours. Each surface boundary vertex gets a
    // lowered duplicate with a downward normal; consecutive valid boundary
    // vertices are connected into a vertical strip.
    double zRange = grid.maxZ - grid.minZ;
    if (!(zRange > 0.0)) {
        zRange = 1.0;
    }
    const float skirtDepth = static_cast<float>(std::max(1.0, 0.05 * zRange));
    const auto skirtVertex = [&](std::uint32_t surface) {
        const std::size_t base = static_cast<std::size_t>(surface) * kVertexFloats;
        for (std::size_t i = 0; i < kVertexFloats; ++i) {
            payload.vertices.push_back(payload.vertices[base + i]);
        }
        const std::size_t last = payload.vertices.size();
        payload.vertices[last - 4] -= skirtDepth; // lower position z
        payload.vertices[last - 3] = 0.0F;        // skirt normal: down
        payload.vertices[last - 2] = 0.0F;
        payload.vertices[last - 1] = -1.0F;
        return static_cast<std::uint32_t>(last / kVertexFloats - 1);
    };
    const auto connectSkirts = [&](std::uint32_t a, std::uint32_t b) {
        if (valid[a] == 0 || valid[b] == 0) {
            return;
        }
        const std::uint32_t skirtA = skirtVertex(a);
        const std::uint32_t skirtB = skirtVertex(b);
        payload.indices.insert(payload.indices.end(), {a, skirtA, b, b, skirtA, skirtB});
    };
    for (std::uint32_t col = 0; col + 1 < dim; ++col) {
        connectSkirts(vertexIndex(0, col), vertexIndex(0, col + 1));            // north edge
        connectSkirts(vertexIndex(dim - 1, col), vertexIndex(dim - 1, col + 1)); // south edge
    }
    for (std::uint32_t row = 0; row + 1 < dim; ++row) {
        connectSkirts(vertexIndex(row, 0), vertexIndex(row + 1, 0));            // west edge
        connectSkirts(vertexIndex(row, dim - 1), vertexIndex(row + 1, dim - 1)); // east edge
    }
    return payload;
}

TerrainPass::TileGpu TerrainPass::uploadPayload(const TilePayload& payload) const {
    const VkDeviceSize vertexBytes = static_cast<VkDeviceSize>(payload.vertices.size() * sizeof(float));
    const VkDeviceSize indexBytes = static_cast<VkDeviceSize>(payload.indices.size() * sizeof(std::uint32_t));

    // BLOCKER 3: an all-NoData tile legitimately produces zero indices. Skip
    // all GPU allocation/upload when there is nothing to draw — Vulkan
    // buffers must have size > 0. The render code checks indexCount == 0
    // and skips drawing, so a TileGpu with VK_NULL_HANDLE buffers is safe.
    if (indexBytes == 0) {
        TileGpu empty;
        empty.indexCount = 0;
        return empty;
    }

    // BLOCKER 7: portable GPU upload via host-visible staging buffer +
    // device-local final buffers. Does not require HOST_VISIBLE +
    // DEVICE_LOCAL on the same memory type (not available on discrete GPUs).
    // Handles non-coherent mapped memory with explicit flush.

    // BLOCKER 2: for non-coherent memory, flush ranges must obey
    // nonCoherentAtomSize alignment. We map the entire allocation and flush
    // with VK_WHOLE_SIZE, which the Vulkan spec always accepts regardless of
    // atom size. The atom size is queried for diagnostic logging only.
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_, &properties);
    const VkDeviceSize atomSize = properties.limits.nonCoherentAtomSize;
    if (atomSize == 0) {
        // Should never happen per the Vulkan spec, but guard defensively.
        runtime::logWarn("viewport", "terrain.invalid_atom_size", {});
    }

    const auto createBuffer = [&](const VkDeviceSize size, const VkBufferUsageFlags usage) {
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer raw = VK_NULL_HANDLE;
        VK_CHECK(vkCreateBuffer(device_, &info, nullptr, &raw), "terrain buffer creation");
        return raw;
    };

    // Allocate host-visible memory for a staging buffer. Prefer HOST_COHERENT
    // for simplicity; fall back to HOST_VISIBLE only and flush explicitly.
    // Returns the allocation size (from VkMemoryRequirements::size) so the
    // caller can map/flush the entire allocation, not just the payload bytes.
    const auto allocateStaging = [&](const VkBuffer buffer) {
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, buffer, &requirements);
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = requirements.size;
        // Try HOST_VISIBLE | HOST_COHERENT first; if not available, accept
        // HOST_VISIBLE and flush manually.
        std::uint32_t typeIndex = 0;
        bool coherent = false;
        try {
            typeIndex = findMemoryType(physical_, requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            coherent = true;
        } catch (const RendererError&) {
            typeIndex = findMemoryType(physical_, requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
            coherent = false;
        }
        alloc.memoryTypeIndex = typeIndex;
        VkDeviceMemory raw = VK_NULL_HANDLE;
        VK_CHECK(vkAllocateMemory(device_, &alloc, nullptr, &raw), "terrain staging memory allocation");
        VK_CHECK(vkBindBufferMemory(device_, buffer, raw, 0), "terrain staging buffer bind");
        return std::make_tuple(raw, coherent, requirements.size);
    };

    // Allocate device-local memory for the final vertex/index buffers.
    const auto allocateDeviceLocal = [&](const VkBuffer buffer) {
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, buffer, &requirements);
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = requirements.size;
        alloc.memoryTypeIndex = findMemoryType(physical_, requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VkDeviceMemory raw = VK_NULL_HANDLE;
        VK_CHECK(vkAllocateMemory(device_, &alloc, nullptr, &raw), "terrain device memory allocation");
        VK_CHECK(vkBindBufferMemory(device_, buffer, raw, 0), "terrain device buffer bind");
        return raw;
    };

    // Map the entire allocation, copy payload bytes, and flush if non-coherent.
    // Using VK_WHOLE_SIZE for the flush range is always spec-valid when the
    // entire allocation is mapped, regardless of nonCoherentAtomSize.
    const auto fillStaging = [&](const VkDeviceMemory memory, const VkDeviceSize allocationSize,
                                  const VkDeviceSize payloadSize, const void* data, bool coherent) {
        void* mapped = nullptr;
        VK_CHECK(vkMapMemory(device_, memory, 0, allocationSize, 0, &mapped), "terrain staging map");
        std::memcpy(mapped, data, static_cast<std::size_t>(payloadSize));
        if (!coherent) {
            VkMappedMemoryRange range{};
            range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory = memory;
            range.offset = 0;
            range.size = VK_WHOLE_SIZE; // Always spec-valid for full-allocation mapping
            VK_CHECK(vkFlushMappedMemoryRanges(device_, 1, &range), "terrain staging flush");
        }
        vkUnmapMemory(device_, memory);
    };

    // One-time copy command on the graphics queue.
    const auto submitCopy = [&](const VkBuffer src, const VkBuffer dst, const VkDeviceSize size) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = queueFamily_;
        VkCommandPool rawPool = VK_NULL_HANDLE;
        VK_CHECK(vkCreateCommandPool(device_, &poolInfo, nullptr, &rawPool), "terrain upload command pool");
        VkCommandBufferAllocateInfo commandInfo{};
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandInfo.commandPool = rawPool;
        commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandInfo.commandBufferCount = 1;
        VkCommandBuffer command = VK_NULL_HANDLE;
        VK_CHECK(vkAllocateCommandBuffers(device_, &commandInfo, &command), "terrain upload command allocation");
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(command, &beginInfo), "terrain upload command begin");
        VkBufferCopy copy{};
        copy.srcOffset = 0;
        copy.dstOffset = 0;
        copy.size = size;
        vkCmdCopyBuffer(command, src, dst, 1, &copy);
        VK_CHECK(vkEndCommandBuffer(command), "terrain upload command end");
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &command;
        VK_CHECK(vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE), "terrain upload submit");
        VK_CHECK(vkQueueWaitIdle(queue_), "terrain upload wait");
        vkDestroyCommandPool(device_, rawPool, nullptr);
    };

    TileGpu gpu;

    // Vertex buffer: staging → device-local.
    VkBuffer vertexStaging = createBuffer(vertexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    auto [vertexStagingMemory, vertexCoherent, vertexAllocSize] = allocateStaging(vertexStaging);
    fillStaging(vertexStagingMemory, vertexAllocSize, vertexBytes, payload.vertices.data(), vertexCoherent);
    gpu.vertexBuffer = createBuffer(vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    gpu.vertexMemory = allocateDeviceLocal(gpu.vertexBuffer);
    submitCopy(vertexStaging, gpu.vertexBuffer, vertexBytes);
    vkDestroyBuffer(device_, vertexStaging, nullptr);
    vkFreeMemory(device_, vertexStagingMemory, nullptr);

    // Index buffer: staging → device-local.
    VkBuffer indexStaging = createBuffer(indexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    auto [indexStagingMemory, indexCoherent, indexAllocSize] = allocateStaging(indexStaging);
    fillStaging(indexStagingMemory, indexAllocSize, indexBytes, payload.indices.data(), indexCoherent);
    gpu.indexBuffer = createBuffer(indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    gpu.indexMemory = allocateDeviceLocal(gpu.indexBuffer);
    submitCopy(indexStaging, gpu.indexBuffer, indexBytes);
    vkDestroyBuffer(device_, indexStaging, nullptr);
    vkFreeMemory(device_, indexStagingMemory, nullptr);

    gpu.indexCount = static_cast<std::uint32_t>(payload.indices.size());
    return gpu;
}

void TerrainPass::destroyGpu(TileGpu& gpu) const {
    if (gpu.indexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device_, gpu.indexMemory, nullptr);
        gpu.indexMemory = VK_NULL_HANDLE;
    }
    if (gpu.indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, gpu.indexBuffer, nullptr);
        gpu.indexBuffer = VK_NULL_HANDLE;
    }
    if (gpu.vertexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device_, gpu.vertexMemory, nullptr);
        gpu.vertexMemory = VK_NULL_HANDLE;
    }
    if (gpu.vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, gpu.vertexBuffer, nullptr);
        gpu.vertexBuffer = VK_NULL_HANDLE;
    }
    gpu.indexCount = 0;
}

void TerrainPass::logTileFailure(const TerrainSceneTile& tile, const std::string_view detail) const {
    runtime::logError("viewport", "terrain.tile_load_failed",
        {{"dataset", tile.datasetUuid},
            {"chunk", std::to_string(tile.chunkX) + "," + std::to_string(tile.chunkY)},
            {"detail", std::string{detail}}});
}

void TerrainPass::update(const GridCamera& camera) {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }

    // Adopt a pending scene swap (control path handed it over).
    std::optional<TerrainScene> adopted;
    {
        std::lock_guard lock{sceneMutex_};
        adopted = std::move(pendingScene_);
        pendingScene_.reset();
    }
    if (adopted.has_value()) {
        cache_.adoptScene(*adopted);
        renderFrame_ = domain::geo::RenderLocalFrame::atRenderOrigin(
            domain::geo::ProjectGlobalPosition{
                .easting = adopted->originEasting,
                .northing = adopted->originNorthing,
                .height = adopted->originHeight});
    }

    const TerrainCameraState state{
        .centerX = camera.centerWorldX(),
        .centerY = camera.centerWorldY(),
        .metersPerPixel = camera.metersPerPixel(),
        .viewportWidth = static_cast<double>(camera.viewportWidth()),
        .viewportHeight = static_cast<double>(camera.viewportHeight()),
        .originEasting = renderFrame_.has_value() ? renderFrame_->renderOrigin().easting : 0.0,
        .originNorthing = renderFrame_.has_value() ? renderFrame_->renderOrigin().northing : 0.0,
    };

    const TerrainTileCache::UpdateResult result = cache_.update(state);

    for (const TerrainTileCache::Entry* entry : result.toRelease) {
        const TerrainTileId key{entry->tile.datasetUuid, {entry->tile.chunkX, entry->tile.chunkY}};
        const auto found = payloads_.find(key);
        if (found != payloads_.end()) {
            if (found->second.gpu.has_value()) {
                destroyGpu(*found->second.gpu);
            }
            payloads_.erase(found);
        }
        cache_.notifyReleased(entry->tile.datasetUuid, entry->tile.chunkX, entry->tile.chunkY);
    }

    for (const TerrainTileCache::Entry* entry : result.toLoad) {
        const TerrainSceneTile& tile = entry->tile;
        const TerrainTileId key{tile.datasetUuid, {tile.chunkX, tile.chunkY}};
        // For LOD replacement (LodReplacing state), keep the old GPU payload
        // drawable until the new LOD is successfully uploaded, then atomically
        // replace it. For initial loads and stale reloads, destroy any
        // previous buffer before the new content lands.
        const bool isLodReplacement = entry->residency == TerrainTileCache::Residency::LodReplacing;
        std::optional<TileGpu> oldGpu;
        const auto existing = payloads_.find(key);
        if (existing != payloads_.end()) {
            if (isLodReplacement) {
                // Hold the old GPU buffers aside; destroy only on success.
                if (existing->second.gpu.has_value()) {
                    oldGpu = std::move(existing->second.gpu);
                }
                // Remove the CPU payload entry; the old GPU buffers are kept
                // aside and re-inserted on failure.
                payloads_.erase(existing);
            } else {
                if (existing->second.gpu.has_value()) {
                    destroyGpu(*existing->second.gpu);
                }
                payloads_.erase(existing);
            }
        }

        try {
            if (!renderFrame_.has_value()) {
                throw domain::terrain::TerrainTileFormatError("no render-local frame for terrain tiles");
            }
            TilePayload payload = buildPayload(tile, tile.path, entry->desiredLod);
            payload.gpu = uploadPayload(payload);
            const auto inserted = payloads_.emplace(key, std::move(payload));
            cache_.notifyLoaded(tile.datasetUuid, tile.chunkX, tile.chunkY, entry->desiredLod, true);
            (void)inserted;
            // LOD replacement succeeded: destroy the old GPU buffers now.
            if (oldGpu.has_value()) {
                destroyGpu(*oldGpu);
            }
        } catch (const domain::terrain::TerrainTileFormatError& error) {
            // Stale or corrupt tile content: rejected, reported, retried on
            // demand — never interpreted leniently.
            cache_.notifyLoaded(tile.datasetUuid, tile.chunkX, tile.chunkY, entry->desiredLod, false);
            logTileFailure(tile, error.what());
            // LOD replacement failed: restore the old GPU payload so the tile
            // remains drawable at its previous LOD (no flicker, no drop).
            if (oldGpu.has_value()) {
                TilePayload fallback = buildPayload(tile, tile.path, entry->loadedLod);
                fallback.gpu = std::move(oldGpu);
                payloads_.emplace(key, std::move(fallback));
            }
        } catch (const std::exception& error) {
            cache_.notifyLoaded(tile.datasetUuid, tile.chunkX, tile.chunkY, entry->desiredLod, false);
            logTileFailure(tile, error.what());
            if (oldGpu.has_value()) {
                TilePayload fallback = buildPayload(tile, tile.path, entry->loadedLod);
                fallback.gpu = std::move(oldGpu);
                payloads_.emplace(key, std::move(fallback));
            }
        }
    }
}

void TerrainPass::record(const VkCommandBuffer command, const GridCamera& camera) const {
    if (device_ == VK_NULL_HANDLE || payloads_.empty()) {
        return;
    }
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.get());

    const VkViewport viewport{
        0.0F, 0.0F,
        static_cast<float>(camera.viewportWidth()),
        static_cast<float>(camera.viewportHeight()),
        0.0F, 1.0F};
    const VkRect2D scissor{
        VkOffset2D{0, 0},
        VkExtent2D{
            static_cast<std::uint32_t>(camera.viewportWidth()),
            static_cast<std::uint32_t>(camera.viewportHeight())}};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);

    std::array<float, 16> viewProj{};
    camera.writeViewProjection(viewProj.data());
    vkCmdPushConstants(
        command, pipelineLayout_.get(), VK_SHADER_STAGE_VERTEX_BIT, 0,
        static_cast<std::uint32_t>(viewProj.size() * sizeof(float)), viewProj.data());

    for (const auto& [id, payload] : payloads_) {
        if (!payload.gpu.has_value() || payload.gpu->indexCount == 0) {
            continue;
        }
        const VkDeviceSize offset = 0;
        const VkBuffer vertexBuffer = payload.gpu->vertexBuffer;
        vkCmdBindVertexBuffers(command, 0, 1, &vertexBuffer, &offset);
        vkCmdBindIndexBuffer(command, payload.gpu->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(command, payload.gpu->indexCount, 1, 0, 0, 0);
    }
}

} // namespace infraforge::viewport
