// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

// Stands in for the compiler in builds configured without it, so that reading
// and checking a DKSH does not drag in 250 translation units of mesa.

#include <lsfg/common/dksh.hpp>

namespace lsfg::dksh {

bool compiler_available() noexcept {
    return false;
}

ErrorCode compile(std::string_view /*glsl*/, Blob& out) {
    out = Blob{};
    return ErrorCode::unsupported;
}

} // namespace lsfg::dksh
