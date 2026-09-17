#include "infraforge/viewport/renderer/RoadPass.hpp"

#include "infraforge/runtime/Logging.hpp"
#include "infraforge/viewport/renderer/Vulkan.hpp"

#include <ViewportShaderSource.h>

#include <array>
#include <cstring>
#include <shaderc/shaderc.hpp>

namespace infraforge::viewport {
namespace {

constexpr std::size_t kPosFloats = 3;
constexpr std::size_t kNormalFloats = 3;
constexpr std::size_t kVertexFloats = kPosFloats + kNormalFloats;

std::uint64_t meshFingerprint(const RoadSceneMesh& mesh) {
    constexpr std::uint64_t kOffset = 1469598103934665603ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t value = kOffset;
    const auto hashBytes = [&](const void* data, const std::size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) value = (value ^ bytes[i]) * kPrime;
    };
    hashBytes(mesh.vertices.data(), mesh.vertices.size() * sizeof(RoadSceneVertex));
    hashBytes(mesh.indices.data(), mesh.indices.size() * sizeof(std::uint32_t));
    return value;
}

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
    VkPhysicalDevice physical,
    std::uint32_t requiredBits,
    VkMemoryPropertyFlags properties) {
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

RoadPass::~RoadPass() {
    destroy();
}

void RoadPass::create(
    VkPhysicalDevice physical,
    VkDevice device,
    VkQueue queue,
    std::uint32_t queueFamily,
    VkRenderPass renderPass) {
    physical_ = physical;
    device_ = device;
    queue_ = queue;
    queueFamily_ = queueFamily;
    renderPass_ = renderPass;

    const std::vector<std::uint32_t> vertexSpirv = compileShader(
        shaders::kRoadVertex, shaderc_glsl_vertex_shader, "road.vert");
    const std::vector<std::uint32_t> fragmentSpirv = compileShader(
        shaders::kRoadFragment, shaderc_glsl_fragment_shader, "road.frag");

    VkShaderModuleCreateInfo vertexInfo{};
    vertexInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertexInfo.codeSize = vertexSpirv.size() * sizeof(std::uint32_t);
    vertexInfo.pCode = vertexSpirv.data();
    VkShaderModule rawVertex = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(device_, &vertexInfo, nullptr, &rawVertex),
        "road vertex shader module creation");
    UniqueVulkan<VkShaderModule> vertexShader{rawVertex, [device](VkShaderModule module) {
        vkDestroyShaderModule(device, module, nullptr);
    }};

    VkShaderModuleCreateInfo fragmentInfo{};
    fragmentInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragmentInfo.codeSize = fragmentSpirv.size() * sizeof(std::uint32_t);
    fragmentInfo.pCode = fragmentSpirv.data();
    VkShaderModule rawFragment = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(device_, &fragmentInfo, nullptr, &rawFragment),
        "road fragment shader module creation");
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
        "road pipeline layout creation");
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
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_.get();
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;
    VkPipeline rawPipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &rawPipeline),
        "road graphics pipeline creation");
    pipeline_ = UniqueVulkan<VkPipeline>{rawPipeline, [device](VkPipeline pipeline) {
        vkDestroyPipeline(device, pipeline, nullptr);
    }};
}

