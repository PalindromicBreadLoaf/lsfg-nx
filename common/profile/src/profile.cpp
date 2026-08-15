// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/common/profile.hpp>

#include <algorithm>
#include <cstring>

namespace lsfg::profile {
namespace {

enum class Section : std::uint8_t {
    none,
    title,
    builds,
    presentation,
    ignored,
};

[[nodiscard]] constexpr bool is_space(const char character) noexcept {
    return character == ' ' || character == '\t' || character == '\r' || character == '\f'
        || character == '\v';
}

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() && is_space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_space(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

[[nodiscard]] bool parse_hex_id(const std::string_view text, std::uint64_t& out) noexcept {
    if (text.size() != 16) {
        return false;
    }

    std::uint64_t value = 0;
    for (const char character : text) {
        std::uint64_t digit = 0;
        if (character >= '0' && character <= '9') {
            digit = static_cast<std::uint64_t>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            digit = static_cast<std::uint64_t>(character - 'a') + 10U;
        } else if (character >= 'A' && character <= 'F') {
            digit = static_cast<std::uint64_t>(character - 'A') + 10U;
        } else {
            return false;
        }
        value = (value << 4U) | digit;
    }

    out = value;
    return true;
}

[[nodiscard]] bool parse_number(std::string_view text, const std::uint32_t limit,
    std::uint32_t& out) noexcept {
    std::uint64_t base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.remove_prefix(2);
    }
    if (text.empty()) {
        return false;
    }

    std::uint64_t value = 0;
    for (const char character : text) {
        std::uint64_t digit = 0;
        if (character >= '0' && character <= '9') {
            digit = static_cast<std::uint64_t>(character - '0');
        } else if (base == 16 && character >= 'a' && character <= 'f') {
            digit = static_cast<std::uint64_t>(character - 'a') + 10U;
        } else if (base == 16 && character >= 'A' && character <= 'F') {
            digit = static_cast<std::uint64_t>(character - 'A') + 10U;
        } else {
            return false;
        }

        value = (value * base) + digit;
        if (value > limit) {
            return false;
        }
    }

    out = static_cast<std::uint32_t>(value);
    return true;
}

[[nodiscard]] bool parse_boolean(const std::string_view text, bool& out) noexcept {
    if (text == "true" || text == "yes" || text == "1") {
        out = true;
        return true;
    }
    if (text == "false" || text == "no" || text == "0") {
        out = false;
        return true;
    }
    return false;
}

[[nodiscard]] Section section_for(const std::string_view name) noexcept {
    if (name == "title") {
        return Section::title;
    }
    if (name == "builds") {
        return Section::builds;
    }
    if (name == "presentation") {
        return Section::presentation;
    }
    return Section::ignored;
}

struct Seen {
    bool name{};
    bool title_id{};
    bool supported{};
};

[[nodiscard]] ErrorCode apply_title(
    const std::string_view key, const std::string_view value, Profile& out, Seen& seen) noexcept {
    if (key == "name") {
        if (seen.name) {
            return ErrorCode::invalid_argument;
        }
        seen.name = true;
        const std::size_t size = std::min(value.size(), name_capacity - 1);
        std::memcpy(out.name.data(), value.data(), size);
        out.name_size = static_cast<std::uint8_t>(size);
        return ErrorCode::ok;
    }
    if (key == "title_id") {
        if (seen.title_id || !parse_hex_id(value, out.title_id)) {
            return ErrorCode::invalid_argument;
        }
        seen.title_id = true;
        return ErrorCode::ok;
    }
    if (key == "supported") {
        if (seen.supported || !parse_boolean(value, out.supported)) {
            return ErrorCode::invalid_argument;
        }
        seen.supported = true;
        return ErrorCode::ok;
    }
    return ErrorCode::ok;
}

[[nodiscard]] ErrorCode apply_build(
    const std::string_view key, const std::string_view value, Profile& out) noexcept {
    if (key.empty() || key.size() > version_capacity) {
        return ErrorCode::invalid_argument;
    }

    std::uint64_t build_id = 0;
    if (!parse_hex_id(value, build_id)) {
        return ErrorCode::invalid_argument;
    }

    for (std::size_t index = 0; index < out.build_count; ++index) {
        const BuildEntry& entry = out.builds[index];
        if (entry.version_view() == key || entry.build_id == build_id) {
            return ErrorCode::invalid_argument;
        }
    }

    if (out.build_count >= max_builds) {
        return ErrorCode::invalid_argument;
    }

    BuildEntry& entry = out.builds[out.build_count];
    std::memcpy(entry.version.data(), key.data(), key.size());
    entry.version_size = static_cast<std::uint8_t>(key.size());
    entry.build_id = build_id;
    ++out.build_count;
    return ErrorCode::ok;
}

[[nodiscard]] ErrorCode apply_presentation(
    const std::string_view key, const std::string_view value, Presentation& out) noexcept {
    std::uint32_t number = 0;

    if (key == "present_interval" || key == "swapchain_buffers") {
        if (!parse_number(value, 0xffU, number)) {
            return ErrorCode::invalid_argument;
        }
        if (key == "present_interval") {
            out.present_interval = static_cast<std::uint8_t>(number);
        } else {
            out.swapchain_buffers = static_cast<std::uint8_t>(number);
        }
        return ErrorCode::ok;
    }

    if (key != "texture_format" && key != "handheld_width" && key != "handheld_height") {
        return ErrorCode::ok;
    }

    if (!parse_number(value, 0xffff'ffffU, number)) {
        return ErrorCode::invalid_argument;
    }
    if (key == "texture_format") {
        out.texture_format = number;
    } else if (key == "handheld_width") {
        out.handheld_width = number;
    } else {
        out.handheld_height = number;
    }
    return ErrorCode::ok;
}

} // namespace

const BuildEntry* Profile::find_build(const std::uint64_t build_id) const noexcept {
    for (std::size_t index = 0; index < build_count; ++index) {
        if (builds[index].build_id == build_id) {
            return &builds[index];
        }
    }
    return nullptr;
}

ErrorCode parse(const std::string_view text, Profile& out) noexcept {
    if (text.size() > max_text_size) {
        return ErrorCode::invalid_argument;
    }

    out = Profile{};

    Section section = Section::none;
    Seen seen{};

    std::size_t offset = 0;
    while (offset <= text.size()) {
        const std::size_t end = std::min(text.find('\n', offset), text.size());
        const std::string_view line = trim(text.substr(offset, end - offset));
        offset = end + 1;

        if (line.empty() || line.front() == ';' || line.front() == '#') {
            continue;
        }

        if (line.front() == '[') {
            if (line.back() != ']') {
                return ErrorCode::invalid_argument;
            }
            section = section_for(trim(line.substr(1, line.size() - 2)));
            continue;
        }

        const std::size_t separator = line.find('=');
        if (separator == std::string_view::npos) {
            return ErrorCode::invalid_argument;
        }

        const std::string_view key = trim(line.substr(0, separator));
        const std::string_view value = trim(line.substr(separator + 1));
        if (key.empty()) {
            return ErrorCode::invalid_argument;
        }

        ErrorCode result = ErrorCode::ok;
        switch (section) {
        case Section::title:
            result = apply_title(key, value, out, seen);
            break;
        case Section::builds:
            result = apply_build(key, value, out);
            break;
        case Section::presentation:
            result = apply_presentation(key, value, out.presentation);
            break;
        case Section::none:
            result = ErrorCode::invalid_argument;
            break;
        case Section::ignored:
            break;
        }

        if (!succeeded(result)) {
            return result;
        }
    }

    if (!seen.title_id) {
        return ErrorCode::invalid_argument;
    }
    return ErrorCode::ok;
}

ErrorCode check(const Profile& profile, const std::uint64_t title_id,
    const std::uint64_t build_id, const Targeting targeting) noexcept {
    if (profile.title_id == 0 || profile.title_id != title_id) {
        return ErrorCode::title_not_allowed;
    }
    if (targeting == Targeting::strict && profile.find_build(build_id) == nullptr) {
        return ErrorCode::build_not_allowed;
    }
    return ErrorCode::ok;
}

bool path_for(const std::string_view root, const std::uint64_t title_id,
    std::array<char, 128>& out) noexcept {
    constexpr std::string_view leaf = "/profile.ini";
    std::array<char, 17> identifier{};
    format_id(title_id, identifier);

    const std::size_t size = root.size() + 1U + 16U + leaf.size();
    if (size + 1U > out.size()) {
        return false;
    }

    char* cursor = out.data();
    std::memcpy(cursor, root.data(), root.size());
    cursor += root.size();
    *cursor++ = '/';
    std::memcpy(cursor, identifier.data(), 16);
    cursor += 16;
    std::memcpy(cursor, leaf.data(), leaf.size());
    cursor += leaf.size();
    *cursor = '\0';
    return true;
}

void format_id(const std::uint64_t value, std::array<char, 17>& out) noexcept {
    constexpr std::string_view digits = "0123456789ABCDEF";
    for (std::size_t index = 0; index < 16; ++index) {
        const auto shift = static_cast<std::uint64_t>(60U - (index * 4U));
        out[index] = digits[static_cast<std::size_t>((value >> shift) & 0xfU)];
    }
    out[16] = '\0';
}

} // namespace lsfg::profile
