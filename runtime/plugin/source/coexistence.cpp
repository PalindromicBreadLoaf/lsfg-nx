// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "coexistence.hpp"

#include "nv_session.hpp"

#include <coexistence_dksh.h>

#include <deko3d.h>
#include <switch.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace lsfg::plugin::coexistence {
namespace {

constexpr std::size_t arena_capacity = 1024U * 1024U;
constexpr std::uint32_t command_memory_size = DK_MEMBLOCK_ALIGNMENT;
constexpr std::int64_t completion_timeout_ns = 2'000'000'000LL;
constexpr std::size_t program_header_size = 64;
constexpr std::size_t program_type_offset = 0;
constexpr std::size_t per_warp_scratch_offset = 20;
constexpr std::uint32_t compute_program_type = 5;

struct DkshHeader {
    std::uint32_t magic;
    std::uint32_t header_size;
    std::uint32_t control_size;
    std::uint32_t code_size;
    std::uint32_t programs_offset;
    std::uint32_t program_count;
};

static_assert(sizeof(DkshHeader) == 24);

struct alignas(DK_MEMBLOCK_ALIGNMENT) Arena {
    std::array<std::byte, arena_capacity> storage{};
    std::size_t used{};
};

Arena g_arena{};
DkFence g_completion_fence{};

void notify(const ProgressCallback progress, const Stage stage) noexcept {
    if (progress != nullptr) {
        progress(stage);
    }
}

[[nodiscard]] DkResult allocate(
    void* const user, const std::size_t alignment, const std::size_t size, void** const out) {
    auto& arena = *static_cast<Arena*>(user);
    if (out == nullptr || alignment == 0 || (alignment & (alignment - 1U)) != 0) {
        return DkResult_BadInput;
    }

    const std::size_t offset = (arena.used + alignment - 1U) & ~(alignment - 1U);
    if (offset > arena.storage.size() || size > arena.storage.size() - offset) {
        *out = nullptr;
        return DkResult_OutOfMemory;
    }

    *out = arena.storage.data() + offset;
    arena.used = offset + size;
    return DkResult_Success;
}

void release(void* /*user*/, void* /*memory*/) {
}

[[nodiscard]] std::uint32_t read_u32(const std::size_t offset) noexcept {
    std::uint32_t value = 0;
    std::memcpy(&value, coexistence_dksh + offset, sizeof(value));
    return value;
}

[[nodiscard]] bool read_shader_header(
    DkshHeader& out, std::uint32_t& per_warp_scratch) noexcept {
    constexpr std::uint32_t dksh_magic = 0x4853'4b44U;
    if (coexistence_dksh_size < sizeof(out)) {
        return false;
    }

    std::memcpy(&out, coexistence_dksh, sizeof(out));
    const std::uint64_t total
        = static_cast<std::uint64_t>(out.control_size) + out.code_size;
    const std::uint64_t program_end
        = static_cast<std::uint64_t>(out.programs_offset) + program_header_size;
    const bool valid = out.magic == dksh_magic
        && out.header_size == sizeof(out)
        && out.control_size % DK_SHADER_CODE_ALIGNMENT == 0
        && out.code_size % DK_SHADER_CODE_ALIGNMENT == 0
        && total == coexistence_dksh_size
        && out.program_count == 1
        && out.programs_offset >= sizeof(out)
        && program_end <= out.control_size
        && read_u32(out.programs_offset + program_type_offset) == compute_program_type;
    if (valid) {
        per_warp_scratch = read_u32(out.programs_offset + per_warp_scratch_offset);
    }
    return valid;
}

[[nodiscard]] std::uint32_t code_memory_size(const DkshHeader& header) noexcept {
    const std::uint64_t needed
        = static_cast<std::uint64_t>(header.code_size) + DK_SHADER_CODE_UNUSABLE_SIZE;
    const std::uint64_t rounded
        = (needed + DK_MEMBLOCK_ALIGNMENT - 1U) & ~(DK_MEMBLOCK_ALIGNMENT - 1U);
    return rounded <= UINT32_MAX ? static_cast<std::uint32_t>(rounded) : 0;
}

} // namespace

const char* stage_name(const Stage stage) noexcept {
    switch (stage) {
    case Stage::not_started: return "not_started";
    case Stage::nvdrv_session: return "nvdrv_session";
    case Stage::device: return "device";
    case Stage::queue: return "queue";
    case Stage::code_memory: return "code_memory";
    case Stage::shader: return "shader";
    case Stage::result_memory: return "result_memory";
    case Stage::command_memory: return "command_memory";
    case Stage::command_buffer: return "command_buffer";
    case Stage::command_list: return "command_list";
    case Stage::submitted: return "submitted";
    case Stage::waiting: return "waiting";
    case Stage::timed_out: return "timed_out";
    case Stage::wait_failed: return "wait_failed";
    case Stage::completed: return "completed";
    case Stage::cleanup: return "cleanup";
    case Stage::verified: return "verified";
    }
    return "unknown";
}

