// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/common/cache_format.hpp>
#include <lsfg/common/pe_resources.hpp>
#include <lsfg/common/ring_log.hpp>
#include <lsfg/common/sha256.hpp>
#include <lsfg/common/shader_set.hpp>
#include <lsfg/common/spirv_module.hpp>

#include <switch.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

namespace {

constexpr const char* dll_path = "sdmc:/switch/lsfg-nx/Lossless.dll";

lsfg::RingLog message_log;

void report_failure(const lsfg::ErrorCode error, const char* const stage) {
    const std::string_view name = lsfg::error_name(error);
    std::printf("\n%s failed: %.*s\n", stage, static_cast<int>(name.size()), name.data());
    message_log.push(armGetSystemTick(), lsfg::LogLevel::error, error, stage);
    consoleUpdate(nullptr);
}

std::vector<std::uint8_t> read_dll() {
    std::FILE* const file = std::fopen(dll_path, "rb");
    if (file == nullptr) {
        return {};
    }

    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(file);
        return {};
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    const std::size_t read = std::fread(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);

    if (read != bytes.size()) {
        return {};
    }
    return bytes;
}

const lsfg::pe::Resource* find_module(
    const lsfg::pe::ResourceTable& table,
    const std::uint32_t resource_id) {
    for (const lsfg::pe::Resource& resource : table.resources) {
        if (resource.type == lsfg::pe::resource_type_rcdata && resource.id == resource_id) {
            return &resource;
        }
    }
    return nullptr;
}

// Everything up to the point where translation and compilation would begin.
bool inspect_dll() {
    const std::vector<std::uint8_t> image = read_dll();
    if (image.empty()) {
        report_failure(lsfg::ErrorCode::io_error, "reading the DLL");
        std::printf("Copy your own Lossless.dll to\n%s\n", dll_path);
        return false;
    }

    std::printf("size      %zu bytes\n", image.size());
    consoleUpdate(nullptr);

    const lsfg::DigestHex hash = lsfg::to_hex(lsfg::sha256(image));
    std::printf("sha256    %s\n", hash.data());

    const lsfg::DigestHex key = lsfg::to_hex(lsfg::cache::cache_key(lsfg::cache::CacheKeyInputs{
        .dll_bytes = image,
        .extractor_version = lsfg::cache::extractor_version,
        .spirv_cross_revision = "",
        .uam_revision = "",
        .translation_options = 0,
        .backend_abi_version = lsfg::cache::abi_version,
    }));
    std::printf("cache key %s\n\n", key.data());
    consoleUpdate(nullptr);

    lsfg::pe::ResourceTable table;
    if (const lsfg::ErrorCode result = lsfg::pe::enumerate_resources(image, table);
        !lsfg::succeeded(result)) {
        report_failure(result, "resource enumeration");
        return false;
    }

    lsfg::shaders::ShaderSet set;
    if (const lsfg::ErrorCode result = lsfg::shaders::identify(image, table.resources, set);
        !lsfg::succeeded(result)) {
        report_failure(result, "shader set identification");
        return false;
    }

    std::printf(
        "%zu resources, %u shaders in two blocks of %u\n",
        table.resources.size(),
        2U * set.block_size,
        set.block_size);
    consoleUpdate(nullptr);

    std::vector<lsfg::shaders::ModuleRequest> requests;
    if (const lsfg::ErrorCode result
        = lsfg::shaders::required_modules(set, lsfg::shaders::Precision::high, false, requests);
        !lsfg::succeeded(result)) {
        report_failure(result, "chain resolution");
        return false;
    }

    std::uint32_t highest_binding = 0;
    std::uint32_t unformatted_storage_images = 0;
    for (const lsfg::shaders::ModuleRequest& request : requests) {
        const lsfg::pe::Resource* const resource = find_module(table, request.resource_id);
        if (resource == nullptr) {
            report_failure(lsfg::ErrorCode::shader_set_unknown, "locating a required module");
            return false;
        }

        lsfg::spirv::Inventory inventory;
        if (const lsfg::ErrorCode result
            = lsfg::spirv::inspect_bytes(lsfg::pe::resource_data(image, *resource), inventory);
            !lsfg::succeeded(result)) {
            report_failure(result, "module inspection");
            return false;
        }

        const lsfg::spirv::DescriptorCounts counts = lsfg::spirv::count_descriptors(inventory);
        highest_binding = std::max(highest_binding, counts.highest_binding);

        for (const lsfg::spirv::Binding& binding : inventory.bindings) {
            if (binding.kind == lsfg::spirv::ResourceKind::storage_image
                && binding.image_format == static_cast<std::uint32_t>(lsfg::spirv::ImageFormat::unknown)) {
                ++unformatted_storage_images;
            }
        }
    }

    std::printf("%zu modules resolved for the chain\n", requests.size());
    std::printf("highest binding slot %u\n", highest_binding);
    std::printf("%u storage images need a format before translation\n", unformatted_storage_images);
    message_log.push(armGetSystemTick(), lsfg::LogLevel::info, lsfg::ErrorCode::ok, "inventory complete");

    std::printf("\nTranslation and compilation are not implemented yet,\n");
    std::printf("so no cache was written.\n");
    return true;
}

} // namespace

int main() {
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    PadState pad{};
    padInitializeDefault(&pad);

    std::printf("LSFG-NX preparation app\n\n");
    std::printf("file      %s\n", dll_path);
    consoleUpdate(nullptr);

    static_cast<void>(inspect_dll());

    std::printf("\nPress + to exit.\n");

    while (appletMainLoop()) {
        padUpdate(&pad);
        if ((padGetButtonsDown(&pad) & HidNpadButton_Plus) != 0U) {
            break;
        }
        consoleUpdate(nullptr);
    }

    consoleExit(nullptr);
    return 0;
}
