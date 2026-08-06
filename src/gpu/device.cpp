#include "rf/gpu/device.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <volk.h>

#include "device_impl.hpp"
#include "instance_impl.hpp"
#include "vulkan_util.hpp"
#include "rf/gpu/shaders/fill_pattern.hpp"

namespace rf::gpu {
namespace {

/// Everything created for a single headless dispatch, destroyed in reverse
/// order whatever path leaves the function.
///
/// Written as one owner rather than a unique_ptr per handle because the
/// destruction order matters and is not the declaration order a reader would
/// assume; keeping it in one place makes it reviewable.
struct PassResources {
    VkDevice device = VK_NULL_HANDLE;

    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory image_memory = VK_NULL_HANDLE;
    VkImageView image_view = VK_NULL_HANDLE;
    VkBuffer readback = VK_NULL_HANDLE;
    VkDeviceMemory readback_memory = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkShaderModule shader = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkCommandPool owning_pool = VK_NULL_HANDLE;

    PassResources() = default;
    PassResources(const PassResources&) = delete;
    PassResources& operator=(const PassResources&) = delete;

    ~PassResources() {
        if (device == VK_NULL_HANDLE) {
            return;
        }
        if (command_buffer != VK_NULL_HANDLE && owning_pool != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device, owning_pool, 1, &command_buffer);
        }
        if (fence != VK_NULL_HANDLE) { vkDestroyFence(device, fence, nullptr); }
        if (pipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, pipeline, nullptr); }
        if (pipeline_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        }
        if (shader != VK_NULL_HANDLE) { vkDestroyShaderModule(device, shader, nullptr); }
        if (descriptor_pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        }
        if (set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
        }
        if (readback != VK_NULL_HANDLE) { vkDestroyBuffer(device, readback, nullptr); }
        if (readback_memory != VK_NULL_HANDLE) { vkFreeMemory(device, readback_memory, nullptr); }
        if (image_view != VK_NULL_HANDLE) { vkDestroyImageView(device, image_view, nullptr); }
        if (image != VK_NULL_HANDLE) { vkDestroyImage(device, image, nullptr); }
        if (image_memory != VK_NULL_HANDLE) { vkFreeMemory(device, image_memory, nullptr); }
    }
};

struct PatternPush {
    std::uint32_t width;
    std::uint32_t height;
};

}  // namespace