Result run(const ProgressCallback progress) noexcept {
    Result result{};
    g_arena.used = 0;
    g_completion_fence = {};

    DkDevice device{};
    DkQueue queue{};
    DkMemBlock code_memory{};
    DkMemBlock result_memory{};
    DkMemBlock command_memory{};
    DkCmdBuf commands{};
    DkshHeader header{};
    DkShader shader{};
    std::uint32_t code_bytes{};
    std::uint32_t per_warp_scratch{};
    std::uint32_t* output{};
    DkResult wait_result{DkResult_Fail};
    const DkShader* shader_pointer = &shader;

    if (!read_shader_header(header, per_warp_scratch)) {
        goto done;
    }
    code_bytes = code_memory_size(header);
    if (code_bytes == 0) {
        goto done;
    }

    DkDeviceMaker device_maker;
    dkDeviceMakerDefaults(&device_maker);
    device_maker.userData = &g_arena;
    device_maker.cbAlloc = &allocate;
    device_maker.cbFree = &release;

    result.stage = Stage::nvdrv_session;
    notify(progress, result.stage);
    if (!nv_session::captured()) {
        result.value = static_cast<std::uint32_t>(nv_session::capture_result());
        goto done;
    }

    result.stage = Stage::device;
    notify(progress, result.stage);
    device = dkDeviceCreate(&device_maker);
    if (device == nullptr) {
        goto done;
    }

    result.stage = Stage::queue;
    notify(progress, result.stage);
    {
        DkQueueMaker maker;
        dkQueueMakerDefaults(&maker, device);
        maker.flags
            = DkQueueFlags_Compute | DkQueueFlags_LowPrio | DkQueueFlags_DisableZcull;
        maker.commandMemorySize = DK_QUEUE_MIN_CMDMEM_SIZE;
        maker.flushThreshold = DK_MEMBLOCK_ALIGNMENT;
        maker.perWarpScratchMemorySize = per_warp_scratch;
        maker.maxConcurrentComputeJobs = 1;
        queue = dkQueueCreate(&maker);
    }
    if (queue == nullptr) {
        goto done;
    }

    result.stage = Stage::code_memory;
    {
        DkMemBlockMaker maker;
        dkMemBlockMakerDefaults(&maker, device, code_bytes);
        maker.flags
            = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code;
        code_memory = dkMemBlockCreate(&maker);
    }
    if (code_memory == nullptr) {
        goto done;
    }
    std::memcpy(dkMemBlockGetCpuAddr(code_memory),
        coexistence_dksh + header.control_size,
        header.code_size);

    result.stage = Stage::shader;
    {
        DkShaderMaker maker;
        dkShaderMakerDefaults(&maker, code_memory, 0);
        maker.control = coexistence_dksh;
        dkShaderInitialize(&shader, &maker);
    }
    if (!dkShaderIsValid(&shader) || dkShaderGetStage(&shader) != DkStage_Compute) {
        goto done;
    }

    result.stage = Stage::result_memory;
    {
        DkMemBlockMaker maker;
        dkMemBlockMakerDefaults(&maker, device, DK_MEMBLOCK_ALIGNMENT);
        maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuUncached;
        result_memory = dkMemBlockCreate(&maker);
    }
    if (result_memory == nullptr) {
        goto done;
    }
    output = static_cast<std::uint32_t*>(dkMemBlockGetCpuAddr(result_memory));
    if (output == nullptr) {
        goto done;
    }
    *output = 0;

    result.stage = Stage::command_memory;
    {
        DkMemBlockMaker maker;
        dkMemBlockMakerDefaults(&maker, device, command_memory_size);
        maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
        command_memory = dkMemBlockCreate(&maker);
    }
    if (command_memory == nullptr) {
        goto done;
    }

    result.stage = Stage::command_buffer;
    {
        DkCmdBufMaker maker;
        dkCmdBufMakerDefaults(&maker, device);
        commands = dkCmdBufCreate(&maker);
    }
    if (commands == nullptr) {
        goto done;
    }

    dkCmdBufAddMemory(commands, command_memory, 0, command_memory_size);
    dkCmdBufBindShaders(commands, DkStageFlag_Compute, &shader_pointer, 1);
    dkCmdBufBindStorageBuffer(commands,
        DkStage_Compute,
        0,
        dkMemBlockGetGpuAddr(result_memory),
        sizeof(*output));
    dkCmdBufDispatchCompute(commands, 1, 1, 1);

    {
        result.stage = Stage::command_list;
        const DkCmdList list = dkCmdBufFinishList(commands);
        if (list == 0) {
            goto done;
        }
        dkQueueSubmitCommands(queue, list);
    }
    result.stage = Stage::submitted;
    notify(progress, result.stage);
    dkQueueSignalFence(queue, &g_completion_fence, true);
    dkQueueFlush(queue);
    result.stage = Stage::waiting;
    notify(progress, result.stage);
    wait_result = dkFenceWait(&g_completion_fence, completion_timeout_ns);
    if (wait_result != DkResult_Success) {
        result.stage = wait_result == DkResult_Timeout ? Stage::timed_out : Stage::wait_failed;
        notify(progress, result.stage);
        result.arena_bytes = g_arena.used;

        // The fence address and every submitted resource must remain alive if
        // the GPU did not confirm completion.
        return result;
    }
    if (dkQueueIsInErrorState(queue)) {
        goto done;
    }
    result.stage = Stage::completed;
    result.value = *output;
    result.passed = result.value == expected_value;
    if (result.passed) {
        result.stage = Stage::verified;
    }

done:
    result.arena_bytes = g_arena.used;
    notify(progress, Stage::cleanup);
    if (commands != nullptr) {
        dkCmdBufDestroy(commands);
    }
    if (command_memory != nullptr) {
        dkMemBlockDestroy(command_memory);
    }
    if (result_memory != nullptr) {
        dkMemBlockDestroy(result_memory);
    }
    if (code_memory != nullptr) {
        dkMemBlockDestroy(code_memory);
    }
    if (queue != nullptr) {
        dkQueueDestroy(queue);
    }
    if (device != nullptr) {
        dkDeviceDestroy(device);
    }
    return result;
}

} // namespace lsfg::plugin::coexistence
