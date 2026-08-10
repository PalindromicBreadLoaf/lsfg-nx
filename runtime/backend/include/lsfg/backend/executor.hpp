// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef __SWITCH__

#include <lsfg/backend/binding.hpp>
#include <lsfg/backend/cache_load.hpp>
#include <lsfg/backend/device.hpp>
#include <lsfg/backend/schedule.hpp>
#include <lsfg/common/cache_store.hpp>
#include <lsfg/common/error.hpp>

#include <deko3d.h>

#include <cstdint>
#include <span>
#include <vector>

namespace lsfg::backend {

// A block of memory the CPU writes and the GPU copies out of, which is how a
// frame gets into a block linear image and how one is read back out.
class Staging {
public:
    Staging() noexcept = default;
    ~Staging();

    Staging(const Staging&) = delete;
    Staging& operator=(const Staging&) = delete;
    Staging(Staging&&) = delete;
    Staging& operator=(Staging&&) = delete;

    [[nodiscard]] ErrorCode create(const Device& device, std::uint64_t bytes);

    void destroy() noexcept;

    [[nodiscard]] std::span<std::uint8_t> bytes() noexcept;
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept;

    [[nodiscard]] DkGpuAddr address() const noexcept;

    [[nodiscard]] std::uint32_t size() const noexcept {
        return size_;
    }

private:
    DkMemBlock memory_{};
    std::uint8_t* host_{};
    std::uint32_t size_{};
};

struct ExecutorOptions {
    // The whole chain is about a hundred dispatches.
    std::uint32_t command_memory_bytes{256U * 1024U};
};

// Records the chain's dispatches into a command buffer and runs them.
class Executor {
public:
    Executor() noexcept = default;
    ~Executor();

    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;
    Executor(Executor&&) = delete;
    Executor& operator=(Executor&&) = delete;

    [[nodiscard]] ErrorCode create(
        const Device& device,
        const cache::Loaded& cache,
        const Plan& plan,
        const Resources& resources,
        const ExecutorOptions& options);

    void destroy() noexcept;

    // Binding the descriptor tables the whole chain shares.
    void begin() noexcept;

    // Records one dispatch of the chain at a real frame index.
    [[nodiscard]] ErrorCode record(std::uint32_t dispatch, std::uint32_t phase);

    // Records one stage at a real frame index, barriered only where it has to be.
    [[nodiscard]] ErrorCode record_stage(
        const Schedule& schedule,
        std::uint32_t stage,
        std::uint32_t frame);

    // Records the prepass and every generated frame after it.
    [[nodiscard]] ErrorCode record_chain(const Schedule& schedule, std::uint32_t frame);

    void barrier(std::uint32_t invalidate = DkInvalidateFlags_Image) noexcept;

    [[nodiscard]] ErrorCode record_upload(
        std::uint32_t image,
        const Staging& staging,
        std::uint64_t offset) noexcept;

    [[nodiscard]] ErrorCode record_download(
        std::uint32_t image,
        const Staging& staging,
        std::uint64_t offset) noexcept;

    // Submits what has been recorded and waits for the queue to drain.
    [[nodiscard]] ErrorCode run();

    [[nodiscard]] std::uint32_t recorded_dispatches() const noexcept {
        return recorded_;
    }

    [[nodiscard]] std::uint32_t recorded_barriers() const noexcept {
        return barriers_;
    }

private:
    [[nodiscard]] ErrorCode copy(
        std::uint32_t image,
        const Staging& staging,
        std::uint64_t offset,
        bool upload) noexcept;

    void mark_read(std::uint32_t image) noexcept;
    void mark_written(std::uint32_t image) noexcept;

    static void out_of_command_memory(void* user, DkCmdBuf commands, std::size_t needed);

    const Device* device_{};
    const cache::Loaded* cache_{};
    const Plan* plan_{};
    const Resources* resources_{};

    DkMemBlock command_memory_{};
    DkCmdBuf commands_{};

    // Per image, whether it has been read or written since the last barrier.
    static constexpr std::uint8_t hazard_read = 1U << 0U;
    static constexpr std::uint8_t hazard_written = 1U << 1U;
    std::vector<std::uint8_t> hazards_;
    std::vector<std::uint32_t> hazard_images_;

    std::uint32_t recorded_{};
    std::uint32_t barriers_{};
    bool recording_{};
    bool out_of_memory_{};
};

} // namespace lsfg::backend

#endif // __SWITCH__
