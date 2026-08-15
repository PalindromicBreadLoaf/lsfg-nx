// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef __SWITCH__

#include <lsfg/backend/cache_load.hpp>
#include <lsfg/backend/layout.hpp>
#include <lsfg/common/cache_store.hpp>
#include <lsfg/common/error.hpp>

#include <deko3d.h>

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace lsfg::backend {

struct DeviceOptions {
    std::uint32_t per_warp_scratch_bytes{};
    std::uint32_t command_memory_bytes{DK_QUEUE_MIN_CMDMEM_SIZE};
};

// A deko device and a compute queue of this project's own.
class Device {
public:
    Device() noexcept = default;
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;

    [[nodiscard]] ErrorCode create(const DeviceOptions& options) noexcept;

    void destroy() noexcept;

    [[nodiscard]] bool valid() const noexcept {
        return device_ != nullptr;
    }

    [[nodiscard]] DkDevice handle() const noexcept {
        return device_;
    }

    [[nodiscard]] DkQueue queue() const noexcept {
        return queue_;
    }

    // deko3d reports a failure through a callback and treats it as fatal, so
    // this is what there is to say about one.
    [[nodiscard]] std::string_view last_error() const noexcept;

private:
    static void report(void* user, const char* context, DkResult result, const char* message);

    DkDevice device_{};
    DkQueue queue_{};
    std::array<char, 192> message_{};
};

struct BorrowedMemoryOptions {
    std::uint32_t nvmap_id{};
    std::uint32_t expected_size{};
    bool gpu_cached{true};
};

class BorrowedMemoryBlock {
public:
    BorrowedMemoryBlock() noexcept = default;
    ~BorrowedMemoryBlock();

    BorrowedMemoryBlock(const BorrowedMemoryBlock&) = delete;
    BorrowedMemoryBlock& operator=(const BorrowedMemoryBlock&) = delete;
    BorrowedMemoryBlock(BorrowedMemoryBlock&&) = delete;
    BorrowedMemoryBlock& operator=(BorrowedMemoryBlock&&) = delete;

    [[nodiscard]] ErrorCode create(
        const Device& device, const BorrowedMemoryOptions& options) noexcept;

    void destroy() noexcept;

    [[nodiscard]] DkMemBlock handle() const noexcept {
        return memory_;
    }

    [[nodiscard]] bool valid() const noexcept {
        return memory_ != nullptr;
    }

private:
    DkMemBlock memory_{};
};

struct ResourceOptions {
    // With no presentation to import from, a standalone harness asks for
    // deko-owned stand ins for the frames a game would supply.
    bool own_imported_images{};
    // A harness reads intermediate images back to check them, and the copy
    // engine can only reach an image that was laid out for it.
    bool copyable_images{};
    std::uint64_t memory_budget_bytes{default_memory_budget_bytes};
};

struct Allocation {
    std::uint64_t owned_image_bytes{};
    std::uint64_t imported_image_bytes{};
    std::uint64_t descriptor_bytes{};
    std::uint64_t uniform_bytes{};
    std::uint64_t code_bytes{};

    std::uint32_t images{};
    std::uint32_t image_descriptors{};
    std::uint32_t modules{};

    [[nodiscard]] std::uint64_t total() const noexcept {
        return owned_image_bytes + imported_image_bytes + descriptor_bytes + uniform_bytes
            + code_bytes;
    }
};

// Every GPU object an accepted cache turns into, other than the command
// buffers that dispatch it.
class Resources {
public:
    Resources() noexcept = default;
    ~Resources();

    Resources(const Resources&) = delete;
    Resources& operator=(const Resources&) = delete;
    Resources(Resources&&) = delete;
    Resources& operator=(Resources&&) = delete;

    [[nodiscard]] ErrorCode create(
        const Device& device,
        const cache::Loaded& cache,
        const Plan& plan,
        const ResourceOptions& options);

    void destroy() noexcept;

    [[nodiscard]] const Allocation& allocation() const noexcept {
        return allocation_;
    }

    [[nodiscard]] const DescriptorLayout& descriptors() const noexcept {
        return descriptors_;
    }

    // Null for an image the backend does not own, which is every history and
    // generated frame once presentation supplies them.
    [[nodiscard]] const DkImage* image(std::uint32_t index) const noexcept;

    [[nodiscard]] const DkShader* module(std::uint32_t pass) const noexcept;

    [[nodiscard]] DkGpuAddr image_descriptor_set() const noexcept;
    [[nodiscard]] DkGpuAddr sampler_descriptor_set() const noexcept;

    // Zero for an index the chain does not have a uniform buffer for.
    [[nodiscard]] DkGpuAddr uniform_buffer(std::uint32_t index) const noexcept;

private:
    [[nodiscard]] ErrorCode lay_out_images(
        const Plan& plan,
        const ResourceOptions& options,
        DkDevice device,
        Arena& arena);

    [[nodiscard]] ErrorCode lay_out_modules(const cache::Loaded& cache, Arena& arena);
    [[nodiscard]] ErrorCode load_modules(const cache::Loaded& cache);
    [[nodiscard]] ErrorCode write_descriptors();
    void write_uniform_buffers(const graph::Config& config);

    DkMemBlock image_memory_{};
    DkMemBlock descriptor_memory_{};
    DkMemBlock uniform_memory_{};
    DkMemBlock code_memory_{};

    std::vector<DkImage> images_;
    std::vector<DkImageLayout> layouts_;
    std::vector<std::uint32_t> image_offsets_;
    std::vector<bool> owned_;

    std::vector<DkShader> modules_;
    std::vector<std::uint32_t> code_offsets_;

    DescriptorLayout descriptors_;
    std::uint32_t sampler_offset_{};
    std::uint32_t uniform_buffers_{};

    Allocation allocation_{};
};

} // namespace lsfg::backend

#endif // __SWITCH__
