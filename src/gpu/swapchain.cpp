#include "rf/gpu/swapchain.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <volk.h>

#include "device_impl.hpp"
#include "texture_impl.hpp"
#include "vulkan_util.hpp"

namespace rf::gpu {
namespace {

using detail::from_vulkan;
using detail::image_barrier;

constexpr std::uint64_t kTimeoutNanoseconds = 5ULL * 1000 * 1000 * 1000;

/// Prefers a plain 8-bit BGRA or RGBA surface.
///
/// Deliberately NOT an sRGB format. The compositor works in the same encoding
/// its inputs arrived in, so asking the presentation engine to apply an sRGB
/// transfer would brighten everything on screen relative to what gets exported
/// -- the exact "preview does not match the file" bug ADR 008 keeps the two
/// paths identical to avoid. Managed colour arrives with OCIO at M9.
[[nodiscard]] VkSurfaceFormatKHR choose_format(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const VkSurfaceFormatKHR& format : formats) {
        if ((format.format == VK_FORMAT_B8G8R8A8_UNORM ||
             format.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats.front();
}

}  // namespace

class Swapchain::Impl {
public:
    /// Declared first so it is destroyed last.
    std::shared_ptr<Device::Impl> device;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkSurfaceFormatKHR format{};
    VkExtent2D extent{};

    std::vector<VkImage> images;  // Owned by the swapchain, not by us.
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkSemaphore rendered = VK_NULL_HANDLE;
    VkFence in_flight = VK_NULL_HANDLE;

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    ~Impl() {
        if (!device || device->device == VK_NULL_HANDLE) {
            return;
        }
        VkDevice handle = device->device;
        vkDeviceWaitIdle(handle);
        destroy_swapchain();
        if (in_flight != VK_NULL_HANDLE) { vkDestroyFence(handle, in_flight, nullptr); }
        if (rendered != VK_NULL_HANDLE) { vkDestroySemaphore(handle, rendered, nullptr); }
        if (acquired != VK_NULL_HANDLE) { vkDestroySemaphore(handle, acquired, nullptr); }
        if (commands != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(handle, device->command_pool, 1, &commands);
        }
        // The surface belongs to whoever created it -- Qt, in practice -- so it
        // is deliberately not destroyed here.
    }

    void destroy_swapchain() {
        if (swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device->device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }
        images.clear();
    }

    [[nodiscard]] Result<void> build(int width, int height) {
        VkDevice handle = device->device;
        VkPhysicalDevice physical = device->physical;

        VkSurfaceCapabilitiesKHR capabilities{};
        VkResult result =
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &capabilities);
        if (result != VK_SUCCESS) {
            return from_vulkan(result, "cannot query surface capabilities");
        }

        // A minimised window reports a zero extent; there is nothing to build.
        VkExtent2D wanted{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
        if (capabilities.currentExtent.width != 0xFFFFFFFFu) {
            wanted = capabilities.currentExtent;
        }
        wanted.width = std::clamp(wanted.width, capabilities.minImageExtent.width,
                                  capabilities.maxImageExtent.width);
        wanted.height = std::clamp(wanted.height, capabilities.minImageExtent.height,
                                   capabilities.maxImageExtent.height);
        if (wanted.width == 0 || wanted.height == 0) {
            return Error{Errc::invalid_argument, "the surface has no area to present to"};
        }

        std::uint32_t format_count = 0;
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &format_count, nullptr);
        if (result != VK_SUCCESS || format_count == 0) {
            return Error{Errc::unsupported_format, "the surface reports no usable formats"};
        }
        std::vector<VkSurfaceFormatKHR> formats(format_count);
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &format_count,
                                                      formats.data());
        if (result != VK_SUCCESS) {
            return from_vulkan(result, "cannot query surface formats");
        }

        // One more image than the minimum so the CPU can prepare the next frame
        // while the display holds one, without exceeding the driver's maximum.
        std::uint32_t image_count = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0) {
            image_count = std::min(image_count, capabilities.maxImageCount);
        }