Device::Device(std::shared_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Device::Device(Device&&) noexcept = default;
Device& Device::operator=(Device&&) noexcept = default;
Device::~Device() = default;

const DeviceInfo& Device::info() const noexcept {
    return impl_->info;
}

Result<Device> Device::create(const Instance& instance, std::size_t device_index) {
    Result<std::vector<DeviceInfo>> infos = instance.enumerate_devices();
    if (!infos) {
        return infos.error();
    }
    if (device_index >= infos.value().size()) {
        return Error{Errc::not_found, "device index " + std::to_string(device_index) +
                                          " is out of range (" +
                                          std::to_string(infos.value().size()) + " devices)"};
    }
    const DeviceInfo& info = infos.value()[device_index];
    if (!info.is_usable()) {
        return Error{Errc::unsupported_format, info.name + " " + info.unusable_reason()};
    }

    std::uint32_t count = 0;
    VkResult result = vkEnumeratePhysicalDevices(instance.impl_->instance, &count, nullptr);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot enumerate physical devices");
    }
    std::vector<VkPhysicalDevice> handles(count);
    result = vkEnumeratePhysicalDevices(instance.impl_->instance, &count, handles.data());
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot enumerate physical devices");
    }
    if (device_index >= handles.size()) {
        return Error{Errc::internal, "device list changed between enumerations"};
    }

    auto impl = std::make_shared<Impl>();
    impl->instance = instance.impl_;
    impl->physical = handles[device_index];
    impl->info = info;

    const float priority = 1.0F;
    VkDeviceQueueCreateInfo queue_create{};
    queue_create.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create.queueFamilyIndex = info.compute_queue_family;
    queue_create.queueCount = 1;
    queue_create.pQueuePriorities = &priority;

    // VK_KHR_swapchain is a DEVICE extension, and creating a swapchain without
    // it leaves vkCreateSwapchainKHR as a null pointer -- which crashes at the
    // call site with no diagnostic rather than failing. Enabled whenever the
    // device offers it, so a headless device simply does not get it and
    // Swapchain::create can say so.
    std::uint32_t extension_count = 0;
    vkEnumerateDeviceExtensionProperties(impl->physical, nullptr, &extension_count, nullptr);
    std::vector<VkExtensionProperties> device_extensions(extension_count);
    if (extension_count > 0) {
        vkEnumerateDeviceExtensionProperties(impl->physical, nullptr, &extension_count,
                                             device_extensions.data());
    }

    std::vector<const char*> enabled;
    const bool has_swapchain =
        std::any_of(device_extensions.begin(), device_extensions.end(),
                    [](const VkExtensionProperties& e) {
                        return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
                    });
    if (has_swapchain) {
        enabled.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
    impl->can_present = has_swapchain;

    VkDeviceCreateInfo device_create{};
    device_create.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create.queueCreateInfoCount = 1;
    device_create.pQueueCreateInfos = &queue_create;
    device_create.enabledExtensionCount = static_cast<std::uint32_t>(enabled.size());
    device_create.ppEnabledExtensionNames = enabled.empty() ? nullptr : enabled.data();

    result = vkCreateDevice(impl->physical, &device_create, nullptr, &impl->device);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot create a logical device for " + info.name);
    }

    // Device-level entry points bypass the loader's dispatch trampoline.
    volkLoadDevice(impl->device);

    vkGetDeviceQueue(impl->device, info.compute_queue_family, 0, &impl->queue);

    VkCommandPoolCreateInfo pool_create{};
    pool_create.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    // RESET_COMMAND_BUFFER lets the compositor re-record one buffer per frame
    // rather than allocating a new one, which is part of keeping the render
    // loop free of per-frame allocation.
    pool_create.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_create.queueFamilyIndex = info.compute_queue_family;
    result = vkCreateCommandPool(impl->device, &pool_create, nullptr, &impl->command_pool);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot create a command pool");
    }

    return Device{std::move(impl)};
}

Result<Device> Device::create_preferred(const Instance& instance) {
    Result<std::size_t> index = instance.preferred_device();
    if (!index) {
        return index.error();
    }
    return create(instance, index.value());
}

