// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/backend/device.hpp>

#ifdef __SWITCH__

#include <lsfg/common/dksh.hpp>

#include <cstdio>
#include <cstring>

static_assert(lsfg::backend::memory_block_alignment == DK_MEMBLOCK_ALIGNMENT);
static_assert(lsfg::backend::uniform_buffer_stride % DK_UNIFORM_BUF_ALIGNMENT == 0);
static_assert(lsfg::backend::sampler_descriptor_count <= DK_NUM_TEXTURE_BINDINGS);

namespace lsfg::backend {
namespace {

constexpr std::uint32_t descriptor_size = sizeof(DkImageDescriptor);

static_assert(sizeof(DkImageDescriptor) == sizeof(DkSamplerDescriptor));

[[nodiscard]] DkImageFormat image_format(const graph::Format format) noexcept {
    switch (format) {
    case graph::Format::rgba8:
        return DkImageFormat_RGBA8_Unorm;
    case graph::Format::r8:
        return DkImageFormat_R8_Unorm;
    case graph::Format::rgba16f:
        return DkImageFormat_RGBA16_Float;
    }
    return DkImageFormat_None;
}

// The three the chain names, plus the one translation introduces for the
// modules that only ask an image its size.
void describe_sampler(const std::uint32_t index, DkSampler& out) noexcept {
    dkSamplerDefaults(&out);
    out.minFilter = DkFilter_Linear;
    out.magFilter = DkFilter_Linear;
    out.mipFilter = DkMipFilter_Linear;

    const bool clamp_to_edge = index == static_cast<std::uint32_t>(graph::Sampler::edge)
        || index == introduced_sampler;
    const DkWrapMode wrap = clamp_to_edge ? DkWrapMode_ClampToEdge : DkWrapMode_ClampToBorder;
    for (DkWrapMode& mode : out.wrapMode) {
        mode = wrap;
    }

    if (index == static_cast<std::uint32_t>(graph::Sampler::border_white)) {
        for (auto& channel : out.borderColor) {
            channel.value_f = 1.0F;
        }
    }
}

[[nodiscard]] std::uint32_t image_flags(const graph::ImageRole role) noexcept {
    std::uint32_t flags = DkImageFlags_UsageLoadStore;
    if (role != graph::ImageRole::internal && role != graph::ImageRole::constant) {
        // A frame that presentation would have supplied is copied rather than
        // computed, so a stand-in for one has to be reachable by the 2D engine.
        flags |= DkImageFlags_Usage2DEngine;
    }
    return flags;
}

[[nodiscard]] std::uint64_t block_bytes(const std::uint64_t size) noexcept {
    constexpr std::uint64_t mask = memory_block_alignment - 1U;
    return (size + mask) & ~mask;
}

[[nodiscard]] DkMemBlock create_block(
    const DkDevice device,
    const std::uint64_t size,
    const std::uint32_t flags) noexcept {
    const std::uint64_t rounded = block_bytes(size);
    if (rounded == 0 || rounded > max_memory_block_bytes) {
        return nullptr;
    }

    DkMemBlockMaker maker;
    dkMemBlockMakerDefaults(&maker, device, static_cast<std::uint32_t>(rounded));
    maker.flags = flags;
    return dkMemBlockCreate(&maker);
}

} // namespace

Device::~Device() {
    destroy();
}

void Device::report(
    void* const user,
    const char* const context,
    const DkResult result,
    const char* const message) {
    auto* const self = static_cast<Device*>(user);
    if (self == nullptr) {
        return;
    }

    std::snprintf(
        self->message_.data(),
        self->message_.size(),
        "%s: result %d, %s",
        context != nullptr ? context : "deko3d",
        static_cast<int>(result),
        message != nullptr ? message : "no message");
}

std::string_view Device::last_error() const noexcept {
    return {message_.data()};
}

ErrorCode Device::create(const DeviceOptions& options) noexcept {
    if (device_ != nullptr) {
        return ErrorCode::invalid_state;
    }

    message_[0] = '\0';

    DkDeviceMaker device_maker;
    dkDeviceMakerDefaults(&device_maker);
    device_maker.userData = this;
    device_maker.cbDebug = &Device::report;

    device_ = dkDeviceCreate(&device_maker);
    if (device_ == nullptr) {
        return ErrorCode::backend_unavailable;
    }

    DkQueueMaker queue_maker;
    dkQueueMakerDefaults(&queue_maker, device_);
    queue_maker.flags = DkQueueFlags_Compute | DkQueueFlags_MediumPrio | DkQueueFlags_DisableZcull;
    queue_maker.commandMemorySize = options.command_memory_bytes;
    queue_maker.flushThreshold = options.command_memory_bytes / 8U;

    if (options.per_warp_scratch_bytes != 0) {
        constexpr std::uint32_t alignment = DK_PER_WARP_SCRATCH_MEM_ALIGNMENT;
        queue_maker.perWarpScratchMemorySize
            = (options.per_warp_scratch_bytes + alignment - 1U) & ~(alignment - 1U);
    }

    queue_ = dkQueueCreate(&queue_maker);
    if (queue_ == nullptr) {
        dkDeviceDestroy(device_);
        device_ = nullptr;
        return ErrorCode::backend_unavailable;
    }

    return ErrorCode::ok;
}

void Device::destroy() noexcept {
    if (queue_ != nullptr) {
        dkQueueWaitIdle(queue_);
        dkQueueDestroy(queue_);
        queue_ = nullptr;
    }
    if (device_ != nullptr) {
        dkDeviceDestroy(device_);
        device_ = nullptr;
    }
}

Resources::~Resources() {
    destroy();
}

ErrorCode Resources::lay_out_images(
    const Plan& plan,
    const ResourceOptions& options,
    const DkDevice device,
    Arena& arena) {
    images_.assign(plan.images.size(), DkImage{});
    layouts_.assign(plan.images.size(), DkImageLayout{});
    image_offsets_.assign(plan.images.size(), 0);
    owned_.assign(plan.images.size(), false);

    for (std::size_t index = 0; index < plan.images.size(); ++index) {
        const ImagePlan& image = plan.images[index];
        const bool imported = image.role == graph::ImageRole::history
            || image.role == graph::ImageRole::generated;
        if (imported && !options.own_imported_images) {
            continue;
        }

        const DkImageFormat format = image_format(image.format);
        if (format == DkImageFormat_None) {
            return ErrorCode::image_layout_unsupported;
        }

        DkImageLayoutMaker maker;
        dkImageLayoutMakerDefaults(&maker, device);
        maker.type = DkImageType_2D;
        maker.flags = image_flags(image.role);
        maker.format = format;
        maker.dimensions[0] = image.extent.width;
        maker.dimensions[1] = image.extent.height;

        dkImageLayoutInitialize(&layouts_[index], &maker);

        const std::uint64_t size = dkImageLayoutGetSize(&layouts_[index]);
        const std::uint32_t alignment = dkImageLayoutGetAlignment(&layouts_[index]);

        image_offsets_[index] = arena.place(size, alignment);
        owned_[index] = true;
        ++allocation_.images;

        if (imported) {
            allocation_.imported_image_bytes += size;
        } else {
            allocation_.owned_image_bytes += size;
        }
    }

    return arena.overflowed() ? ErrorCode::out_of_memory : ErrorCode::ok;
}

ErrorCode Resources::write_descriptors() {
    auto* const table = static_cast<std::uint8_t*>(dkMemBlockGetCpuAddr(descriptor_memory_));
    if (table == nullptr) {
        return ErrorCode::backend_unavailable;
    }

    for (std::size_t index = 0; index < descriptors_.images.size(); ++index) {
        const ImageDescriptors& entry = descriptors_.images[index];
        if (entry.sampled == no_descriptor && entry.storage == no_descriptor) {
            continue;
        }
        if (!owned_[index]) {
            continue;
        }

        DkImageView view;
        dkImageViewDefaults(&view, &images_[index]);

        if (entry.sampled != no_descriptor) {
            auto* const target = reinterpret_cast<DkImageDescriptor*>(
                table + (entry.sampled * descriptor_size));
            dkImageDescriptorInitialize(target, &view, false, false);
        }
        if (entry.storage != no_descriptor) {
            auto* const target = reinterpret_cast<DkImageDescriptor*>(
                table + (entry.storage * descriptor_size));
            dkImageDescriptorInitialize(target, &view, true, false);
        }
    }

    for (std::uint32_t index = 0; index < sampler_descriptor_count; ++index) {
        DkSampler sampler;
        describe_sampler(index, sampler);

        auto* const target = reinterpret_cast<DkSamplerDescriptor*>(
            table + sampler_offset_ + (index * descriptor_size));
        dkSamplerDescriptorInitialize(target, &sampler);
    }

    return ErrorCode::ok;
}

void Resources::write_uniform_buffers(const graph::Config& config) {
    auto* const base = static_cast<std::uint8_t*>(dkMemBlockGetCpuAddr(uniform_memory_));
    if (base == nullptr) {
        return;
    }

    for (std::uint32_t index = 0; index < uniform_buffers_; ++index) {
        const graph::ConstantBuffer contents = graph::constant_buffer(index, config);
        std::memcpy(base + (index * uniform_buffer_stride), &contents, sizeof(contents));
    }
}

ErrorCode Resources::lay_out_modules(const cache::Loaded& cache, Arena& arena) {
    code_offsets_.clear();
    code_offsets_.reserve(cache.passes.size());

    for (const cache::LoadedPass& pass : cache.passes) {
        if (pass.dksh.size() < sizeof(dksh::FileHeader)) {
            return ErrorCode::cache_integrity_failure;
        }

        dksh::FileHeader header{};
        std::memcpy(&header, pass.dksh.data(), sizeof(header));
        if (header.magic != dksh::magic
            || static_cast<std::uint64_t>(header.control_size) + header.code_size
                > pass.dksh.size()) {
            return ErrorCode::cache_integrity_failure;
        }

        code_offsets_.push_back(arena.place(header.code_size, DK_SHADER_CODE_ALIGNMENT));
    }

    static_cast<void>(arena.place(DK_SHADER_CODE_UNUSABLE_SIZE, 1));

    return arena.overflowed() ? ErrorCode::out_of_memory : ErrorCode::ok;
}

ErrorCode Resources::load_modules(const cache::Loaded& cache) {
    auto* const code = static_cast<std::uint8_t*>(dkMemBlockGetCpuAddr(code_memory_));
    if (code == nullptr) {
        return ErrorCode::backend_unavailable;
    }

    modules_.assign(cache.passes.size(), DkShader{});

    for (std::size_t index = 0; index < cache.passes.size(); ++index) {
        const cache::LoadedPass& pass = cache.passes[index];

        dksh::FileHeader header{};
        std::memcpy(&header, pass.dksh.data(), sizeof(header));

        std::memcpy(
            code + code_offsets_[index],
            pass.dksh.data() + header.control_size,
            header.code_size);

        DkShaderMaker maker;
        dkShaderMakerDefaults(&maker, code_memory_, code_offsets_[index]);
        maker.control = pass.dksh.data();

        dkShaderInitialize(&modules_[index], &maker);
        if (!dkShaderIsValid(&modules_[index])) {
            return ErrorCode::shader_interface_mismatch;
        }
        ++allocation_.modules;
    }

    return ErrorCode::ok;
}

ErrorCode Resources::create(
    const Device& device,
    const cache::Loaded& cache,
    const Plan& plan,
    const ResourceOptions& options) {
    destroy();

    if (!device.valid()) {
        return ErrorCode::backend_unavailable;
    }
    if (plan.images.size() != cache.graph.images.size()) {
        return ErrorCode::invalid_argument;
    }

    const DkDevice handle = device.handle();

    if (const ErrorCode code = describe(cache.graph, descriptors_); !succeeded(code)) {
        destroy();
        return code;
    }
    allocation_.image_descriptors = descriptors_.image_descriptors;

    Arena images;
    if (const ErrorCode code = lay_out_images(plan, options, handle, images); !succeeded(code)) {
        destroy();
        return code;
    }

    Arena code_arena;
    if (const ErrorCode code = lay_out_modules(cache, code_arena); !succeeded(code)) {
        destroy();
        return code;
    }

    sampler_offset_ = descriptors_.image_descriptors * descriptor_size;
    uniform_buffers_ = cache.graph.uniform_buffer_count;

    const std::uint64_t descriptor_size_wanted = sampler_offset_
        + (sampler_descriptor_count * descriptor_size);
    const std::uint64_t uniform_size_wanted = static_cast<std::uint64_t>(uniform_buffers_)
        * uniform_buffer_stride;

    allocation_.descriptor_bytes = block_bytes(descriptor_size_wanted);
    allocation_.uniform_bytes = block_bytes(uniform_size_wanted);
    allocation_.code_bytes = code_arena.block_size();

    // Nothing has been created yet, so a chain that would not fit costs the
    // process nothing to refuse.
    if (allocation_.total() > options.memory_budget_bytes) {
        destroy();
        return ErrorCode::out_of_memory;
    }

    image_memory_ = create_block(
        handle,
        images.block_size(),
        DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image | DkMemBlockFlags_ZeroFillInit);
    if (image_memory_ == nullptr) {
        destroy();
        return ErrorCode::out_of_memory;
    }

    for (std::size_t index = 0; index < images_.size(); ++index) {
        if (owned_[index]) {
            dkImageInitialize(
                &images_[index], &layouts_[index], image_memory_, image_offsets_[index]);
        }
    }

    descriptor_memory_ = create_block(
        handle, descriptor_size_wanted, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    if (descriptor_memory_ == nullptr) {
        destroy();
        return ErrorCode::out_of_memory;
    }

    if (const ErrorCode code = write_descriptors(); !succeeded(code)) {
        destroy();
        return code;
    }

    if (uniform_buffers_ != 0) {
        uniform_memory_ = create_block(
            handle, uniform_size_wanted, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        if (uniform_memory_ == nullptr) {
            destroy();
            return ErrorCode::out_of_memory;
        }
        write_uniform_buffers(cache.graph.config);
    }

    code_memory_ = create_block(
        handle,
        code_arena.block_size(),
        DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);
    if (code_memory_ == nullptr) {
        destroy();
        return ErrorCode::out_of_memory;
    }

    if (const ErrorCode code = load_modules(cache); !succeeded(code)) {
        destroy();
        return code;
    }

    // The layouts and the offsets only place the images and the code.
    layouts_.clear();
    layouts_.shrink_to_fit();
    image_offsets_.clear();
    image_offsets_.shrink_to_fit();
    code_offsets_.clear();
    code_offsets_.shrink_to_fit();

    return ErrorCode::ok;
}

void Resources::destroy() noexcept {
    modules_.clear();
    modules_.shrink_to_fit();
    code_offsets_.clear();
    code_offsets_.shrink_to_fit();

    images_.clear();
    images_.shrink_to_fit();
    layouts_.clear();
    layouts_.shrink_to_fit();
    image_offsets_.clear();
    image_offsets_.shrink_to_fit();
    owned_.clear();

    descriptors_ = DescriptorLayout{};

    for (DkMemBlock* const block :
         {&code_memory_, &uniform_memory_, &descriptor_memory_, &image_memory_}) {
        if (*block != nullptr) {
            dkMemBlockDestroy(*block);
            *block = nullptr;
        }
    }

    sampler_offset_ = 0;
    uniform_buffers_ = 0;
    allocation_ = Allocation{};
}

const DkImage* Resources::image(const std::uint32_t index) const noexcept {
    if (index >= images_.size() || !owned_[index]) {
        return nullptr;
    }
    return &images_[index];
}

const DkShader* Resources::module(const std::uint32_t pass) const noexcept {
    if (pass >= modules_.size()) {
        return nullptr;
    }
    return &modules_[pass];
}

DkGpuAddr Resources::image_descriptor_set() const noexcept {
    if (descriptor_memory_ == nullptr) {
        return DK_GPU_ADDR_INVALID;
    }
    return dkMemBlockGetGpuAddr(descriptor_memory_);
}

DkGpuAddr Resources::sampler_descriptor_set() const noexcept {
    if (descriptor_memory_ == nullptr) {
        return DK_GPU_ADDR_INVALID;
    }
    return dkMemBlockGetGpuAddr(descriptor_memory_) + sampler_offset_;
}

DkGpuAddr Resources::uniform_buffer(const std::uint32_t index) const noexcept {
    if (uniform_memory_ == nullptr || index >= uniform_buffers_) {
        return DK_GPU_ADDR_INVALID;
    }
    return dkMemBlockGetGpuAddr(uniform_memory_) + (index * uniform_buffer_stride);
}

} // namespace lsfg::backend

#endif // __SWITCH__