        const VkSwapchainKHR previous = swapchain;

        VkSwapchainCreateInfoKHR create{};
        create.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        create.surface = surface;
        create.minImageCount = image_count;
        create.imageFormat = choose_format(formats).format;
        create.imageColorSpace = choose_format(formats).colorSpace;
        create.imageExtent = wanted;
        create.imageArrayLayers = 1;
        // TRANSFER_DST, not COLOR_ATTACHMENT: frames arrive as a copy from the
        // compositor's texture rather than being rendered here.
        create.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        create.preTransform = capabilities.currentTransform;
        create.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        // FIFO is the only mode guaranteed to exist, and it is the one that
        // paces to the display. Blocking here is the wait ADR 008 wants.
        create.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        create.clipped = VK_TRUE;
        create.oldSwapchain = previous;

        VkSwapchainKHR built = VK_NULL_HANDLE;
        result = vkCreateSwapchainKHR(handle, &create, nullptr, &built);

        // Retiring the old one is correct whether or not creation succeeded.
        if (previous != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(handle, previous, nullptr);
            swapchain = VK_NULL_HANDLE;
            images.clear();
        }
        if (result != VK_SUCCESS) {
            return from_vulkan(result, "cannot create a swapchain");
        }

        swapchain = built;
        format = choose_format(formats);
        extent = wanted;

        std::uint32_t count = 0;
        result = vkGetSwapchainImagesKHR(handle, swapchain, &count, nullptr);
        if (result != VK_SUCCESS) {
            return from_vulkan(result, "cannot query swapchain images");
        }
        images.resize(count);
        result = vkGetSwapchainImagesKHR(handle, swapchain, &count, images.data());
        if (result != VK_SUCCESS) {
            return from_vulkan(result, "cannot query swapchain images");
        }
        return ok();
    }
};

Swapchain::Swapchain(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Swapchain::Swapchain(Swapchain&&) noexcept = default;
Swapchain& Swapchain::operator=(Swapchain&&) noexcept = default;
Swapchain::~Swapchain() = default;

int Swapchain::width() const noexcept { return static_cast<int>(impl_->extent.width); }
int Swapchain::height() const noexcept { return static_cast<int>(impl_->extent.height); }
std::uint32_t Swapchain::image_count() const noexcept {
    return static_cast<std::uint32_t>(impl_->images.size());
}

Result<Swapchain> Swapchain::create(Device& device, SurfaceHandle surface, int width, int height) {
    if (surface == 0) {
        return Error{Errc::invalid_argument, "no surface was provided"};
    }
    if (width <= 0 || height <= 0) {
        return Error{Errc::invalid_argument, "surface dimensions must be positive"};
    }

    auto impl = std::make_unique<Impl>();
    impl->device = device.impl_;
    impl->surface = reinterpret_cast<VkSurfaceKHR>(surface);

    if (!impl->device->can_present) {
        // Without VK_KHR_swapchain the swapchain entry points are null, and
        // calling one crashes the process instead of returning an error.
        return Error{Errc::unsupported_format,
                     "this device does not support VK_KHR_swapchain, so it cannot present"};
    }

    VkBool32 supported = VK_FALSE;
    VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(
        impl->device->physical, impl->device->info.compute_queue_family, impl->surface,
        &supported);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot query surface support");
    }
    if (supported == VK_FALSE) {
        // Rare but real: some drivers expose a compute-capable family that
        // cannot present. Saying so beats failing later inside vkQueuePresent.
        return Error{Errc::unsupported_format,
                     "the device's queue family " +
                         std::to_string(impl->device->info.compute_queue_family) +
                         " cannot present to this surface"};
    }

    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = impl->device->command_pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    result = vkAllocateCommandBuffers(impl->device->device, &allocate, &impl->commands);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot allocate a present command buffer");
    }

    VkSemaphoreCreateInfo semaphore_create{};
    semaphore_create.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    result = vkCreateSemaphore(impl->device->device, &semaphore_create, nullptr, &impl->acquired);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot create the acquire semaphore");
    }
    result = vkCreateSemaphore(impl->device->device, &semaphore_create, nullptr, &impl->rendered);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot create the present semaphore");
    }

    VkFenceCreateInfo fence_create{};
    fence_create.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    result = vkCreateFence(impl->device->device, &fence_create, nullptr, &impl->in_flight);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot create the present fence");
    }

    if (Result<void> built = impl->build(width, height); !built) {
        return built.error();
    }
    return Swapchain{std::move(impl)};
}