Result<ImageRgba8> Device::render_fill_pattern(int width, int height) {
    if (width <= 0 || height <= 0) {
        return Error{Errc::invalid_argument, "image dimensions must be positive"};
    }

    const auto image_width = static_cast<std::uint32_t>(width);
    const auto image_height = static_cast<std::uint32_t>(height);
    const VkDeviceSize byte_count =
        static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;

    PassResources pass;
    pass.device = impl_->device;
    pass.owning_pool = impl_->command_pool;

    // --- storage image --------------------------------------------------------
    VkImageCreateInfo image_create{};
    image_create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_create.imageType = VK_IMAGE_TYPE_2D;
    image_create.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_create.extent = {image_width, image_height, 1};
    image_create.mipLevels = 1;
    image_create.arrayLayers = 1;
    image_create.samples = VK_SAMPLE_COUNT_1_BIT;
    image_create.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_create.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult result = vkCreateImage(impl_->device, &image_create, nullptr, &pass.image);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot create the output image");
    }

    VkMemoryRequirements image_requirements{};
    vkGetImageMemoryRequirements(impl_->device, pass.image, &image_requirements);
    Result<std::uint32_t> image_memory_type = detail::find_memory_type(
        impl_->physical, image_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!image_memory_type) {
        return image_memory_type.error().with_context("output image");
    }

    VkMemoryAllocateInfo image_allocate{};
    image_allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    image_allocate.allocationSize = image_requirements.size;
    image_allocate.memoryTypeIndex = image_memory_type.value();
    result = vkAllocateMemory(impl_->device, &image_allocate, nullptr, &pass.image_memory);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot allocate image memory");
    }
    result = vkBindImageMemory(impl_->device, pass.image, pass.image_memory, 0);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot bind image memory");
    }

    VkImageViewCreateInfo view_create{};
    view_create.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_create.image = pass.image;
    view_create.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_create.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_create.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    result = vkCreateImageView(impl_->device, &view_create, nullptr, &pass.image_view);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot create the image view");
    }

    // --- readback buffer ------------------------------------------------------
    VkBufferCreateInfo buffer_create{};
    buffer_create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_create.size = byte_count;
    buffer_create.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    result = vkCreateBuffer(impl_->device, &buffer_create, nullptr, &pass.readback);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot create the readback buffer");
    }

    VkMemoryRequirements buffer_requirements{};
    vkGetBufferMemoryRequirements(impl_->device, pass.readback, &buffer_requirements);
    // HOST_COHERENT avoids an explicit invalidate before reading the mapping.
    Result<std::uint32_t> buffer_memory_type =
        detail::find_memory_type(impl_->physical, buffer_requirements.memoryTypeBits,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!buffer_memory_type) {
        return buffer_memory_type.error().with_context("readback buffer");
    }

    VkMemoryAllocateInfo buffer_allocate{};
    buffer_allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    buffer_allocate.allocationSize = buffer_requirements.size;
    buffer_allocate.memoryTypeIndex = buffer_memory_type.value();
    result = vkAllocateMemory(impl_->device, &buffer_allocate, nullptr, &pass.readback_memory);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot allocate readback memory");
    }
    result = vkBindBufferMemory(impl_->device, pass.readback, pass.readback_memory, 0);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot bind readback memory");
    }

    // --- descriptors ----------------------------------------------------------
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layout_create{};
    layout_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_create.bindingCount = 1;
    layout_create.pBindings = &binding;
    result = vkCreateDescriptorSetLayout(impl_->device, &layout_create, nullptr, &pass.set_layout);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot create the descriptor set layout");
    }

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    pool_size.descriptorCount = 1;

    VkDescriptorPoolCreateInfo pool_create{};
    pool_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_create.maxSets = 1;
    pool_create.poolSizeCount = 1;
    pool_create.pPoolSizes = &pool_size;
    result = vkCreateDescriptorPool(impl_->device, &pool_create, nullptr, &pass.descriptor_pool);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot create the descriptor pool");
    }

    VkDescriptorSetAllocateInfo set_allocate{};
    set_allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_allocate.descriptorPool = pass.descriptor_pool;
    set_allocate.descriptorSetCount = 1;
    set_allocate.pSetLayouts = &pass.set_layout;

    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    result = vkAllocateDescriptorSets(impl_->device, &set_allocate, &descriptor_set);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot allocate the descriptor set");
    }

    VkDescriptorImageInfo descriptor_image{};
    descriptor_image.imageView = pass.image_view;
    descriptor_image.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptor_set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.pImageInfo = &descriptor_image;
    vkUpdateDescriptorSets(impl_->device, 1, &write, 0, nullptr);

    // --- pipeline -------------------------------------------------------------
    VkShaderModuleCreateInfo shader_create{};
    shader_create.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_create.codeSize = shaders::fill_pattern_word_count * sizeof(std::uint32_t);
    shader_create.pCode = shaders::fill_pattern;
    result = vkCreateShaderModule(impl_->device, &shader_create, nullptr, &pass.shader);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot create the shader module");
    }

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(PatternPush);

    VkPipelineLayoutCreateInfo pipeline_layout_create{};
    pipeline_layout_create.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_create.setLayoutCount = 1;
    pipeline_layout_create.pSetLayouts = &pass.set_layout;
    pipeline_layout_create.pushConstantRangeCount = 1;
    pipeline_layout_create.pPushConstantRanges = &push_range;
    result = vkCreatePipelineLayout(impl_->device, &pipeline_layout_create, nullptr,
                                    &pass.pipeline_layout);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot create the pipeline layout");
    }

    VkComputePipelineCreateInfo pipeline_create{};
    pipeline_create.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_create.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline_create.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline_create.stage.module = pass.shader;
    pipeline_create.stage.pName = "main";
    pipeline_create.layout = pass.pipeline_layout;
    result = vkCreateComputePipelines(impl_->device, VK_NULL_HANDLE, 1, &pipeline_create, nullptr,
                                      &pass.pipeline);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot create the compute pipeline");
    }

    // --- record and submit ----------------------------------------------------
    VkCommandBufferAllocateInfo command_allocate{};
    command_allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_allocate.commandPool = impl_->command_pool;
    command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_allocate.commandBufferCount = 1;
    result = vkAllocateCommandBuffers(impl_->device, &command_allocate, &pass.command_buffer);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot allocate a command buffer");
    }

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(pass.command_buffer, &begin);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot begin the command buffer");
    }

    // UNDEFINED -> GENERAL. The contents are not preserved, which is correct:
    // the shader writes every pixel it is responsible for.
    VkImageMemoryBarrier to_general{};
    to_general.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_general.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_general.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    to_general.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_general.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_general.image = pass.image;
    to_general.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    to_general.srcAccessMask = 0;
    to_general.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(pass.command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &to_general);

    vkCmdBindPipeline(pass.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pass.pipeline);
    vkCmdBindDescriptorSets(pass.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pass.pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);

    const PatternPush push{image_width, image_height};
    vkCmdPushConstants(pass.command_buffer, pass.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(push), &push);

    // Rounded up: the shader bounds-checks, so a partial edge workgroup is safe
    // and the alternative would leave the right and bottom edges unwritten.
    constexpr std::uint32_t kGroupSize = 8;
    vkCmdDispatch(pass.command_buffer, (image_width + kGroupSize - 1) / kGroupSize,
                  (image_height + kGroupSize - 1) / kGroupSize, 1);

    VkImageMemoryBarrier to_transfer{};
    to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_transfer.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.image = pass.image;
    to_transfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    to_transfer.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(pass.command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &to_transfer);

    VkBufferImageCopy copy{};
    // Zero bufferRowLength means tightly packed to the image width, which is
    // what ImageRgba8 promises.
    copy.bufferRowLength = 0;
    copy.bufferImageHeight = 0;
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {image_width, image_height, 1};
    vkCmdCopyImageToBuffer(pass.command_buffer, pass.image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pass.readback, 1, &copy);

    result = vkEndCommandBuffer(pass.command_buffer);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot end the command buffer");
    }

    VkFenceCreateInfo fence_create{};
    fence_create.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    result = vkCreateFence(impl_->device, &fence_create, nullptr, &pass.fence);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot create a fence");
    }

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &pass.command_buffer;
    result = vkQueueSubmit(impl_->queue, 1, &submit, pass.fence);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot submit work");
    }

    // A generous but finite timeout. Waiting forever on a wedged driver would
    // hang a test run with no diagnosis; ten seconds is far beyond any honest
    // completion time for this dispatch, including on a software device.
    constexpr std::uint64_t kTimeoutNanoseconds = 10ULL * 1000 * 1000 * 1000;
    result = vkWaitForFences(impl_->device, 1, &pass.fence, VK_TRUE, kTimeoutNanoseconds);
    if (result == VK_TIMEOUT) {
        return Error{Errc::timeout, "the GPU did not finish within 10 seconds"};
    }
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "waiting for the GPU failed");
    }

    // --- readback -------------------------------------------------------------
    void* mapped = nullptr;
    result = vkMapMemory(impl_->device, pass.readback_memory, 0, byte_count, 0, &mapped);
    if (result != VK_SUCCESS) {
        return detail::from_vulkan(result, "cannot map the readback buffer");
    }

    ImageRgba8 image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<std::size_t>(byte_count));
    std::memcpy(image.pixels.data(), mapped, static_cast<std::size_t>(byte_count));
    vkUnmapMemory(impl_->device, pass.readback_memory);

    return image;
}

}  // namespace rf::gpu
