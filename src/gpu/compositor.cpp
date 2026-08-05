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
#include "texture_impl.hpp"
#include "vulkan_util.hpp"

namespace rf::gpu {
namespace {

using detail::from_vulkan;
using detail::image_barrier;

struct CompositePush {
    std::uint32_t width;
    std::uint32_t height;
    float opacity;
};

constexpr std::uint32_t kGroupSize = 8;
constexpr std::uint64_t kTimeoutNanoseconds = 30ULL * 1000 * 1000 * 1000;

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

    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    /// One descriptor set per layer: each dispatch binds a different source
    /// image, and a set that a submitted command buffer still references cannot
    /// be rewritten. Grown on demand, never shrunk -- a timeline's layer count
    /// is stable in practice.
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> sets;

    /// Scratch textures backing the CPU-pixels-in, CPU-pixels-out path. Cached
    /// so repeated calls at one size do not reallocate.
    std::vector<Texture> staged_layers;
    std::unique_ptr<Texture> staged_target;
    int staged_width = 0;
    int staged_height = 0;

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    ~Impl() {
        if (!device || device->device == VK_NULL_HANDLE) {
            return;
        }
        VkDevice handle = device->device;
        vkDeviceWaitIdle(handle);

        // Textures own device resources and must go before the pipeline objects
        // and, ultimately, before the device itself.
        staged_layers.clear();
        staged_target.reset();

        if (fence != VK_NULL_HANDLE) { vkDestroyFence(handle, fence, nullptr); }
        if (commands != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(handle, device->command_pool, 1, &commands);
        }
        if (descriptor_pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(handle, descriptor_pool, nullptr);
        }
        if (pipeline != VK_NULL_HANDLE) { vkDestroyPipeline(handle, pipeline, nullptr); }
        if (pipeline_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(handle, pipeline_layout, nullptr);
        }
        if (shader != VK_NULL_HANDLE) { vkDestroyShaderModule(handle, shader, nullptr); }
        if (set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(handle, set_layout, nullptr);
        }
    }

    /// Grows the descriptor pool so there is one set per layer.
    [[nodiscard]] Result<void> ensure_sets(std::size_t count) {
        if (sets.size() >= count && descriptor_pool != VK_NULL_HANDLE) {
            return ok();
        }
        VkDevice handle = device->device;
        vkDeviceWaitIdle(handle);

        if (descriptor_pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(handle, descriptor_pool, nullptr);
            descriptor_pool = VK_NULL_HANDLE;
            sets.clear();
        }

        const auto capacity = static_cast<std::uint32_t>(std::max<std::size_t>(count, 1));

        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        pool_size.descriptorCount = capacity * 2;

        VkDescriptorPoolCreateInfo pool_create{};
        pool_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_create.maxSets = capacity;
        pool_create.poolSizeCount = 1;
        pool_create.pPoolSizes = &pool_size;
        VkResult result = vkCreateDescriptorPool(handle, &pool_create, nullptr, &descriptor_pool);
        if (result != VK_SUCCESS) {
            return from_vulkan(result, "cannot create the composite descriptor pool");
        }

        const std::vector<VkDescriptorSetLayout> layouts(capacity, set_layout);
        VkDescriptorSetAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate.descriptorPool = descriptor_pool;
        allocate.descriptorSetCount = capacity;
        allocate.pSetLayouts = layouts.data();

        sets.resize(capacity);
        result = vkAllocateDescriptorSets(handle, &allocate, sets.data());
        if (result != VK_SUCCESS) {
            sets.clear();
            return from_vulkan(result, "cannot allocate composite descriptor sets");
        }
        return ok();
    }

    /// Rebuilds the scratch textures used by the CPU-pixels path.
    [[nodiscard]] Result<void> ensure_staging(Device& owner, int width, int height,
                                              std::size_t count) {
        if (staged_width == width && staged_height == height && staged_layers.size() >= count &&
            staged_target) {
            return ok();
        }

        staged_layers.clear();
        staged_target.reset();

        Result<Texture> target = Texture::create(owner, width, height);
        if (!target) {
            return target.error().with_context("composite target");
        }
        staged_target = std::make_unique<Texture>(std::move(target).value());

        staged_layers.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            Result<Texture> layer = Texture::create(owner, width, height);
            if (!layer) {
                return layer.error().with_context("composite layer " + std::to_string(i));
            }
            staged_layers.push_back(std::move(layer).value());
        }

        staged_width = width;
        staged_height = height;
        return ok();
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
        return from_vulkan(result, "cannot create the composite descriptor layout");
    }

    VkShaderModuleCreateInfo shader_create{};
    shader_create.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_create.codeSize = shaders::composite_word_count * sizeof(std::uint32_t);
    shader_create.pCode = shaders::composite;
    result = vkCreateShaderModule(handle, &shader_create, nullptr, &impl->shader);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot create the composite shader module");
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
    result =
        vkCreatePipelineLayout(handle, &pipeline_layout_create, nullptr, &impl->pipeline_layout);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot create the composite pipeline layout");
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
        return from_vulkan(result, "cannot create the composite pipeline");
    }

    VkCommandBufferAllocateInfo command_allocate{};
    command_allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_allocate.commandPool = impl->device->command_pool;
    command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_allocate.commandBufferCount = 1;
    result = vkAllocateCommandBuffers(handle, &command_allocate, &impl->commands);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot allocate a composite command buffer");
    }

    VkFenceCreateInfo fence_create{};
    fence_create.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    result = vkCreateFence(handle, &fence_create, nullptr, &impl->fence);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot create a composite fence");
    }

    return Compositor{std::move(impl)};
}