void RoadPass::destroy() {
    if (device_ == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(device_);
    for (auto& [key, entry] : meshes_) {
        (void)key;
        releaseMesh(entry.gpu);
    }
    meshes_.clear();
    for (auto& retired : retiredMeshes_) releaseMesh(retired.gpu);
    retiredMeshes_.clear();
    pipeline_.reset();
    pipelineLayout_.reset();
    device_ = VK_NULL_HANDLE;
}

void RoadPass::setScene(const RoadScene& scene) {
    std::lock_guard lock{sceneMutex_};
    pendingScene_ = scene;
}

void RoadPass::update(const EditorCamera& camera) {
    (void)camera;
    for (auto it = retiredMeshes_.begin(); it != retiredMeshes_.end();) {
        if (--it->framesRemaining == 0) {
            releaseMesh(it->gpu);
            it = retiredMeshes_.erase(it);
        } else {
            ++it;
        }
    }
    std::optional<RoadScene> adopted;
    {
        std::lock_guard lock{sceneMutex_};
        if (pendingScene_.has_value()) {
            adopted = std::move(pendingScene_);
            pendingScene_.reset();
        }
    }
    if (!adopted.has_value()) return;

    std::unordered_map<std::string, const RoadSceneMesh*> incoming;
    for (const auto& mesh : adopted->meshes) {
        if (!mesh.isEmpty()) incoming.emplace(mesh.key(), &mesh);
    }
    for (auto it = meshes_.begin(); it != meshes_.end();) {
        if (!incoming.contains(it->first)) {
            retiredMeshes_.push_back({.gpu = it->second.gpu});
            it = meshes_.erase(it);
        } else ++it;
    }
    for (const auto& [key, mesh] : incoming) {
        const auto fingerprint = meshFingerprint(*mesh);
        const auto existing = meshes_.find(key);
        if (existing != meshes_.end() && existing->second.fingerprint == fingerprint) continue;
        const auto replacement = uploadMesh(*mesh);
        if (existing != meshes_.end()) {
            retiredMeshes_.push_back({.gpu = existing->second.gpu});
            existing->second = replacement;
        } else {
            meshes_.emplace(key, replacement);
        }
    }
}

RoadPass::MeshEntry RoadPass::uploadMesh(const RoadSceneMesh& mesh) {

    MeshEntry entry;
    entry.roadId = mesh.roadId;
    entry.fingerprint = meshFingerprint(mesh);
    entry.vertices = mesh.vertices;
    entry.indices = mesh.indices;

    // Build the vertex buffer data: pos(3) + normal(3) per vertex.
    std::vector<float> vertexData;
    vertexData.reserve(mesh.vertices.size() * kVertexFloats);
    for (const auto& v : mesh.vertices) {
        vertexData.push_back(v.x);
        vertexData.push_back(v.y);
        vertexData.push_back(v.z);
        vertexData.push_back(v.nx);
        vertexData.push_back(v.ny);
        vertexData.push_back(v.nz);
    }

    const VkDeviceSize vertexSize = vertexData.size() * sizeof(float);
    const VkDeviceSize indexSize = mesh.indices.size() * sizeof(std::uint32_t);

    // Vertex buffer.
    VkBufferCreateInfo vertexBufferInfo{};
    vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vertexBufferInfo.size = vertexSize;
    vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vertexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VK_CHECK(vkCreateBuffer(device_, &vertexBufferInfo, nullptr, &vertexBuffer),
        "road vertex buffer creation");

    VkMemoryRequirements vertexReqs{};
    vkGetBufferMemoryRequirements(device_, vertexBuffer, &vertexReqs);
    VkMemoryAllocateInfo vertexAlloc{};
    vertexAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    vertexAlloc.allocationSize = vertexReqs.size;
    vertexAlloc.memoryTypeIndex = findMemoryType(physical_,
        vertexReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateMemory(device_, &vertexAlloc, nullptr, &vertexMemory),
        "road vertex memory allocation");
    VK_CHECK(vkBindBufferMemory(device_, vertexBuffer, vertexMemory, 0),
        "road vertex memory bind");

    void* vertexMapped = nullptr;
    VK_CHECK(vkMapMemory(device_, vertexMemory, 0, vertexSize, 0, &vertexMapped),
        "road vertex memory map");
    std::memcpy(vertexMapped, vertexData.data(), vertexSize);
    vkUnmapMemory(device_, vertexMemory);

    entry.gpu.vertexBuffer = vertexBuffer;
    entry.gpu.vertexMemory = vertexMemory;
    entry.gpu.vertexCount = static_cast<std::uint32_t>(mesh.vertices.size());

    // Index buffer.
    VkBufferCreateInfo indexBufferInfo{};
    indexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    indexBufferInfo.size = indexSize;
    indexBufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    indexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VK_CHECK(vkCreateBuffer(device_, &indexBufferInfo, nullptr, &indexBuffer),
        "road index buffer creation");

    VkMemoryRequirements indexReqs{};
    vkGetBufferMemoryRequirements(device_, indexBuffer, &indexReqs);
    VkMemoryAllocateInfo indexAlloc{};
    indexAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    indexAlloc.allocationSize = indexReqs.size;
    indexAlloc.memoryTypeIndex = findMemoryType(physical_,
        indexReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateMemory(device_, &indexAlloc, nullptr, &indexMemory),
        "road index memory allocation");
    VK_CHECK(vkBindBufferMemory(device_, indexBuffer, indexMemory, 0),
        "road index memory bind");

    void* indexMapped = nullptr;
    VK_CHECK(vkMapMemory(device_, indexMemory, 0, indexSize, 0, &indexMapped),
        "road index memory map");
    std::memcpy(indexMapped, mesh.indices.data(), indexSize);
    vkUnmapMemory(device_, indexMemory);

    entry.gpu.indexBuffer = indexBuffer;
    entry.gpu.indexMemory = indexMemory;
    entry.gpu.indexCount = static_cast<std::uint32_t>(mesh.indices.size());

    return entry;
}

void RoadPass::releaseMesh(MeshGpu& gpu) {
    if (device_ == VK_NULL_HANDLE) return;
    if (gpu.vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, gpu.vertexBuffer, nullptr);
        gpu.vertexBuffer = VK_NULL_HANDLE;
    }
    if (gpu.vertexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device_, gpu.vertexMemory, nullptr);
        gpu.vertexMemory = VK_NULL_HANDLE;
    }
    if (gpu.indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, gpu.indexBuffer, nullptr);
        gpu.indexBuffer = VK_NULL_HANDLE;
    }
    if (gpu.indexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device_, gpu.indexMemory, nullptr);
        gpu.indexMemory = VK_NULL_HANDLE;
    }
    gpu.indexCount = 0;
    gpu.vertexCount = 0;
}

