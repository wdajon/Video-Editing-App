#include "rf/gpu/texture.hpp"

#include <cstring>
#include <string>
#include <utility>

#include <volk.h>

#include "texture_impl.hpp"
#include "vulkan_util.hpp"

namespace rf::gpu {
namespace {

using detail::create_host_buffer;
using detail::create_storage_image;
using detail::from_vulkan;
using detail::image_barrier;

constexpr std::uint64_t kTimeoutNanoseconds = 30ULL * 1000 * 1000 * 1000;

}  // namespace

Result<void> Texture::Impl::submit_and_wait(std::string_view what) {
    VkResult result = vkEndCommandBuffer(commands);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, std::string{"cannot end "} + std::string{what});
    }
    result = vkResetFences(device->device, 1, &fence);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot reset the texture fence");
    }

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commands;
    result = vkQueueSubmit(device->queue, 1, &submit, fence);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, std::string{"cannot submit "} + std::string{what});
    }

    result = vkWaitForFences(device->device, 1, &fence, VK_TRUE, kTimeoutNanoseconds);
    if (result == VK_TIMEOUT) {
        return Error{Errc::timeout, std::string{what} + " did not finish within 30 seconds"};
    }
    if (result != VK_SUCCESS) {
        return from_vulkan(result, std::string{"waiting for "} + std::string{what} + " failed");
    }
    return ok();
}

Result<void> Texture::Impl::begin_commands() {
    VkResult result = vkResetCommandBuffer(commands, 0);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot reset the texture command buffer");
    }
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(commands, &begin);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot begin the texture command buffer");
    }
    return ok();
}

Texture::Texture(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Texture::Texture(Texture&&) noexcept = default;
Texture& Texture::operator=(Texture&&) noexcept = default;
Texture::~Texture() = default;

int Texture::width() const noexcept { return impl_->width; }
int Texture::height() const noexcept { return impl_->height; }

Result<Texture> Texture::create(Device& device, int width, int height) {
    if (width <= 0 || height <= 0) {
        return Error{Errc::invalid_argument, "texture dimensions must be positive"};
    }

    auto impl = std::make_unique<Impl>();
    impl->device = device.impl_;
    impl->width = width;
    impl->height = height;

    VkDevice handle = impl->device->device;
    VkPhysicalDevice physical = impl->device->physical;
    const auto image_width = static_cast<std::uint32_t>(width);
    const auto image_height = static_cast<std::uint32_t>(height);
    const VkDeviceSize bytes =
        static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;

    // STORAGE so a compute shader can read or write it, and both transfer
    // directions so it can be uploaded to and read back from.
    if (Result<void> made = create_storage_image(
            handle, physical, image_width, image_height,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            impl->image, impl->memory, impl->view);
        !made) {
        return made.error().with_context("texture");
    }

    if (Result<void> made = create_host_buffer(
            handle, physical, bytes,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, impl->staging,
            impl->staging_memory);
        !made) {
        return made.error().with_context("texture staging");
    }

    VkResult result =
        vkMapMemory(handle, impl->staging_memory, 0, VK_WHOLE_SIZE, 0, &impl->staging_mapping);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot map texture staging memory");
    }

    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = impl->device->command_pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    result = vkAllocateCommandBuffers(handle, &allocate, &impl->commands);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot allocate a texture command buffer");
    }

    VkFenceCreateInfo fence_create{};
    fence_create.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    result = vkCreateFence(handle, &fence_create, nullptr, &impl->fence);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot create a texture fence");
    }

    return Texture{std::move(impl)};
}

Result<Texture> Texture::create_from(Device& device, const ImageRgba8& pixels) {
    if (!pixels.is_valid()) {
        return Error{Errc::invalid_argument, "source image is not a valid RGBA8 buffer"};
    }
    Result<Texture> texture = create(device, pixels.width, pixels.height);
    if (!texture) {
        return texture.error();
    }
    if (Result<void> uploaded = texture.value().upload(pixels); !uploaded) {
        return uploaded.error();
    }
    return texture;
}

Result<void> Texture::upload(const ImageRgba8& pixels) {
    if (pixels.width != impl_->width || pixels.height != impl_->height) {
        return Error{Errc::invalid_argument,
                     "upload is " + std::to_string(pixels.width) + "x" +
                         std::to_string(pixels.height) + ", texture is " +
                         std::to_string(impl_->width) + "x" + std::to_string(impl_->height)};
    }
    if (!pixels.is_valid()) {
        return Error{Errc::invalid_argument, "source image has a pixel buffer of the wrong size"};
    }

    std::memcpy(impl_->staging_mapping, pixels.pixels.data(), pixels.pixels.size());

    if (Result<void> begun = impl_->begin_commands(); !begun) {
        return begun.error();
    }

    // First transition must come from UNDEFINED; later ones from GENERAL, which
    // is the layout every operation leaves the image in.
    const VkImageLayout from =
        impl_->initialised ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
    image_barrier(impl_->commands, impl_->image, from, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  impl_->initialised ? VK_ACCESS_SHADER_READ_BIT : 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                  impl_->initialised ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                                     : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {static_cast<std::uint32_t>(impl_->width),
                        static_cast<std::uint32_t>(impl_->height), 1};
    vkCmdCopyBufferToImage(impl_->commands, impl_->staging, impl_->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    image_barrier(impl_->commands, impl_->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    if (Result<void> done = impl_->submit_and_wait("the texture upload"); !done) {
        return done.error();
    }
    impl_->initialised = true;
    return ok();
}

Result<ImageRgba8> Texture::read_back() const {
    if (!impl_->initialised) {
        return Error{Errc::invalid_argument,
                     "texture has never been written, so its contents are undefined"};
    }

    if (Result<void> begun = impl_->begin_commands(); !begun) {
        return begun.error();
    }

    image_barrier(impl_->commands, impl_->image, VK_IMAGE_LAYOUT_GENERAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {static_cast<std::uint32_t>(impl_->width),
                        static_cast<std::uint32_t>(impl_->height), 1};
    vkCmdCopyImageToBuffer(impl_->commands, impl_->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           impl_->staging, 1, &copy);

    image_barrier(impl_->commands, impl_->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_READ_BIT,
                  VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    if (Result<void> done = impl_->submit_and_wait("the texture readback"); !done) {
        return done.error();
    }

    ImageRgba8 image;
    image.width = impl_->width;
    image.height = impl_->height;
    image.pixels.resize(image.expected_size());
    std::memcpy(image.pixels.data(), impl_->staging_mapping, image.pixels.size());
    return image;
}

}  // namespace rf::gpu