Result<void> Compositor::composite_into(Texture& target, const std::vector<GpuLayer>& layers) {
    std::vector<const GpuLayer*> drawn;
    drawn.reserve(layers.size());
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const GpuLayer& layer = layers[i];
        if (!layer.enabled) {
            continue;
        }
        if (layer.source == nullptr) {
            return Error{Errc::invalid_argument,
                         "layer " + std::to_string(i) + " has no source texture"};
        }
        if (layer.source->width() != target.width() ||
            layer.source->height() != target.height()) {
            return Error{Errc::invalid_argument,
                         "layer " + std::to_string(i) + " is " +
                             std::to_string(layer.source->width()) + "x" +
                             std::to_string(layer.source->height()) + ", target is " +
                             std::to_string(target.width()) + "x" +
                             std::to_string(target.height())};
        }
        drawn.push_back(&layer);
    }

    if (Result<void> ready = impl_->ensure_sets(drawn.size()); !ready) {
        return ready.error();
    }

    VkDevice handle = impl_->device->device;
    const auto image_width = static_cast<std::uint32_t>(target.width());
    const auto image_height = static_cast<std::uint32_t>(target.height());

    // Written before recording: a descriptor set referenced by a submitted
    // command buffer must not be rewritten, which is why each layer gets its
    // own set rather than one being updated between dispatches.
    for (std::size_t i = 0; i < drawn.size(); ++i) {
        VkDescriptorImageInfo destination_info{};
        destination_info.imageView = target.impl_->view;
        destination_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorImageInfo source_info{};
        source_info.imageView = drawn[i]->source->impl_->view;
        source_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet writes[2]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = impl_->sets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].pImageInfo = &destination_info;
        writes[1] = writes[0];
        writes[1].dstBinding = 1;
        writes[1].pImageInfo = &source_info;
        vkUpdateDescriptorSets(handle, 2, writes, 0, nullptr);
    }

    VkResult result = vkResetCommandBuffer(impl_->commands, 0);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot reset the composite command buffer");
    }
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(impl_->commands, &begin);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot begin the composite command buffer");
    }

    // The backdrop is opaque black, cleared every frame so a timeline with
    // nothing on it shows black rather than the previous frame.
    const VkImageLayout target_from =
        target.impl_->initialised ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
    image_barrier(impl_->commands, target.impl_->image, target_from,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkClearColorValue black{};
    black.float32[3] = 1.0F;
    VkImageSubresourceRange whole{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(impl_->commands, target.impl_->image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &whole);

    image_barrier(impl_->commands, target.impl_->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    vkCmdBindPipeline(impl_->commands, VK_PIPELINE_BIND_POINT_COMPUTE, impl_->pipeline);

    for (std::size_t i = 0; i < drawn.size(); ++i) {
        vkCmdBindDescriptorSets(impl_->commands, VK_PIPELINE_BIND_POINT_COMPUTE,
                                impl_->pipeline_layout, 0, 1, &impl_->sets[i], 0, nullptr);

        const CompositePush push{image_width, image_height, drawn[i]->opacity};
        vkCmdPushConstants(impl_->commands, impl_->pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           sizeof(push), &push);
        vkCmdDispatch(impl_->commands, (image_width + kGroupSize - 1) / kGroupSize,
                      (image_height + kGroupSize - 1) / kGroupSize, 1);

        if (i + 1 < drawn.size()) {
            // The next layer reads the destination this dispatch just wrote.
            image_barrier(impl_->commands, target.impl_->image, VK_IMAGE_LAYOUT_GENERAL,
                          VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT,
                          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }
    }

    result = vkEndCommandBuffer(impl_->commands);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot end the composite command buffer");
    }

    result = vkResetFences(handle, 1, &impl_->fence);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot reset the composite fence");
    }

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &impl_->commands;
    result = vkQueueSubmit(impl_->device->queue, 1, &submit, impl_->fence);
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "cannot submit the composite");
    }

    result = vkWaitForFences(handle, 1, &impl_->fence, VK_TRUE, kTimeoutNanoseconds);
    if (result == VK_TIMEOUT) {
        return Error{Errc::timeout, "the GPU did not finish the composite within 30 seconds"};
    }
    if (result != VK_SUCCESS) {
        return from_vulkan(result, "waiting for the composite failed");
    }

    target.impl_->initialised = true;
    return ok();
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

    // Device is reconstructed here purely to hand Texture::create an owner; the
    // shared impl means this is a reference to the same device, not a new one.
    Device owner{impl_->device};
    if (Result<void> ready = impl_->ensure_staging(owner, width, height, drawn.size()); !ready) {
        return ready.error();
    }

    std::vector<GpuLayer> gpu_layers;
    gpu_layers.reserve(drawn.size());
    for (std::size_t i = 0; i < drawn.size(); ++i) {
        if (Result<void> uploaded = impl_->staged_layers[i].upload(drawn[i]->source); !uploaded) {
            return uploaded.error().with_context("layer " + std::to_string(i));
        }
        gpu_layers.push_back({&impl_->staged_layers[i], drawn[i]->opacity, true});
    }

    if (Result<void> composed = composite_into(*impl_->staged_target, gpu_layers); !composed) {
        return composed.error();
    }
    return impl_->staged_target->read_back();
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
                const float above =
                    static_cast<float>(layer.source.pixels[pixel + channel]) / 255.0F;
                const float below = static_cast<float>(image.pixels[pixel + channel]) / 255.0F;
                image.pixels[pixel + channel] =
                    to_byte(above * coverage + below * (1.0F - coverage));
            }
            image.pixels[pixel + 3] = 255u;
        }
    }
    return image;
}

}  // namespace rf::gpu
