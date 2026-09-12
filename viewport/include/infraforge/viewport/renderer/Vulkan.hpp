#pragma once

#include <vulkan/vulkan.h>

#include <functional>
#include <stdexcept>
#include <string>
#include <utility>

namespace infraforge::viewport {

// Renderer failures are explicit and diagnostic-rich; they map to the
// renderer's Failed state and surface in the editor UI.
class RendererError : public std::runtime_error {
public:
    RendererError(std::string message, VkResult result)
        : std::runtime_error(std::move(message)),
          result_(result) {}

    [[nodiscard]] VkResult result() const noexcept { return result_; }

private:
    VkResult result_;
};

[[nodiscard]] std::string vulkanResultName(VkResult result);

#define VK_CHECK(expression, context)                                                    \
    do {                                                                                 \
        const VkResult checkResult = (expression);                                        \
        if (checkResult != VK_SUCCESS) {                                                 \
            throw infraforge::viewport::RendererError(                                   \
                std::string{context} + " failed", checkResult);                           \
        }                                                                                 \
    } while (false)

// RAII holder for Vulkan objects destroyed through instance/device handles.
// Move-only; destruction runs the stored destroyer exactly once.
template <typename Handle>
class UniqueVulkan {
public:
    using Destroyer = std::function<void(Handle)>;

    UniqueVulkan() = default;
    UniqueVulkan(Handle handle, Destroyer destroyer)
        : handle_(handle),
          destroyer_(std::move(destroyer)) {}

    ~UniqueVulkan() { reset(); }

    UniqueVulkan(const UniqueVulkan&) = delete;
    UniqueVulkan& operator=(const UniqueVulkan&) = delete;

    UniqueVulkan(UniqueVulkan&& other) noexcept
        : handle_(std::exchange(other.handle_, VK_NULL_HANDLE)),
          destroyer_(std::move(other.destroyer_)) {}

    UniqueVulkan& operator=(UniqueVulkan&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, VK_NULL_HANDLE);
            destroyer_ = std::move(other.destroyer_);
        }
        return *this;
    }

    void reset() {
        if (handle_ != VK_NULL_HANDLE && destroyer_) {
            destroyer_(handle_);
        }
        handle_ = VK_NULL_HANDLE;
        destroyer_ = nullptr;
    }

    [[nodiscard]] Handle get() const noexcept { return handle_; }
    [[nodiscard]] bool valid() const noexcept { return handle_ != VK_NULL_HANDLE; }

    [[nodiscard]] Handle release() noexcept {
        return std::exchange(handle_, VK_NULL_HANDLE);
    }

private:
    Handle handle_{VK_NULL_HANDLE};
    Destroyer destroyer_;
};

} // namespace infraforge::viewport