void RoadPass::record(VkCommandBuffer command, const EditorCamera& camera) const {
    if (meshes_.empty() || pipeline_.get() == VK_NULL_HANDLE) return;

    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.get());

    std::array<float, 16> viewProj{};
    camera.writeViewProjection(viewProj.data());
    vkCmdPushConstants(command, pipelineLayout_.get(),
        VK_SHADER_STAGE_VERTEX_BIT, 0,
        static_cast<std::uint32_t>(viewProj.size() * sizeof(float)), viewProj.data());

    for (const auto& [key, entry] : meshes_) {
        (void)key;
        if (entry.gpu.indexCount == 0) continue;
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(command, 0, 1, &entry.gpu.vertexBuffer, &offset);
        vkCmdBindIndexBuffer(command, entry.gpu.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(command, entry.gpu.indexCount, 1, 0, 0, 0);
    }
}

std::string RoadPass::pickRoad(const CameraPoint3d& projectPoint,
    const CameraPoint3d& renderOrigin) const {
    const double px = projectPoint.x - renderOrigin.x;
    const double py = projectPoint.y - renderOrigin.y;
    const auto signedArea = [](const RoadSceneVertex& a, const RoadSceneVertex& b,
        const double x, const double y) {
        return (static_cast<double>(b.x) - a.x) * (y - a.y)
            - (static_cast<double>(b.y) - a.y) * (x - a.x);
    };
    for (const auto& [key, entry] : meshes_) {
        (void)key;
        for (std::size_t i = 0; i + 2 < entry.indices.size(); i += 3) {
            const auto& a = entry.vertices[entry.indices[i]];
            const auto& b = entry.vertices[entry.indices[i + 1]];
            const auto& c = entry.vertices[entry.indices[i + 2]];
            const double ab = signedArea(a, b, px, py);
            const double bc = signedArea(b, c, px, py);
            const double ca = signedArea(c, a, px, py);
            if ((ab >= 0.0 && bc >= 0.0 && ca >= 0.0) ||
                (ab <= 0.0 && bc <= 0.0 && ca <= 0.0)) {
                if (entry.roadId == "__authoring_preview__") continue;
                return entry.roadId;
            }
        }
    }
    return {};
}

} // namespace infraforge::viewport
