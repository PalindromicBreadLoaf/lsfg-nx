// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <lsfg/instrument/presentation.hpp>

#include <atomic>
#include <cstdint>

namespace lsfg::plugin {

struct Observations {
    instrument::TextureLog textures;
    instrument::SwapchainMap swapchain;
    instrument::EventTrace trace;
    instrument::PresentTimeline timeline;

    std::uint64_t window_initializations{};
    std::uint64_t swapchain_replacements{};
    std::uint64_t interval_changes{};
    std::uint64_t active_texture_changes{};
    std::uint64_t foreign_window_presents{};
};

// The graphics API is called from several game threads, so an observation is
// taken under one lock rather than field by field.
class Observe {
public:
    Observe() noexcept;
    ~Observe();

    Observe(const Observe&) = delete;
    Observe& operator=(const Observe&) = delete;
    Observe(Observe&&) = delete;
    Observe& operator=(Observe&&) = delete;

    [[nodiscard]] Observations* operator->() const noexcept;
    [[nodiscard]] Observations& operator*() const noexcept;
};

// The system counter, read directly rather than through the game's own tick
// query.
[[nodiscard]] std::uint64_t now() noexcept;

[[nodiscard]] const Observations& peek() noexcept;

} // namespace lsfg::plugin
