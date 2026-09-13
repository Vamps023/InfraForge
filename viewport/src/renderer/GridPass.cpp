#include "infraforge/viewport/renderer/GridPass.hpp"

#include <ViewportShaderSource.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <shaderc/shaderc.hpp>
#include <vector>

namespace infraforge::viewport {
namespace {

struct GridVertex {
    float x;
    float y;
    float r;
    float g;
    float b;
};

// World-space grid: +/-200 m around the origin, 1 m minor lines, 10 m major
// lines, and emphasized X/Y axes. The buffer is static; the camera projects
// whatever part of it is visible.
std::vector<GridVertex> buildGridGeometry() {
    constexpr double kExtent = 200.0;
    constexpr double kMinorStep = 1.0;
    constexpr double kMajorStep = 10.0;
    constexpr double kEpsilon = 1e-9;

    std::vector<GridVertex> vertices;
    vertices.reserve(2200);

    const auto pushLine = [&](const double ax, const double ay, const double bx, const double by,
                              const float r, const float g, const float b) {
        vertices.push_back({static_cast<float>(ax), static_cast<float>(ay), r, g, b});
        vertices.push_back({static_cast<float>(bx), static_cast<float>(by), r, g, b});
    };

    constexpr float kMinorR = 0.13F, kMinorG = 0.16F, kMinorB = 0.20F;
    constexpr float kMajorR = 0.22F, kMajorG = 0.27F, kMajorB = 0.33F;
    constexpr float kAxisXR = 0.55F, kAxisXG = 0.28F, kAxisXB = 0.28F;
    constexpr float kAxisYR = 0.28F, kAxisYG = 0.52F, kAxisYB = 0.33F;

    for (double value = -kExtent; value <= kExtent + kEpsilon; value += kMinorStep) {
        if (std::abs(value) < kEpsilon) {
            continue; // axes replace the origin lines
        }
        const bool major = std::fmod(std::abs(value) + kEpsilon, kMajorStep) < 2 * kEpsilon;
        if (major) {
            continue; // drawn in the major pass below
        }
        pushLine(value, -kExtent, value, kExtent, kMinorR, kMinorG, kMinorB);
        pushLine(-kExtent, value, kExtent, value, kMinorR, kMinorG, kMinorB);
    }
    for (double value = -kExtent; value <= kExtent + kEpsilon; value += kMajorStep) {
        if (std::abs(value) < kEpsilon) {
            continue;
        }
        pushLine(value, -kExtent, value, kExtent, kMajorR, kMajorG, kMajorB);
        pushLine(-kExtent, value, kExtent, value, kMajorR, kMajorG, kMajorB);
    }
    pushLine(-kExtent, 0.0, kExtent, 0.0, kAxisXR, kAxisXG, kAxisXB);
    pushLine(0.0, -kExtent, 0.0, kExtent, kAxisYR, kAxisYG, kAxisYB);

    return vertices;
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

void GridPass::create(
    const VkPhysicalDevice physical,
    const VkDevice device,
    const VkQueue queue,
    const std::uint32_t queueFamily,
    const VkRenderPass renderPass) {
    device_ = device;
    physical_ = physical;
    queue_ = queue;
    queueFamily_ = queueFamily;
    renderPass_ = renderPass;

    const std::vector<std::uint32_t> vertexSpirv = compileShader(
        shaders::kGridVertex, shaderc_glsl_vertex_shader, "grid.vert");
    const std::vector<std::uint32_t> fragmentSpirv = compileShader(
        shaders::kGridFragment, shaderc_glsl_fragment_shader, "grid.frag");

    VkShaderModuleCreateInfo vertexInfo{};
    vertexInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertexInfo.codeSize = vertexSpirv.size() * sizeof(std::uint32_t);
    vertexInfo.pCode = vertexSpirv.data();
    VkShaderModule rawVertex = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(device_, &vertexInfo, nullptr, &rawVertex),
        "vertex shader module creation");
    vertexShader_ = UniqueVulkan<VkShaderModule>{rawVertex, [device](VkShaderModule module) {
        vkDestroyShaderModule(device, module, nullptr);
    }};

    VkShaderModuleCreateInfo fragmentInfo{};
    fragmentInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragmentInfo.codeSize = fragmentSpirv.size() * sizeof(std::uint32_t);
    fragmentInfo.pCode = fragmentSpirv.data();
    VkShaderModule rawFragment = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(device_, &fragmentInfo, nullptr, &rawFragment),
        "fragment shader module creation");
    fragmentShader_ = UniqueVulkan<VkShaderModule>{rawFragment, [device](VkShaderModule module) {
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
        "pipeline layout creation");
    pipelineLayout_ = UniqueVulkan<VkPipelineLayout>{rawLayout, [device](VkPipelineLayout layout) {
        vkDestroyPipelineLayout(device, layout, nullptr);
    }};

    createPipeline();
    createVertexBuffer();
}

void GridPass::createPipeline() {
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexShader_.get();
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentShader_.get();
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(GridVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attributes{};
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[0].offset = offsetof(GridVertex, x);
    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = offsetof(GridVertex, r);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

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
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_.get();
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &rawPipeline),
        "grid pipeline creation");
    pipeline_ = UniqueVulkan<VkPipeline>{rawPipeline, [device = device_](VkPipeline pipeline) {
        vkDestroyPipeline(device, pipeline, nullptr);
    }};
}

void GridPass::createVertexBuffer() {
    const std::vector<GridVertex> vertices = buildGridGeometry();
    const VkDeviceSize byteSize = static_cast<VkDeviceSize>(vertices.size() * sizeof(GridVertex));
    vertexCount_ = static_cast<VkDeviceSize>(vertices.size());

    // 1. Staging buffer (host visible) filled from CPU memory. RAII holders
    // release the temporaries if any later step throws.
    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = byteSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer rawStagingBuffer = VK_NULL_HANDLE;
    VK_CHECK(vkCreateBuffer(device_, &stagingInfo, nullptr, &rawStagingBuffer),
        "staging buffer creation");
    UniqueVulkan<VkBuffer> stagingBuffer{rawStagingBuffer, [device = device_](VkBuffer buffer) {
        vkDestroyBuffer(device, buffer, nullptr);
    }};

    VkMemoryRequirements stagingRequirements{};
    vkGetBufferMemoryRequirements(device_, stagingBuffer.get(), &stagingRequirements);
    VkMemoryAllocateInfo stagingAlloc{};
    stagingAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingAlloc.allocationSize = stagingRequirements.size;
    stagingAlloc.memoryTypeIndex = findMemoryType(
        physical_, stagingRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory rawStagingMemory = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateMemory(device_, &stagingAlloc, nullptr, &rawStagingMemory),
        "staging memory allocation");
    UniqueVulkan<VkDeviceMemory> stagingMemory{rawStagingMemory,
        [device = device_](VkDeviceMemory memory) { vkFreeMemory(device, memory, nullptr); }};
    VK_CHECK(vkBindBufferMemory(device_, stagingBuffer.get(), stagingMemory.get(), 0),
        "staging buffer bind");

    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(device_, stagingMemory.get(), 0, byteSize, 0, &mapped), "staging map");
    std::memcpy(mapped, vertices.data(), static_cast<std::size_t>(byteSize));
    vkUnmapMemory(device_, stagingMemory.get());

