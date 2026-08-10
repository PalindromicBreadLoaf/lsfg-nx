// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/backend/executor.hpp>

#ifdef __SWITCH__

#include <algorithm>
#include <array>

static_assert(lsfg::backend::max_texture_slots <= DK_NUM_TEXTURE_BINDINGS);
static_assert(lsfg::backend::max_storage_slots <= DK_NUM_IMAGE_BINDINGS);
static_assert(lsfg::backend::max_uniform_slots <= DK_NUM_UNIFORM_BUFS);
static_assert(lsfg::backend::uniform_buffer_stride <= DK_UNIFORM_BUF_MAX_SIZE);

namespace lsfg::backend {
namespace {

[[nodiscard]] std::uint64_t staging_bytes(const ImagePlan& image) noexcept {
    return static_cast<std::uint64_t>(image.extent.width) * image.extent.height
        * graph::bytes_per_pixel(image.format);
}

} // namespace

Staging::~Staging() {
    destroy();
}

ErrorCode Staging::create(const Device& device, const std::uint64_t bytes) {
    destroy();

    if (!device.valid() || bytes == 0) {
        return ErrorCode::invalid_argument;
    }

    constexpr std::uint64_t mask = memory_block_alignment - 1U;
    const std::uint64_t rounded = (bytes + mask) & ~mask;
    if (rounded > max_memory_block_bytes) {
        return ErrorCode::out_of_memory;
    }

    DkMemBlockMaker maker;
    dkMemBlockMakerDefaults(&maker, device.handle(), static_cast<std::uint32_t>(rounded));
    // Uncached at both ends so that a copy the GPU has finished is one the CPU can
    // read without a cache operation to invalidate in either direction.
    maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuUncached;

    memory_ = dkMemBlockCreate(&maker);
    if (memory_ == nullptr) {
        return ErrorCode::out_of_memory;
    }

    host_ = static_cast<std::uint8_t*>(dkMemBlockGetCpuAddr(memory_));
    if (host_ == nullptr) {
        destroy();
        return ErrorCode::backend_unavailable;
    }

    size_ = static_cast<std::uint32_t>(rounded);
    return ErrorCode::ok;
}

void Staging::destroy() noexcept {
    if (memory_ != nullptr) {
        dkMemBlockDestroy(memory_);
        memory_ = nullptr;
    }
    host_ = nullptr;
    size_ = 0;
}

std::span<std::uint8_t> Staging::bytes() noexcept {
    return {host_, size_};
}

std::span<const std::uint8_t> Staging::bytes() const noexcept {
    return {host_, size_};
}

DkGpuAddr Staging::address() const noexcept {
    if (memory_ == nullptr) {
        return DK_GPU_ADDR_INVALID;
    }
    return dkMemBlockGetGpuAddr(memory_);
}

Executor::~Executor() {
    destroy();
}

void Executor::out_of_command_memory(
    void* const user,
    DkCmdBuf /*commands*/,
    std::size_t /*needed*/) {
    auto* const self = static_cast<Executor*>(user);
    if (self != nullptr) {
        self->out_of_memory_ = true;
    }
}

ErrorCode Executor::create(
    const Device& device,
    const cache::Loaded& cache,
    const Plan& plan,
    const Resources& resources,
    const ExecutorOptions& options) {
    destroy();

    if (!device.valid() || options.command_memory_bytes == 0) {
        return ErrorCode::invalid_argument;
    }
    if (resources.descriptors().images.size() != cache.graph.images.size()) {
        return ErrorCode::invalid_argument;
    }

    constexpr std::uint64_t mask = memory_block_alignment - 1U;
    const std::uint64_t rounded = (options.command_memory_bytes + mask) & ~mask;

    DkMemBlockMaker maker;
    dkMemBlockMakerDefaults(&maker, device.handle(), static_cast<std::uint32_t>(rounded));
    maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;

    command_memory_ = dkMemBlockCreate(&maker);
    if (command_memory_ == nullptr) {
        return ErrorCode::out_of_memory;
    }

    DkCmdBufMaker command_maker;
    dkCmdBufMakerDefaults(&command_maker, device.handle());
    command_maker.userData = this;
    command_maker.cbAddMem = &Executor::out_of_command_memory;

    commands_ = dkCmdBufCreate(&command_maker);
    if (commands_ == nullptr) {
        destroy();
        return ErrorCode::backend_unavailable;
    }

    hazards_.assign(cache.graph.images.size(), 0);
    hazard_images_.reserve(cache.graph.images.size());

    device_ = &device;
    cache_ = &cache;
    plan_ = &plan;
    resources_ = &resources;
    return ErrorCode::ok;
}

void Executor::destroy() noexcept {
    if (commands_ != nullptr) {
        dkCmdBufDestroy(commands_);
        commands_ = nullptr;
    }
    if (command_memory_ != nullptr) {
        dkMemBlockDestroy(command_memory_);
        command_memory_ = nullptr;
    }

    hazards_.clear();
    hazards_.shrink_to_fit();
    hazard_images_.clear();
    hazard_images_.shrink_to_fit();

    device_ = nullptr;
    cache_ = nullptr;
    plan_ = nullptr;
    resources_ = nullptr;
    recorded_ = 0;
    barriers_ = 0;
    recording_ = false;
    out_of_memory_ = false;
}

void Executor::begin() noexcept {
    if (commands_ == nullptr) {
        return;
    }

    dkCmdBufClear(commands_);
    dkCmdBufAddMemory(commands_, command_memory_, 0, dkMemBlockGetSize(command_memory_));

    recorded_ = 0;
    barriers_ = 0;
    recording_ = true;
    out_of_memory_ = false;

    for (const std::uint32_t image : hazard_images_) {
        hazards_[image] = 0;
    }
    hazard_images_.clear();

    const DescriptorLayout& descriptors = resources_->descriptors();
    dkCmdBufBindImageDescriptorSet(
        commands_, resources_->image_descriptor_set(), descriptors.image_descriptors);
    dkCmdBufBindSamplerDescriptorSet(
        commands_, resources_->sampler_descriptor_set(), sampler_descriptor_count);
    // Nothing carries over from the list before this one.
    dkCmdBufBarrier(
        commands_, DkBarrier_None, DkInvalidateFlags_Descriptors | DkInvalidateFlags_Image);
}

ErrorCode Executor::record(const std::uint32_t dispatch, const std::uint32_t phase) {
    if (commands_ == nullptr || !recording_) {
        return ErrorCode::invalid_state;
    }

    DispatchBinding binding;
    if (const ErrorCode code
        = bind(*cache_, *plan_, resources_->descriptors(), dispatch, phase, binding);
        !succeeded(code)) {
        return code;
    }

    const DkShader* const shader = resources_->module(binding.pass);
    if (shader == nullptr || !dkShaderIsValid(shader)) {
        return ErrorCode::shader_interface_mismatch;
    }

    bool wait = false;
    std::uint32_t invalidate = 0;
    for (std::uint32_t index = 0; index < binding.texture_count; ++index) {
        if ((hazards_[binding.textures[index].image] & hazard_written) != 0) {
            wait = true;
            invalidate |= DkInvalidateFlags_Image;
        }
    }
    for (std::uint32_t index = 0; index < binding.storage_count; ++index) {
        // Anything already touched this batch is a hazard in either direction.
        constexpr std::uint8_t touched = hazard_read | hazard_written;
        if ((hazards_[binding.storages[index].image] & touched) != 0) {
            wait = true;
        }
    }
    if (wait) {
        barrier(invalidate);
    }

    for (std::uint32_t index = 0; index < binding.texture_count; ++index) {
        mark_read(binding.textures[index].image);
    }
    for (std::uint32_t index = 0; index < binding.storage_count; ++index) {
        mark_written(binding.storages[index].image);
    }

    dkCmdBufBindShaders(commands_, DkStageFlag_Compute, &shader, 1);

    std::array<DkResHandle, max_texture_slots> textures{};
    std::uint32_t texture_slots = 0;
    for (std::uint32_t index = 0; index < binding.texture_count; ++index) {
        const TextureBinding& texture = binding.textures[index];
        textures[texture.slot] = dkMakeTextureHandle(texture.descriptor, texture.sampler);
        texture_slots = std::max(texture_slots, texture.slot + 1U);
    }
    if (texture_slots != 0) {
        dkCmdBufBindTextures(commands_, DkStage_Compute, 0, textures.data(), texture_slots);
    }

    std::array<DkResHandle, max_storage_slots> storages{};
    std::uint32_t storage_slots = 0;
    for (std::uint32_t index = 0; index < binding.storage_count; ++index) {
        const StorageBinding& storage = binding.storages[index];
        storages[storage.slot] = dkMakeImageHandle(storage.descriptor);
        storage_slots = std::max(storage_slots, storage.slot + 1U);
    }
    if (storage_slots != 0) {
        dkCmdBufBindImages(commands_, DkStage_Compute, 0, storages.data(), storage_slots);
    }

    if (binding.uniform_slot != no_slot) {
        const DkGpuAddr address = resources_->uniform_buffer(binding.uniform_buffer);
        if (address == DK_GPU_ADDR_INVALID) {
            return ErrorCode::invalid_state;
        }
        dkCmdBufBindUniformBuffer(
            commands_, DkStage_Compute, binding.uniform_slot, address, uniform_buffer_stride);
    }

    dkCmdBufDispatchCompute(commands_, binding.groups_x, binding.groups_y, 1);
    ++recorded_;
    return ErrorCode::ok;
}

ErrorCode Executor::record_stage(
    const Schedule& schedule,
    const std::uint32_t stage,
    const std::uint32_t frame) {
    if (stage >= schedule.stages.size()) {
        return ErrorCode::invalid_argument;
    }

    const StageRange& range = schedule.stages[stage];
    for (std::uint32_t index = 0; index < range.count; ++index) {
        if (const ErrorCode code = record(range.first + index, frame); !succeeded(code)) {
            return code;
        }
    }
    return ErrorCode::ok;
}

ErrorCode Executor::record_chain(const Schedule& schedule, const std::uint32_t frame) {
    for (std::uint32_t stage = 0; stage < schedule.stages.size(); ++stage) {
        if (const ErrorCode code = record_stage(schedule, stage, frame); !succeeded(code)) {
            return code;
        }
    }
    return ErrorCode::ok;
}

void Executor::barrier(const std::uint32_t invalidate) noexcept {
    if (commands_ == nullptr || !recording_) {
        return;
    }

    dkCmdBufBarrier(commands_, DkBarrier_Primitives, invalidate);
    ++barriers_;

    for (const std::uint32_t image : hazard_images_) {
        hazards_[image] = 0;
    }
    hazard_images_.clear();
}

void Executor::mark_read(const std::uint32_t image) noexcept {
    if (hazards_[image] == 0) {
        hazard_images_.push_back(image);
    }
    hazards_[image] |= hazard_read;
}

void Executor::mark_written(const std::uint32_t image) noexcept {
    if (hazards_[image] == 0) {
        hazard_images_.push_back(image);
    }
    hazards_[image] |= hazard_written;
}

ErrorCode Executor::copy(
    const std::uint32_t image,
    const Staging& staging,
    const std::uint64_t offset,
    const bool upload) noexcept {
    if (commands_ == nullptr || !recording_) {
        return ErrorCode::invalid_state;
    }
    if (image >= plan_->images.size()) {
        return ErrorCode::invalid_argument;
    }

    const DkImage* const target = resources_->image(image);
    if (target == nullptr) {
        return ErrorCode::invalid_state;
    }

    const ImagePlan& planned = plan_->images[image];
    const std::uint64_t bytes = staging_bytes(planned);
    if (offset > staging.size() || bytes > staging.size() - offset) {
        return ErrorCode::invalid_argument;
    }

    DkImageView view;
    dkImageViewDefaults(&view, target);

    const DkImageRect rect{
        .x = 0,
        .y = 0,
        .z = 0,
        .width = planned.extent.width,
        .height = planned.extent.height,
        .depth = 1,
    };
    // A zero row length and image height are the rect's own, so the staging
    // side is tightly packed and its stride never has to be agreed on.
    const DkCopyBuf buffer{
        .addr = staging.address() + offset,
        .rowLength = 0,
        .imageHeight = 0,
    };

    if (upload) {
        dkCmdBufCopyBufferToImage(commands_, &buffer, &view, &rect, 0);
        mark_written(image);
    } else {
        dkCmdBufCopyImageToBuffer(commands_, &view, &rect, &buffer, 0);
        mark_read(image);
    }
    return ErrorCode::ok;
}

ErrorCode Executor::record_upload(
    const std::uint32_t image,
    const Staging& staging,
    const std::uint64_t offset) noexcept {
    return copy(image, staging, offset, true);
}

ErrorCode Executor::record_download(
    const std::uint32_t image,
    const Staging& staging,
    const std::uint64_t offset) noexcept {
    return copy(image, staging, offset, false);
}

ErrorCode Executor::run() {
    if (commands_ == nullptr || !recording_) {
        return ErrorCode::invalid_state;
    }
    if (out_of_memory_) {
        return ErrorCode::out_of_memory;
    }

    recording_ = false;

    const DkCmdList list = dkCmdBufFinishList(commands_);
    if (list == 0) {
        return ErrorCode::backend_dispatch_failed;
    }

    const DkQueue queue = device_->queue();
    dkQueueSubmitCommands(queue, list);
    dkQueueWaitIdle(queue);

    if (dkQueueIsInErrorState(queue)) {
        return ErrorCode::backend_dispatch_failed;
    }
    return ErrorCode::ok;
}

} // namespace lsfg::backend

#endif // __SWITCH__
