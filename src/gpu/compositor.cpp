#include "rf/gpu/compositor.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <volk.h>

#include "device_impl.hpp"
#include "rf/gpu/shaders/composite.hpp"
#include "vulkan_util.hpp"

namespace rf::gpu {
namespace {

struct CompositePush {
    std::uint32_t width;
    std::uint32_t height;
    float opacity;
};

constexpr std::uint32_t kGroupSize = 8;

/// Per-composite resources, destroyed in reverse order whatever path leaves the
/// function. The pipeline itself is not here -- it belongs to the Compositor and
/// is built once.
struct FrameResources {
    VkDevice device = VK_NULL_HANDLE;
    VkCommandPool owning_pool = VK_NULL_HANDLE;

    VkImage destination = VK_NULL_HANDLE;
    VkDeviceMemory destination_memory = VK_NULL_HANDLE;
    VkImageView destination_view = VK_NULL_HANDLE;
    VkImage source = VK_NULL_HANDLE;
    VkDeviceMemory source_memory = VK_NULL_HANDLE;
    VkImageView source_view = VK_NULL_HANDLE;
    VkBuffer upload = VK_NULL_HANDLE;
    VkDeviceMemory upload_memory = VK_NULL_HANDLE;
    VkBuffer readback = VK_NULL_HANDLE;
    VkDeviceMemory readback_memory = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;

    FrameResources() = default;
    FrameResources(const FrameResources&) = delete;
    FrameResources& operator=(const FrameResources&) = delete;

    ~FrameResources() {
        if (device == VK_NULL_HANDLE) {
            return;
        }
        if (command_buffer != VK_NULL_HANDLE && owning_pool != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device, owning_pool, 1, &command_buffer);
        }
        if (fence != VK_NULL_HANDLE) { vkDestroyFence(device, fence, nullptr); }
        if (descriptor_pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        }
        if (readback != VK_NULL_HANDLE) { vkDestroyBuffer(device, readback, nullptr); }
        if (readback_memory != VK_NULL_HANDLE) { vkFreeMemory(device, readback_memory, nullptr); }
        if (upload != VK_NULL_HANDLE) { vkDestroyBuffer(device, upload, nullptr); }
        if (upload_memory != VK_NULL_HANDLE) { vkFreeMemory(device, upload_memory, nullptr); }
        if (source_view != VK_NULL_HANDLE) { vkDestroyImageView(device, source_view, nullptr); }
        if (source != VK_NULL_HANDLE) { vkDestroyImage(device, source, nullptr); }
        if (source_memory != VK_NULL_HANDLE) { vkFreeMemory(device, source_memory, nullptr); }
        if (destination_view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, destination_view, nullptr);
        }
        if (destination != VK_NULL_HANDLE) { vkDestroyImage(device, destination, nullptr); }
        if (destination_memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, destination_memory, nullptr);
        }
    }
};

[[nodiscard]] Result<void> create_storage_image(VkDevice device, VkPhysicalDevice physical,
                                                std::uint32_t width, std::uint32_t height,
                                                VkImageUsageFlags usage, VkImage& image,
                                                VkDeviceMemory& memory, VkImageView& view) {
    VkImageCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create.imageType = VK_IMAGE_TYPE_2D;
    create.format = VK_FORMAT_R8G8B8A8_UNORM;
    create.extent = {width, height, 1};
    create.mipLevels = 1;
    create.arrayLayers = 1;
    create.samples = VK_SAMPLE_COUNT_1_BIT;
    create.tiling = VK_IMAGE_TILING_OPTIMAL;
    create.usage = usage;
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult result = vkCreateImage(device, &create, nullptr, &image);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot create a composite image");
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, image, &requirements);
    Result<std::uint32_t> type = detail::find_memory_type(physical, requirements.memoryTypeBits,
                                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!type) {
        return type.error().with_context("composite image");
    }

    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = type.value();
    result = vkAllocateMemory(device, &allocate, nullptr, &memory);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot allocate composite image memory");
    }
    result = vkBindImageMemory(device, image, memory, 0);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot bind composite image memory");
    }

    VkImageViewCreateInfo view_create{};
    view_create.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_create.image = image;
    view_create.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_create.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_create.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    result = vkCreateImageView(device, &view_create, nullptr, &view);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot create a composite image view");
    }
    return ok();
}