Result<void> Swapchain::resize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return Error{Errc::invalid_argument, "surface dimensions must be positive"};
    }
    // Images may still be in use by a submitted frame.
    vkDeviceWaitIdle(impl_->device->device);
    return impl_->build(width, height);
}

Result<void> Swapchain::present(Texture& source) {
    VkDevice handle = impl_->device->device;

    std::uint32_t index = 0;
    // Blocks until the display is ready under FIFO. This is the pace.
    VkResult result = vkAcquireNextImageKHR(handle, impl_->swapchain, kTimeoutNanoseconds,
                                            impl_->acquired, VK_NULL_HANDLE, &index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return Error{Errc::version_mismatch, "the surface changed size; rebuild the swapchain"};
    }
    if (result == VK_TIMEOUT) {
        return Error{Errc::timeout, "the display did not provide an image within 5 seconds"};
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        return from_vulkan(result, "cannot acquire a swapchain image");
    }

    result = vkResetCommandBuffer(impl_->commands, 0);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot reset the present command buffer");
    }

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(impl_->commands, &begin);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot begin the present command buffer");
    }

    image_barrier(impl_->commands, source.impl_->image, VK_IMAGE_LAYOUT_GENERAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);

    // UNDEFINED as the old layout: the previous contents of a reused swapchain
    // image are not wanted, and demanding they be preserved would force the
    // driver into a needless copy.
    image_barrier(impl_->commands, impl_->images[index], VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    // Blit rather than copy: the window is rarely the same size as the frame,
    // and a vertical Reels frame in a wide window is the normal case.
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {source.width(), source.height(), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[1] = {static_cast<std::int32_t>(impl_->extent.width),
                          static_cast<std::int32_t>(impl_->extent.height), 1};
    vkCmdBlitImage(impl_->commands, source.impl_->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   impl_->images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   VK_FILTER_LINEAR);

    image_barrier(impl_->commands, impl_->images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_WRITE_BIT, 0,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    image_barrier(impl_->commands, source.impl_->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_READ_BIT,
                  VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    result = vkEndCommandBuffer(impl_->commands);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot end the present command buffer");
    }

    result = vkResetFences(handle, 1, &impl_->in_flight);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot reset the present fence");
    }

    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &impl_->acquired;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &impl_->commands;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &impl_->rendered;
    result = vkQueueSubmit(impl_->device->queue, 1, &submit, impl_->in_flight);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot submit the present copy");
    }

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &impl_->rendered;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &impl_->swapchain;
    present_info.pImageIndices = &index;
    result = vkQueuePresentKHR(impl_->device->queue, &present_info);

    // Wait before returning. Without more than one frame in flight the command
    // buffer and semaphores are reused next call, so they must be free -- and
    // with FIFO the acquire above already paced us, so this costs little.
    // Pipelining is worth doing once there is evidence it is needed.
    const VkResult waited = vkWaitForFences(handle, 1, &impl_->in_flight, VK_TRUE,
                                            kTimeoutNanoseconds);
    if (waited != VK_SUCCESS && waited != VK_TIMEOUT) {
        return from_vulkan(waited, "waiting for the presented frame failed");
    }

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        return Error{Errc::version_mismatch, "the surface changed size; rebuild the swapchain"};
    }
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot present");
    }
    return ok();
}

}  // namespace rf::gpu
