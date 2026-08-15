// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "deferred.hpp"

#include "imports.hpp"

#include <switch.h>

#include <saltysd_core.h>
#include <saltysd_dynamic.h>
#include <useful.h>

#include <array>
#include <atomic>
#include <cstdint>

extern "C" {

void lsfg_plugin_deferred_thunk_0();

void* lsfg_plugin_deferred_enter(std::uint32_t index);

} // extern "C"

namespace lsfg::plugin::deferred {
namespace {

struct Trigger {
    const char* symbol;
    void (*thunk)();
    imports::Sites sites{};
    void* original{};
};

std::array<Trigger, 1> g_triggers{
    Trigger{"_ZN2nn2vi10InitializeEv", &lsfg_plugin_deferred_thunk_0},
};

std::atomic<bool> g_fired{false};
Callback g_callback{nullptr};

} // namespace

bool arm(const Callback callback) noexcept {
    g_callback = callback;

    bool armed = false;
    for (Trigger& trigger : g_triggers) {
        trigger.sites = imports::find(trigger.symbol);
        if (trigger.sites.empty()) {
            SaltySDCore_printf("lsfg-nx: %s is not imported here\n", trigger.symbol);
            continue;
        }
        if (trigger.sites.overflow != 0) {
            SaltySDCore_printf(
                "lsfg-nx: %s has too many import sites, not arming\n",
                trigger.symbol);
            trigger.sites = imports::Sites{};
            continue;
        }

        void* const slot = imports::current_target(trigger.sites);

        trigger.original
            = reinterpret_cast<void*>(SaltySDCore_FindSymbolBuiltin(trigger.symbol));
        if (trigger.original == nullptr) {
            SaltySDCore_printf("lsfg-nx: %s has no address to chain to\n", trigger.symbol);
            trigger.sites = imports::Sites{};
            continue;
        }

        if (!imports::replace(
                trigger.sites,
                trigger.original,
                reinterpret_cast<void*>(trigger.thunk))) {
            SaltySDCore_printf("lsfg-nx: %s relocation rewrite failed\n", trigger.symbol);
            trigger.sites = imports::Sites{};
            continue;
        }

        SaltySDCore_printf(
            "lsfg-nx: armed on %s across %u import sites (%u glob_dat, %u jump_slot), "
            "%u other references ignored, "
            "slot 0x%lx, chaining to 0x%lx\n",
            trigger.symbol,
            static_cast<unsigned>(trigger.sites.count),
            static_cast<unsigned>(trigger.sites.glob_dat),
            static_cast<unsigned>(trigger.sites.jump_slot),
            static_cast<unsigned>(trigger.sites.other_kind),
            reinterpret_cast<std::uintptr_t>(slot),
            reinterpret_cast<std::uintptr_t>(trigger.original));
        armed = true;
    }

    return armed;
}

bool fired() noexcept {
    return g_fired.load(std::memory_order_acquire);
}

extern "C" void* lsfg_plugin_deferred_enter(const std::uint32_t index) {
    bool expected = false;
    if (g_fired.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        for (Trigger& trigger : g_triggers) {
            if (!trigger.sites.empty()) {
                imports::redirect(trigger.sites, trigger.original);
            }
        }

        if (g_callback != nullptr) {
            g_callback();
        }
    }

    return g_triggers[index].original;
}

} // namespace lsfg::plugin::deferred