[[nodiscard]] Result<void> create_host_buffer(VkDevice device, VkPhysicalDevice physical,
                                              VkDeviceSize size, VkBufferUsageFlags usage,
                                              VkBuffer& buffer, VkDeviceMemory& memory) {
    VkBufferCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create.size = size;
    create.usage = usage;
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(device, &create, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot create a host buffer");
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    // HOST_COHERENT avoids explicit flush and invalidate around the mapping.
    Result<std::uint32_t> type = detail::find_memory_type(
        physical, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!type) {
        return type.error().with_context("host buffer");
    }

    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = type.value();
    result = vkAllocateMemory(device, &allocate, nullptr, &memory);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot allocate host buffer memory");
    }
    result = vkBindBufferMemory(device, buffer, memory, 0);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot bind host buffer memory");
    }
    return ok();
}

void image_barrier(VkCommandBuffer commands, VkImage image, VkImageLayout from, VkImageLayout to,
                   VkAccessFlags source_access, VkAccessFlags destination_access,
                   VkPipelineStageFlags source_stage, VkPipelineStageFlags destination_stage) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = source_access;
    barrier.dstAccessMask = destination_access;
    vkCmdPipelineBarrier(commands, source_stage, destination_stage, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);
}

[[nodiscard]] std::uint8_t to_byte(float value) {
    const float clamped = std::clamp(value, 0.0F, 1.0F);
    return static_cast<std::uint8_t>(std::lround(clamped * 255.0F));
}

}  // namespace

class Compositor::Impl {
public:
    /// Keeps the device alive for at least as long as this compositor.
    /// Declared first so it is destroyed last.
    std::shared_ptr<Device::Impl> device;

    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkShaderModule shader = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    ~Impl() {
        if (!device || device->device == VK_NULL_HANDLE) {
            return;
        }
        VkDevice handle = device->device;
        vkDeviceWaitIdle(handle);
        if (pipeline != VK_NULL_HANDLE) { vkDestroyPipeline(handle, pipeline, nullptr); }
        if (pipeline_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(handle, pipeline_layout, nullptr);
        }
        if (shader != VK_NULL_HANDLE) { vkDestroyShaderModule(handle, shader, nullptr); }
        if (set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(handle, set_layout, nullptr);
        }
    }
};

Compositor::Compositor(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Compositor::Compositor(Compositor&&) noexcept = default;
Compositor& Compositor::operator=(Compositor&&) noexcept = default;
Compositor::~Compositor() = default;

Result<Compositor> Compositor::create(Device& device) {
    auto impl = std::make_unique<Impl>();
    impl->device = device.impl_;
    VkDevice handle = impl->device->device;

    const VkDescriptorSetLayoutBinding bindings[2] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };

    VkDescriptorSetLayoutCreateInfo layout_create{};
    layout_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_create.bindingCount = 2;
    layout_create.pBindings = bindings;
    VkResult result =
        vkCreateDescriptorSetLayout(handle, &layout_create, nullptr, &impl->set_layout);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot create the composite descriptor layout");
    }

    VkShaderModuleCreateInfo shader_create{};
    shader_create.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_create.codeSize = shaders::composite_word_count * sizeof(std::uint32_t);
    shader_create.pCode = shaders::composite;
    result = vkCreateShaderModule(handle, &shader_create, nullptr, &impl->shader);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot create the composite shader module");
    }

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push_range.size = sizeof(CompositePush);

    VkPipelineLayoutCreateInfo pipeline_layout_create{};
    pipeline_layout_create.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_create.setLayoutCount = 1;
    pipeline_layout_create.pSetLayouts = &impl->set_layout;
    pipeline_layout_create.pushConstantRangeCount = 1;
    pipeline_layout_create.pPushConstantRanges = &push_range;
    result = vkCreatePipelineLayout(handle, &pipeline_layout_create, nullptr,
                                    &impl->pipeline_layout);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot create the composite pipeline layout");
    }

    VkComputePipelineCreateInfo pipeline_create{};
    pipeline_create.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_create.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline_create.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline_create.stage.module = impl->shader;
    pipeline_create.stage.pName = "main";
    pipeline_create.layout = impl->pipeline_layout;
    result = vkCreateComputePipelines(handle, VK_NULL_HANDLE, 1, &pipeline_create, nullptr,
                                      &impl->pipeline);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot create the composite pipeline");
    }

    return Compositor{std::move(impl)};
}

