#include "infraforge/viewport/renderer/VulkanSwapchain.hpp"

#include "infraforge/viewport/renderer/SwapchainState.hpp"

#include <cstdint>

#include <algorithm>
#include <array>
#include <limits>

namespace infraforge::viewport {

void VulkanSwapchain::create(
    const VkPhysicalDevice physical,
    const VkDevice device,
    const VkSurfaceKHR surface,
    const std::uint32_t graphicsFamily,
    const VkRenderPass renderPass,
    const std::uint32_t width,
    const std::uint32_t height) {
    destroy();
    physical_ = physical;
    // Null render pass means "create and own the standard grid pass on first
    // creation"; the owned pass then survives recreations because its
    // attachment format is stable and pipelines are built against it.
    device_ = device;
    surface_ = surface;
    graphicsFamily_ = graphicsFamily;
    if (renderPass != VK_NULL_HANDLE) {
        renderPass_ = renderPass;
    } else if (renderPass_ == VK_NULL_HANDLE) {
        createRenderPass();
        ownedRenderPass_ = UniqueVulkan<VkRenderPass>{
            renderPass_,
            [device](VkRenderPass pass) { vkDestroyRenderPass(device, pass, nullptr); }};
    }
    extent_ = VkExtent2D{width, height};

    VkSurfaceCapabilitiesKHR capabilities{};
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &capabilities),
        "surface capabilities query");

    extent_.width = std::clamp(
        width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent_.height = std::clamp(
        height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    // Format preference: 8-bit UNORM BGRA with sRGB nonlinear; any other
    // supported pair is taken verbatim (format and colorspace together).
    std::uint32_t formatCount = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, nullptr),
        "surface format probe");
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, formats.data()),
        "surface format enumeration");
    const VkSurfaceFormatKHR chosenFormat = selectSurfaceFormat(formats);
    format_ = chosenFormat.format;
    colorSpace_ = chosenFormat.colorSpace;

    // FIFO is always supported and vsync-appropriate for an editor viewport.
    const VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

    std::uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = format_;
    createInfo.imageColorSpace = colorSpace_;
    createInfo.imageExtent = extent_;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VkSwapchainKHR rawSwapchain = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSwapchainKHR(device_, &createInfo, nullptr, &rawSwapchain), "swapchain creation");
    swapchain_ = UniqueVulkan<VkSwapchainKHR>{
        rawSwapchain,
        [device](VkSwapchainKHR swapchain) { vkDestroySwapchainKHR(device, swapchain, nullptr); }};

    createImageViewsAndFramebuffers();
}

void VulkanSwapchain::destroy() {
    // Framebuffers, depth attachments, views, and the swapchain are torn
    // down; the owned render pass survives recreations because its
    // attachment formats are stable and pipelines are built against it. Its
    // UniqueVulkan destructor releases it at final object destruction
    // (before the device goes away).
    for (const VkFramebuffer framebuffer : framebuffers_) {
        vkDestroyFramebuffer(device_, framebuffer, nullptr);
    }
    framebuffers_.clear();
    depthViews_.clear();
    depthMemories_.clear();
    depthImages_.clear();
    imageViews_.clear();
    images_.clear();
    swapchain_.reset();
}

VkFormat VulkanSwapchain::selectDepthFormat() const {
    const std::array<VkFormat, 4> candidates{
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_X8_D24_UNORM_PACK32,
    };
    for (const VkFormat format : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physical_, format, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
            return format;
        }
    }
    throw RendererError("no supported depth attachment format found", VK_ERROR_INITIALIZATION_FAILED);
}

void VulkanSwapchain::createRenderPass() {
    depthFormat_ = selectDepthFormat();

    VkAttachmentDescription color{};
    color.format = format_;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth{};
    depth.format = depthFormat_;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    std::array<VkAttachmentDescription, 2> attachments{color, depth};

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
        | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
    createInfo.pAttachments = attachments.data();
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;

    VkRenderPass rawPass = VK_NULL_HANDLE;
    VK_CHECK(vkCreateRenderPass(device_, &createInfo, nullptr, &rawPass), "render pass creation");
    renderPass_ = rawPass;
}

