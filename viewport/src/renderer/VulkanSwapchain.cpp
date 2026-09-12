#include "infraforge/viewport/renderer/VulkanSwapchain.hpp"

#include <algorithm>

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

    // Format preference: 8-bit UNORM BGRA; fall back to the first supported.
    std::uint32_t formatCount = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, nullptr),
        "surface format probe");
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, formats.data()),
        "surface format enumeration");
    format_ = formats.front().format;
    for (const VkSurfaceFormatKHR& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM
            && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            format_ = VK_FORMAT_B8G8R8A8_UNORM;
            break;
        }
    }

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
    createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
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
    // Framebuffers, views, and the swapchain are torn down; the owned render
    // pass survives recreations because its attachment format is stable and
    // pipelines are built against it. Its UniqueVulkan destructor releases
    // it at final object destruction (before the device goes away).
    for (const VkFramebuffer framebuffer : framebuffers_) {
        vkDestroyFramebuffer(device_, framebuffer, nullptr);
    }
    framebuffers_.clear();
    imageViews_.clear();
    images_.clear();
    swapchain_.reset();
}

void VulkanSwapchain::createRenderPass() {
    VkAttachmentDescription color{};
    color.format = format_;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = 1;
    createInfo.pAttachments = &color;
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

    imageViews_.reserve(imageCount);
    framebuffers_.reserve(imageCount);

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

        const VkImageView imageView = imageViews_.back().get();
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass_;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &imageView;
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