Result<ImageRgba8> Compositor::composite(const std::vector<Layer>& layers, int width, int height) {
    if (width <= 0 || height <= 0) {
        return Error{Errc::invalid_argument, "composite dimensions must be positive"};
    }

    std::vector<const Layer*> drawn;
    drawn.reserve(layers.size());
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const Layer& layer = layers[i];
        if (!layer.enabled) {
            continue;
        }
        if (layer.source.width != width || layer.source.height != height) {
            return Error{Errc::invalid_argument,
                         "layer " + std::to_string(i) + " is " +
                             std::to_string(layer.source.width) + "x" +
                             std::to_string(layer.source.height) + ", expected " +
                             std::to_string(width) + "x" + std::to_string(height)};
        }
        if (!layer.source.is_valid()) {
            return Error{Errc::invalid_argument,
                         "layer " + std::to_string(i) + " has a pixel buffer of the wrong size"};
        }
        drawn.push_back(&layer);
    }

    const auto image_width = static_cast<std::uint32_t>(width);
    const auto image_height = static_cast<std::uint32_t>(height);
    const VkDeviceSize frame_bytes =
        static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;

    VkDevice handle = impl_->device->device;
    VkPhysicalDevice physical = impl_->device->physical;

    FrameResources frame;
    frame.device = handle;
    frame.owning_pool = impl_->device->command_pool;

    if (Result<void> made = create_storage_image(
            handle, physical, image_width, image_height,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            frame.destination, frame.destination_memory, frame.destination_view);
        !made) {
        return made.error().with_context("destination");
    }
    if (Result<void> made = create_storage_image(
            handle, physical, image_width, image_height,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, frame.source,
            frame.source_memory, frame.source_view);
        !made) {
        return made.error().with_context("source");
    }

    // One upload buffer holding every layer back to back, so the whole composite
    // is a single submission rather than one per layer.
    const VkDeviceSize upload_bytes =
        frame_bytes * static_cast<VkDeviceSize>(std::max<std::size_t>(drawn.size(), 1));
    if (Result<void> made = create_host_buffer(handle, physical, upload_bytes,
                                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT, frame.upload,
                                               frame.upload_memory);
        !made) {
        return made.error().with_context("upload buffer");
    }
    if (Result<void> made = create_host_buffer(handle, physical, frame_bytes,
                                               VK_BUFFER_USAGE_TRANSFER_DST_BIT, frame.readback,
                                               frame.readback_memory);
        !made) {
        return made.error().with_context("readback buffer");
    }

    if (!drawn.empty()) {
        void* mapped = nullptr;
        VkResult result = vkMapMemory(handle, frame.upload_memory, 0, upload_bytes, 0, &mapped);
        if (result != VK_SUCCESS) {
            return detail::from_vulkan(result, "cannot map the upload buffer");
        }
        auto* bytes = static_cast<std::uint8_t*>(mapped);
        for (std::size_t i = 0; i < drawn.size(); ++i) {
            std::memcpy(bytes + i * static_cast<std::size_t>(frame_bytes),
                        drawn[i]->source.pixels.data(), static_cast<std::size_t>(frame_bytes));
        }
        vkUnmapMemory(handle, frame.upload_memory);
    }

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    pool_size.descriptorCount = 2;

    VkDescriptorPoolCreateInfo pool_create{};
    pool_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_create.maxSets = 1;
    pool_create.poolSizeCount = 1;
    pool_create.pPoolSizes = &pool_size;
    VkResult result = vkCreateDescriptorPool(handle, &pool_create, nullptr, &frame.descriptor_pool);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot create the composite descriptor pool");
    }

    VkDescriptorSetAllocateInfo set_allocate{};
    set_allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_allocate.descriptorPool = frame.descriptor_pool;
    set_allocate.descriptorSetCount = 1;
    set_allocate.pSetLayouts = &impl_->set_layout;

    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    result = vkAllocateDescriptorSets(handle, &set_allocate, &descriptor_set);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot allocate the composite descriptor set");
    }

    // The two images never change across layers -- only the contents of the
    // source do -- so the descriptor set is written once.
    VkDescriptorImageInfo destination_info{};
    destination_info.imageView = frame.destination_view;
    destination_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkDescriptorImageInfo source_info{};
    source_info.imageView = frame.source_view;
    source_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptor_set;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].pImageInfo = &destination_info;
    writes[1] = writes[0];
    writes[1].dstBinding = 1;
    writes[1].pImageInfo = &source_info;
    vkUpdateDescriptorSets(handle, 2, writes, 0, nullptr);

    VkCommandBufferAllocateInfo command_allocate{};
    command_allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_allocate.commandPool = impl_->device->command_pool;
    command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_allocate.commandBufferCount = 1;
    result = vkAllocateCommandBuffers(handle, &command_allocate, &frame.command_buffer);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot allocate a command buffer");
    }

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(frame.command_buffer, &begin);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot begin the command buffer");
    }

    // The backdrop is opaque black. A timeline with nothing on it shows black,
    // not uninitialised memory, and clearing explicitly is what guarantees that
    // when every layer is disabled.
    image_barrier(frame.command_buffer, frame.destination, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkClearColorValue black{};
    black.float32[3] = 1.0F;
    VkImageSubresourceRange whole{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(frame.command_buffer, frame.destination,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &whole);

    image_barrier(frame.command_buffer, frame.destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    image_barrier(frame.command_buffer, frame.source, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    vkCmdBindPipeline(frame.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, impl_->pipeline);
    vkCmdBindDescriptorSets(frame.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            impl_->pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);

    for (std::size_t i = 0; i < drawn.size(); ++i) {
        VkBufferImageCopy copy{};
        copy.bufferOffset = static_cast<VkDeviceSize>(i) * frame_bytes;
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {image_width, image_height, 1};
        vkCmdCopyBufferToImage(frame.command_buffer, frame.upload, frame.source,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        image_barrier(frame.command_buffer, frame.source, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        const CompositePush push{image_width, image_height, drawn[i]->opacity};
        vkCmdPushConstants(frame.command_buffer, impl_->pipeline_layout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(frame.command_buffer, (image_width + kGroupSize - 1) / kGroupSize,
                      (image_height + kGroupSize - 1) / kGroupSize, 1);

        if (i + 1 < drawn.size()) {
            // The next layer overwrites the source and reads the destination
            // this dispatch just wrote, so both need ordering before it runs.
            image_barrier(frame.command_buffer, frame.source, VK_IMAGE_LAYOUT_GENERAL,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
                          VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT);
            image_barrier(frame.command_buffer, frame.destination, VK_IMAGE_LAYOUT_GENERAL,
                          VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT,
                          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }
    }

    image_barrier(frame.command_buffer, frame.destination, VK_IMAGE_LAYOUT_GENERAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy readback_copy{};
    readback_copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    readback_copy.imageExtent = {image_width, image_height, 1};
    vkCmdCopyImageToBuffer(frame.command_buffer, frame.destination,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, frame.readback, 1,
                           &readback_copy);

    result = vkEndCommandBuffer(frame.command_buffer);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot end the command buffer");
    }

    VkFenceCreateInfo fence_create{};
    fence_create.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    result = vkCreateFence(handle, &fence_create, nullptr, &frame.fence);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot create a fence");
    }

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &frame.command_buffer;
    result = vkQueueSubmit(impl_->device->queue, 1, &submit, frame.fence);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot submit the composite");
    }

    constexpr std::uint64_t kTimeoutNanoseconds = 30ULL * 1000 * 1000 * 1000;
    result = vkWaitForFences(handle, 1, &frame.fence, VK_TRUE, kTimeoutNanoseconds);
    if (result == VK_TIMEOUT) {
        return Error{Errc::timeout, "the GPU did not finish the composite within 30 seconds"};
    }
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "waiting for the composite failed");
    }

    void* mapped = nullptr;
    result = vkMapMemory(handle, frame.readback_memory, 0, frame_bytes, 0, &mapped);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot map the readback buffer");
    }

    ImageRgba8 image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<std::size_t>(frame_bytes));
    std::memcpy(image.pixels.data(), mapped, static_cast<std::size_t>(frame_bytes));
    vkUnmapMemory(handle, frame.readback_memory);

    return image;
}

ImageRgba8 composite_reference(const std::vector<Layer>& layers, int width, int height) {
    ImageRgba8 image;
    if (width <= 0 || height <= 0) {
        return image;
    }
    image.width = width;
    image.height = height;
    image.pixels.assign(image.expected_size(), 0);

    // Opaque black backdrop, matching vkCmdClearColorImage above.
    for (std::size_t i = 3; i < image.pixels.size(); i += 4) {
        image.pixels[i] = 255u;
    }

    for (const Layer& layer : layers) {
        if (!layer.enabled || layer.source.width != width || layer.source.height != height ||
            !layer.source.is_valid()) {
            continue;
        }
        for (std::size_t pixel = 0; pixel < image.pixels.size(); pixel += 4) {
            // Mirrors shaders/composite.comp: unorm loads are value/255, the
            // blend is a lerp against an opaque backdrop, and the store rounds.
            const float coverage =
                (static_cast<float>(layer.source.pixels[pixel + 3]) / 255.0F) * layer.opacity;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const float above = static_cast<float>(layer.source.pixels[pixel + channel]) / 255.0F;
                const float below = static_cast<float>(image.pixels[pixel + channel]) / 255.0F;
                image.pixels[pixel + channel] = to_byte(above * coverage + below * (1.0F - coverage));
            }
            image.pixels[pixel + 3] = 255u;
        }
    }
    return image;
}

}  // namespace rf::gpu