void VulkanSwapchain::createImageViewsAndFramebuffers() {
    std::uint32_t imageCount = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(device_, swapchain_.get(), &imageCount, nullptr),
        "swapchain image probe");
    images_.resize(imageCount);
    VK_CHECK(vkGetSwapchainImagesKHR(device_, swapchain_.get(), &imageCount, images_.data()),
        "swapchain image retrieval");

    // Depth format: chosen with the owned render pass (createRenderPass) and
    // stable across recreations, so framebuffers always match the pass.
    imageViews_.reserve(imageCount);
    depthImages_.reserve(imageCount);
    depthMemories_.reserve(imageCount);
    depthViews_.reserve(imageCount);
    framebuffers_.reserve(imageCount);

    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physical_, &memoryProperties);

    for (const VkImage image : images_) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format_;
        viewInfo.components = VkComponentMapping{
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY};
        viewInfo.subresourceRange = VkImageSubresourceRange{
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkImageView rawView = VK_NULL_HANDLE;
        VK_CHECK(vkCreateImageView(device_, &viewInfo, nullptr, &rawView), "swapchain image view creation");
        imageViews_.emplace_back(rawView, [device = device_](VkImageView view) {
            vkDestroyImageView(device, view, nullptr);
        });

        // Per-image depth attachment.
        VkImageCreateInfo depthInfo{};
        depthInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        depthInfo.imageType = VK_IMAGE_TYPE_2D;
        depthInfo.format = depthFormat_;
        depthInfo.extent = VkExtent3D{extent_.width, extent_.height, 1};
        depthInfo.mipLevels = 1;
        depthInfo.arrayLayers = 1;
        depthInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        depthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depthInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        depthInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImage rawDepth = VK_NULL_HANDLE;
        VK_CHECK(vkCreateImage(device_, &depthInfo, nullptr, &rawDepth), "depth image creation");
        depthImages_.push_back(rawDepth);

        VkMemoryRequirements depthRequirements{};
        vkGetImageMemoryRequirements(device_, rawDepth, &depthRequirements);
        std::uint32_t memoryType = std::numeric_limits<std::uint32_t>::max();
        for (std::uint32_t type = 0; type < memoryProperties.memoryTypeCount; ++type) {
            const bool typeAllowed = (depthRequirements.memoryTypeBits & (1U << type)) != 0;
            const bool hasProperties =
                (memoryProperties.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
            if (typeAllowed && hasProperties) {
                memoryType = type;
                break;
            }
        }
        if (memoryType == std::numeric_limits<std::uint32_t>::max()) {
            throw RendererError("no suitable depth memory type found", VK_ERROR_INITIALIZATION_FAILED);
        }
        VkMemoryAllocateInfo depthAlloc{};
        depthAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        depthAlloc.allocationSize = depthRequirements.size;
        depthAlloc.memoryTypeIndex = memoryType;
        VkDeviceMemory rawDepthMemory = VK_NULL_HANDLE;
        VK_CHECK(vkAllocateMemory(device_, &depthAlloc, nullptr, &rawDepthMemory),
            "depth memory allocation");
        depthMemories_.emplace_back(rawDepthMemory, [device = device_](VkDeviceMemory memory) {
            vkFreeMemory(device, memory, nullptr);
        });
        VK_CHECK(vkBindImageMemory(device_, rawDepth, rawDepthMemory, 0), "depth image memory bind");

        VkImageViewCreateInfo depthViewInfo{};
        depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        depthViewInfo.image = rawDepth;
        depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depthViewInfo.format = depthFormat_;
        depthViewInfo.subresourceRange = VkImageSubresourceRange{
            VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        VkImageView rawDepthView = VK_NULL_HANDLE;
        VK_CHECK(vkCreateImageView(device_, &depthViewInfo, nullptr, &rawDepthView),
            "depth image view creation");
        depthViews_.emplace_back(rawDepthView, [device = device_](VkImageView view) {
            vkDestroyImageView(device, view, nullptr);
        });

        const std::array<VkImageView, 2> framebufferAttachments{
            imageViews_.back().get(), depthViews_.back().get()};
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass_;
        framebufferInfo.attachmentCount = static_cast<std::uint32_t>(framebufferAttachments.size());
        framebufferInfo.pAttachments = framebufferAttachments.data();
        framebufferInfo.width = extent_.width;
        framebufferInfo.height = extent_.height;
        framebufferInfo.layers = 1;

        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VK_CHECK(vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &framebuffer),
            "swapchain framebuffer creation");
        framebuffers_.push_back(framebuffer);
    }
}

} // namespace infraforge::viewport
