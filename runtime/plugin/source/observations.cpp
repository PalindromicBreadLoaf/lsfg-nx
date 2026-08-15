// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "observations.hpp"

#include <switch.h>

namespace lsfg::plugin {
namespace {

Observations g_observations{};
std::atomic_flag g_lock{};

} // namespace

Observe::Observe() noexcept {
    while (g_lock.test_and_set(std::memory_order_acquire)) {
        asm volatile("yield" ::: "memory");
    }
}

Observe::~Observe() {
    g_lock.clear(std::memory_order_release);
}

Observations* Observe::operator->() const noexcept {
    return &g_observations;
}

Observations& Observe::operator*() const noexcept {
    return g_observations;
}

std::uint64_t now() noexcept {
    return armGetSystemTick();
}

const Observations& peek() noexcept {
    return g_observations;
}

} // namespace lsfg::plugin