    // 2. Device-local destination buffer.
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = byteSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer rawBuffer = VK_NULL_HANDLE;
    VK_CHECK(vkCreateBuffer(device_, &bufferInfo, nullptr, &rawBuffer), "grid vertex buffer creation");
    vertexBuffer_ = UniqueVulkan<VkBuffer>{rawBuffer, [device = device_](VkBuffer buffer) {
        vkDestroyBuffer(device, buffer, nullptr);
    }};

    VkMemoryRequirements deviceRequirements{};
    vkGetBufferMemoryRequirements(device_, vertexBuffer_.get(), &deviceRequirements);
    VkMemoryAllocateInfo deviceAlloc{};
    deviceAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    deviceAlloc.allocationSize = deviceRequirements.size;
    deviceAlloc.memoryTypeIndex = findMemoryType(
        physical_, deviceRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory rawMemory = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateMemory(device_, &deviceAlloc, nullptr, &rawMemory),
        "grid vertex memory allocation");
    vertexMemory_ = UniqueVulkan<VkDeviceMemory>{rawMemory, [device = device_](VkDeviceMemory memory) {
        vkFreeMemory(device, memory, nullptr);
    }};
    VK_CHECK(vkBindBufferMemory(device_, vertexBuffer_.get(), vertexMemory_.get(), 0),
        "grid vertex buffer bind");

    // 3. One-shot copy command on the graphics queue.
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = queueFamily_;
    VkCommandPool rawPool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateCommandPool(device_, &poolInfo, nullptr, &rawPool), "upload command pool creation");
    UniqueVulkan<VkCommandPool> uploadPool{rawPool, [device = device_](VkCommandPool pool) {
        vkDestroyCommandPool(device, pool, nullptr);
    }};

    VkCommandBufferAllocateInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandInfo.commandPool = uploadPool.get();
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(device_, &commandInfo, &command), "upload command buffer allocation");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(command, &beginInfo), "upload command begin");

    VkBufferCopy copy{};
    copy.srcOffset = 0;
    copy.dstOffset = 0;
    copy.size = byteSize;
    vkCmdCopyBuffer(command, stagingBuffer.get(), vertexBuffer_.get(), 1, &copy);
    VK_CHECK(vkEndCommandBuffer(command), "upload command end");

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &command;
    VK_CHECK(vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE), "upload submit");
    VK_CHECK(vkQueueWaitIdle(queue_), "upload wait");

    // The staging buffer/memory and upload pool release at scope end.
}

void GridPass::destroy() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    vertexMemory_.reset();
    vertexBuffer_.reset();
    pipeline_.reset();
    pipelineLayout_.reset();
    fragmentShader_.reset();
    vertexShader_.reset();
    queue_ = VK_NULL_HANDLE;
    renderPass_ = VK_NULL_HANDLE;
    physical_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
}

void GridPass::record(
    const VkCommandBuffer commandBuffer,
    const GridCamera& camera) const {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.get());

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
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    std::array<float, 16> viewProj{};
    camera.writeViewProjection(viewProj.data());
    vkCmdPushConstants(
        commandBuffer, pipelineLayout_.get(), VK_SHADER_STAGE_VERTEX_BIT, 0,
        static_cast<std::uint32_t>(viewProj.size() * sizeof(float)), viewProj.data());

    const VkDeviceSize offset = 0;
    const VkBuffer vertexBufferHandle = vertexBuffer_.get();
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBufferHandle, &offset);
    vkCmdDraw(commandBuffer, static_cast<std::uint32_t>(vertexCount_), 1, 0, 0);
}

} // namespace infraforge::viewport
