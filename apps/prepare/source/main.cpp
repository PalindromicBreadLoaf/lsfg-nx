// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/common/cache_format.hpp>
#include <lsfg/common/dksh.hpp>
#include <lsfg/common/pe_resources.hpp>
#include <lsfg/common/ring_log.hpp>
#include <lsfg/common/sha256.hpp>
#include <lsfg/common/shader_set.hpp>
#include <lsfg/common/spirv_module.hpp>
#include <lsfg/common/translate.hpp>
#include <lsfg/common/version.hpp>

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

// Everything up to the point where a cache would be written.
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
        .spirv_cross_revision = lsfg::version::spirv_cross_revision,
        .uam_revision = lsfg::version::uam_revision,
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

    std::printf("%zu modules resolved for the chain\n\n", requests.size());
    consoleUpdate(nullptr);

    std::size_t glsl_bytes = 0;
    std::size_t dksh_bytes = 0;
    std::uint32_t worst_gprs = 0;
    std::uint32_t worst_scratch = 0;
    const std::uint64_t started = armGetSystemTick();

    for (const lsfg::shaders::ModuleRequest& request : requests) {
        const lsfg::pe::Resource* const resource = find_module(table, request.resource_id);
        if (resource == nullptr) {
            report_failure(lsfg::ErrorCode::shader_set_unknown, "locating a required module");
            return false;
        }

        const std::span<const std::uint8_t> data = lsfg::pe::resource_data(image, *resource);

        std::printf("%-10.*s", static_cast<int>(request.name.size()), request.name.data());
        consoleUpdate(nullptr);

        lsfg::translate::Module module;
        if (const lsfg::ErrorCode result
            = lsfg::translate::to_glsl(data, lsfg::translate::Options{}, module);
            !lsfg::succeeded(result)) {
            report_failure(result, "translation");
            return false;
        }

        lsfg::dksh::Blob blob;
        if (const lsfg::ErrorCode result = lsfg::dksh::compile(module.glsl, blob);
            !lsfg::succeeded(result)) {
            std::printf("\n%s\n", blob.log.c_str());
            report_failure(result, "compilation");
            return false;
        }

        // A dispatch sized from the manifest is only correct if the workgroup
        // size came through SPIR-V, GLSL, and DKSH unchanged.
        if (blob.program.block_dim_x != module.local_size_x
            || blob.program.block_dim_y != module.local_size_y
            || blob.program.block_dim_z != module.local_size_z) {
            report_failure(lsfg::ErrorCode::shader_interface_mismatch, "workgroup size check");
            return false;
        }

        glsl_bytes += module.glsl.size();
        dksh_bytes += blob.bytes.size();
        worst_gprs = std::max(worst_gprs, blob.program.gprs);
        worst_scratch = std::max(worst_scratch, blob.program.per_warp_scratch_bytes);

        std::printf(
            " %6zu B GLSL  %5zu B DKSH  %3u regs\n",
            module.glsl.size(),
            blob.bytes.size(),
            blob.program.gprs);
        consoleUpdate(nullptr);
    }

    const std::uint64_t elapsed_ms
        = armTicksToNs(armGetSystemTick() - started) / 1'000'000ULL;

    std::printf(
        "\n%zu modules: %zu B of GLSL, %zu B of DKSH in %llu ms\n",
        requests.size(),
        glsl_bytes,
        dksh_bytes,
        static_cast<unsigned long long>(elapsed_ms));
    std::printf("most registers %u, most scratch %u B per warp\n", worst_gprs, worst_scratch);
    message_log.push(armGetSystemTick(), lsfg::LogLevel::info, lsfg::ErrorCode::ok, "chain compiled");

    // TODO: write the DKSH modules and a manifest to the cache directory.
    std::printf("\nNo cache was written.\n");
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
