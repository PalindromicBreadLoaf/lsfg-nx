// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "nv_session.hpp"

#include <saltysd_core.h>
#include <saltysd_dynamic.h>
#include <saltysd_ipc.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" {

struct LsfgSdkHandle {
    std::uint32_t value;
};

Result lsfg_plugin_nv_connect_trampoline(LsfgSdkHandle *out, const char *name);

extern std::uint32_t lsfg_plugin_nv_connect_trampoline_instruction[];

extern std::uintptr_t lsfg_plugin_nv_connect_trampoline_return;

Result lsfg_plugin_nv_connect_hook(LsfgSdkHandle *out, const char *name);

} // extern "C"

namespace lsfg::plugin::nv_session {
namespace {

constexpr const char *connect_symbol =
    "_ZN2nn2sf4hipc20ConnectToHipcServiceEPNS_3svc6HandleEPKc";
constexpr const char *application_service = "nvdrv";

std::atomic<Handle> g_session{INVALID_HANDLE};
std::atomic<::Result> g_capture_result{
    MAKERESULT(Module_Libnx, LibnxError_NotInitialized)};
std::atomic<bool> g_armed{false};

struct AbsoluteBranch {
    std::uint32_t load_target;
    std::uint32_t branch_target;
    std::uintptr_t target;
};

static_assert(sizeof(AbsoluteBranch) == 16);

[[nodiscard]] bool
instruction_is_relocatable(const std::uint32_t instruction) noexcept {
    const bool branch_immediate = (instruction & 0x7c00'0000U) == 0x1400'0000U;
    const bool branch_register = (instruction & 0xfe00'0000U) == 0xd600'0000U;
    const bool conditional_branch =
        (instruction & 0xff00'0010U) == 0x5400'0000U;
    const bool compare_branch = (instruction & 0x7e00'0000U) == 0x3400'0000U;
    const bool test_branch = (instruction & 0x7e00'0000U) == 0x3600'0000U;
    const bool address_relative = (instruction & 0x1f00'0000U) == 0x1000'0000U;
    const bool literal_load = (instruction & 0x3b00'0000U) == 0x1800'0000U;
    return !branch_immediate && !branch_register && !conditional_branch &&
           !compare_branch && !test_branch && !address_relative &&
           !literal_load;
}

[[nodiscard]] ::Result write_memory(const std::uintptr_t destination,
                                    const void *const source,
                                    const std::size_t size) noexcept {
    return SaltySD_Memcpy(destination, reinterpret_cast<std::uintptr_t>(source),
                          size);
}

[[nodiscard]] ArmResult arm_failure(const ::Result result) noexcept {
    g_capture_result.store(result, std::memory_order_release);
    return ArmResult{result, false};
}

} // namespace

ArmResult arm() noexcept {
    if (g_armed.load(std::memory_order_acquire)) {
        return ArmResult{0, true};
    }

    const std::uintptr_t target = SaltySDCore_FindSymbolBuiltin(connect_symbol);
    if (target == 0) {
        return arm_failure(MAKERESULT(Module_Libnx, LibnxError_NotFound));
    }

    std::array<std::uint32_t, 4> original{};
    std::memcpy(original.data(), reinterpret_cast<const void *>(target),
                sizeof(original));
    for (const std::uint32_t instruction : original) {
        if (!instruction_is_relocatable(instruction)) {
            return arm_failure(MAKERESULT(Module_Libnx, LibnxError_BadReloc));
        }
    }

    constexpr std::uint32_t load_target = 0x5800'0050U;
    constexpr std::uint32_t branch_target = 0xd61f'0200U;
    const AbsoluteBranch branch{
        load_target,
        branch_target,
        reinterpret_cast<std::uintptr_t>(&lsfg_plugin_nv_connect_hook),
    };

    const std::uintptr_t resume = target + sizeof(original);
    ::Result result =
        write_memory(reinterpret_cast<std::uintptr_t>(
                         &lsfg_plugin_nv_connect_trampoline_instruction),
                     original.data(), sizeof(original));
    if (R_SUCCEEDED(result)) {
        result = write_memory(reinterpret_cast<std::uintptr_t>(
                                  &lsfg_plugin_nv_connect_trampoline_return),
                              &resume, sizeof(resume));
    }
    if (R_SUCCEEDED(result)) {
        result = write_memory(target, &branch, sizeof(branch));
    }
    if (R_FAILED(result)) {
        return arm_failure(result);
    }

    g_armed.store(true, std::memory_order_release);
    return ArmResult{0, true};
}

bool captured() noexcept {
    return g_session.load(std::memory_order_acquire) != INVALID_HANDLE;
}

::Result capture_result() noexcept {
    return g_capture_result.load(std::memory_order_acquire);
}

} // namespace lsfg::plugin::nv_session

extern "C" Result lsfg_plugin_nv_connect_hook(LsfgSdkHandle *const out,
                                              const char *const name) {
    const ::Result result = lsfg_plugin_nv_connect_trampoline(out, name);
    if (R_FAILED(result) || out == nullptr || name == nullptr ||
        std::strcmp(name, lsfg::plugin::nv_session::application_service) != 0) {
        return result;
    }

    Handle expected = INVALID_HANDLE;
    if (!lsfg::plugin::nv_session::g_session.compare_exchange_strong(
            expected, out->value, std::memory_order_acq_rel)) {
        return result;
    }
    lsfg::plugin::nv_session::g_capture_result.store(0,
                                                     std::memory_order_release);
    return result;
}

extern "C" Result lsfg_plugin_borrow_nvdrv_session(Handle *const out) {
    if (out == nullptr) {
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }
    *out = INVALID_HANDLE;

    const Handle session =
        lsfg::plugin::nv_session::g_session.load(std::memory_order_acquire);
    if (session == INVALID_HANDLE) {
        return lsfg::plugin::nv_session::g_capture_result.load(
            std::memory_order_acquire);
    }
    *out = session;
    return 0;
}
