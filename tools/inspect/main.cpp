// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

// Host-side counterpart of the preparation app's extraction stage. It reads a
// DLL and reports what the preparation app would find in it.

#include <lsfg/common/cache_format.hpp>
#include <lsfg/common/dksh.hpp>
#include <lsfg/common/pe_resources.hpp>
#include <lsfg/common/sha256.hpp>
#include <lsfg/common/shader_set.hpp>
#include <lsfg/common/spirv_module.hpp>
#include <lsfg/common/translate.hpp>
#include <lsfg/common/version.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::filesystem::path dll;
    std::filesystem::path dump_directory;
    std::filesystem::path glsl_directory;
    bool list_all{};
    bool translate{};
    bool valid{};
};

Options parse_arguments(const std::span<const std::string_view> arguments) {
    Options options;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
        if (argument == "--all") {
            options.list_all = true;
        } else if (argument == "--translate") {
            options.translate = true;
        } else if (argument == "--dump" && index + 1U < arguments.size()) {
            options.dump_directory = arguments[++index];
        } else if (argument == "--glsl" && index + 1U < arguments.size()) {
            options.glsl_directory = arguments[++index];
            options.translate = true;
        } else if (argument.starts_with("--")) {
            return options;
        } else if (options.dll.empty()) {
            options.dll = argument;
        } else {
            return options;
        }
    }

    options.valid = !options.dll.empty();
    return options;
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }

    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size <= 0) {
        return {};
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!file) {
        return {};
    }
    return bytes;
}

void print_module(
    const lsfg::spirv::Inventory& inventory,
    const std::uint32_t resource_id,
    const std::string_view name,
    const std::size_t size) {
    const lsfg::spirv::DescriptorCounts counts = lsfg::spirv::count_descriptors(inventory);

    std::cout << "  " << std::setw(14) << std::left << name << std::right
              << " id " << std::setw(4) << resource_id
              << "  " << std::setw(7) << size << " B"
              << "  local " << inventory.local_size_x << 'x' << inventory.local_size_y << 'x'
              << inventory.local_size_z
              << "  tex " << std::setw(2) << (counts.sampled_images + counts.separate_images)
              << "  img " << std::setw(2) << counts.storage_images
              << "  smp " << std::setw(2) << counts.samplers
              << "  ubo " << std::setw(2) << counts.uniform_buffers
              << "  ssbo " << std::setw(2) << counts.storage_buffers
              << "  set " << counts.highest_set
              << "  bind " << std::setw(2) << counts.highest_binding << '\n';
}

void print_bindings(const lsfg::spirv::Inventory& inventory) {
    for (const lsfg::spirv::Binding& binding : inventory.bindings) {
        std::cout << "      set " << binding.set << " binding " << std::setw(2) << binding.binding
                  << "  " << std::setw(15) << std::left
                  << lsfg::spirv::resource_kind_name(binding.kind) << std::right;
        if (binding.array_size != 1) {
            std::cout << " [" << binding.array_size << ']';
        }
        if (binding.kind == lsfg::spirv::ResourceKind::storage_image
            || binding.kind == lsfg::spirv::ResourceKind::separate_image
            || binding.kind == lsfg::spirv::ResourceKind::sampled_image) {
            std::cout << " format " << lsfg::spirv::image_format_name(binding.image_format) << '('
                      << binding.image_format << ')';
        }
        std::cout << '\n';
    }
}

void print_slots(const lsfg::translate::Module& module) {
    for (const lsfg::translate::SlotAssignment& slot : module.slots) {
        std::cout << "        " << std::setw(14) << std::left
                  << lsfg::translate::slot_kind_name(slot.kind) << std::right << std::setw(3)
                  << slot.slot << "  from binding " << std::setw(3) << slot.spirv_binding;
        if (slot.kind == lsfg::translate::SlotKind::texture) {
            std::cout << " with sampler " << std::setw(3) << slot.spirv_sampler_binding;
            if (slot.uses_dummy_sampler) {
                std::cout << " (introduced)";
            }
        }
        std::cout << '\n';
    }
}

int report(const Options& options) {
    const std::vector<std::uint8_t> image = read_file(options.dll);
    if (image.empty()) {
        std::cerr << "cannot read " << options.dll << '\n';
        return EXIT_FAILURE;
    }

    const lsfg::Digest digest = lsfg::sha256(image);
    std::cout << "file      " << options.dll.string() << '\n'
              << "size      " << image.size() << " bytes\n"
              << "sha256    " << lsfg::to_hex(digest).data() << '\n';

    const lsfg::Digest key = lsfg::cache::cache_key(lsfg::cache::CacheKeyInputs{
        .dll_bytes = image,
        .extractor_version = lsfg::cache::extractor_version,
        .spirv_cross_revision = lsfg::version::spirv_cross_revision,
        .uam_revision = lsfg::version::uam_revision,
        .translation_options = 0,
        .backend_abi_version = lsfg::cache::abi_version,
    });
    std::cout << "cache key " << lsfg::to_hex(key).data() << "\n\n";

    lsfg::pe::ResourceTable table;
    if (const lsfg::ErrorCode result = lsfg::pe::enumerate_resources(image, table);
        !lsfg::succeeded(result)) {
        std::cerr << "resource enumeration failed: " << lsfg::error_name(result) << '\n';
        return EXIT_FAILURE;
    }

    std::size_t spirv_count = 0;
    std::size_t rcdata_bytes = 0;
    for (const lsfg::pe::Resource& resource : table.resources) {
        if (resource.type != lsfg::pe::resource_type_rcdata) {
            continue;
        }
        rcdata_bytes += resource.size;
        if (lsfg::spirv::is_spirv(lsfg::pe::resource_data(image, resource))) {
            ++spirv_count;
        }
    }

    std::cout << "resources " << table.resources.size() << " total, " << spirv_count
              << " SPIR-V modules, " << rcdata_bytes << " bytes of RCDATA\n";
    if (table.named_entries_skipped != 0) {
        std::cout << "          " << table.named_entries_skipped << " name-keyed entries skipped\n";
    }

    lsfg::shaders::ShaderSet set;
    if (const lsfg::ErrorCode result = lsfg::shaders::identify(image, table.resources, set);
        !lsfg::succeeded(result)) {
        std::cerr << "shader set not recognised: " << lsfg::error_name(result) << '\n';
        return EXIT_FAILURE;
    }

    const std::uint32_t high_base
        = set.first_resource_id + (set.high_precision_block * set.block_size);
    const std::uint32_t low_base = set.first_resource_id + (set.low_precision_block * set.block_size);
    std::cout << "set       " << 2U * set.block_size << " modules in two blocks of " << set.block_size
              << ", ids " << set.first_resource_id << '-'
              << set.first_resource_id + (2U * set.block_size) - 1U << '\n'
              << "          low precision  base id " << low_base << '\n'
              << "          high precision base id " << high_base << "\n\n";

    const auto find_resource = [&](const std::uint32_t id) -> const lsfg::pe::Resource* {
        for (const lsfg::pe::Resource& resource : table.resources) {
            if (resource.type == lsfg::pe::resource_type_rcdata && resource.id == id) {
                return &resource;
            }
        }
        return nullptr;
    };

    std::uint32_t paired_sizes = 0;
    for (std::uint32_t index = 0; index < set.block_size; ++index) {
        const lsfg::pe::Resource* const low
            = find_resource(lsfg::shaders::resource_id_for(set, lsfg::shaders::Precision::low, index));
        const lsfg::pe::Resource* const high
            = find_resource(lsfg::shaders::resource_id_for(set, lsfg::shaders::Precision::high, index));
        if (low != nullptr && high != nullptr && low->size == high->size) {
            ++paired_sizes;
        }
    }
    std::cout << "          " << paired_sizes << " of " << set.block_size
              << " block positions hold equally sized modules in both blocks\n\n";

    std::vector<lsfg::shaders::ModuleRequest> requests;
    if (const lsfg::ErrorCode result
        = lsfg::shaders::required_modules(set, lsfg::shaders::Precision::high, false, requests);
        !lsfg::succeeded(result)) {
        std::cerr << "chain does not fit the detected set: " << lsfg::error_name(result) << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "chain     " << lsfg::shaders::chain_slots().size() << " dispatch slots use "
              << requests.size() << " distinct modules\n\n";

    lsfg::spirv::DescriptorCounts worst{};
    std::vector<std::uint32_t> unique_capabilities;
    bool any_specialised_workgroup = false;

    lsfg::translate::Module worst_translation;
    std::uint32_t modules_translated = 0;
    std::uint32_t modules_needing_a_dummy_sampler = 0;
    std::uint32_t storage_images_formatted = 0;
    std::size_t glsl_bytes = 0;

    std::uint32_t modules_compiled = 0;
    std::size_t dksh_bytes = 0;
    std::uint32_t worst_gprs = 0;
    std::uint32_t worst_scratch = 0;
    std::uint32_t worst_shared_memory = 0;

    const auto account = [&](const lsfg::spirv::Inventory& inventory) {
        const lsfg::spirv::DescriptorCounts counts = lsfg::spirv::count_descriptors(inventory);
        worst.samplers = std::max(worst.samplers, counts.samplers);
        worst.sampled_images = std::max(worst.sampled_images, counts.sampled_images);
        worst.separate_images = std::max(worst.separate_images, counts.separate_images);
        worst.storage_images = std::max(worst.storage_images, counts.storage_images);
        worst.uniform_buffers = std::max(worst.uniform_buffers, counts.uniform_buffers);
        worst.storage_buffers = std::max(worst.storage_buffers, counts.storage_buffers);
        worst.highest_set = std::max(worst.highest_set, counts.highest_set);
        worst.highest_binding = std::max(worst.highest_binding, counts.highest_binding);

        any_specialised_workgroup = any_specialised_workgroup || inventory.local_size_is_specialised;

        for (const std::uint32_t capability : inventory.capabilities) {
            if (std::ranges::find(unique_capabilities, capability) == unique_capabilities.end()) {
                unique_capabilities.push_back(capability);
            }
        }
    };

    std::cout << "high-precision modules the chain needs:\n";
    for (const lsfg::shaders::ModuleRequest& request : requests) {
        const lsfg::pe::Resource* const resource = find_resource(request.resource_id);
        if (resource == nullptr) {
            std::cerr << "missing resource " << request.resource_id << '\n';
            return EXIT_FAILURE;
        }

        const std::span<const std::uint8_t> data = lsfg::pe::resource_data(image, *resource);
        lsfg::spirv::Inventory inventory;
        if (const lsfg::ErrorCode result = lsfg::spirv::inspect_bytes(data, inventory);
            !lsfg::succeeded(result)) {
            std::cerr << "module " << request.resource_id << " rejected: " << lsfg::error_name(result)
                      << '\n';
            return EXIT_FAILURE;
        }

        print_module(inventory, request.resource_id, request.name, data.size());
        if (options.list_all) {
            print_bindings(inventory);
        }
        account(inventory);

        if (options.translate) {
            lsfg::translate::Module module;
            if (const lsfg::ErrorCode result
                = lsfg::translate::to_glsl(data, lsfg::translate::Options{}, module);
                !lsfg::succeeded(result)) {
                std::cerr << "module " << request.resource_id
                          << " did not translate: " << lsfg::error_name(result) << '\n';
                return EXIT_FAILURE;
            }

            ++modules_translated;
            glsl_bytes += module.glsl.size();
            modules_needing_a_dummy_sampler += module.needed_dummy_sampler ? 1U : 0U;
            storage_images_formatted += module.patch.images_formatted;

            worst_translation.texture_count
                = std::max(worst_translation.texture_count, module.texture_count);
            worst_translation.storage_image_count
                = std::max(worst_translation.storage_image_count, module.storage_image_count);
            worst_translation.uniform_buffer_count
                = std::max(worst_translation.uniform_buffer_count, module.uniform_buffer_count);

            std::cout << "        " << module.glsl.size() << " bytes of GLSL, "
                      << module.texture_count << " textures, " << module.storage_image_count
                      << " storage images, " << module.uniform_buffer_count << " uniform buffers\n";
            if (options.list_all) {
                print_slots(module);
            }

            if (!options.glsl_directory.empty()) {
                std::filesystem::create_directories(options.glsl_directory);
                std::ofstream out(options.glsl_directory / (std::string{request.name} + ".comp"));
                out << module.glsl;
            }

            if (lsfg::dksh::compiler_available()) {
                lsfg::dksh::Blob blob;
                const lsfg::ErrorCode result = lsfg::dksh::compile(module.glsl, blob);
                if (!lsfg::succeeded(result)) {
                    std::cerr << "module " << request.resource_id
                              << " did not compile: " << lsfg::error_name(result) << '\n'
                              << blob.log << '\n';
                    return EXIT_FAILURE;
                }

                // The workgroup size has to survive SPIR-V, GLSL, and DKSH
                // unchanged, else every dispatch this module takes part in is
                // sized wrongly.
                if (blob.program.block_dim_x != module.local_size_x
                    || blob.program.block_dim_y != module.local_size_y
                    || blob.program.block_dim_z != module.local_size_z) {
                    std::cerr << "module " << request.resource_id
                              << " changed workgroup size in translation\n";
                    return EXIT_FAILURE;
                }

                ++modules_compiled;
                dksh_bytes += blob.bytes.size();
                worst_gprs = std::max(worst_gprs, blob.program.gprs);
                worst_scratch = std::max(worst_scratch, blob.program.per_warp_scratch_bytes);
                worst_shared_memory
                    = std::max(worst_shared_memory, blob.program.shared_memory_bytes);

                std::cout << "        " << blob.bytes.size() << " bytes of DKSH, "
                          << blob.program.gprs << " registers, "
                          << blob.program.per_warp_scratch_bytes << " B scratch per warp\n";
                if (!blob.log.empty()) {
                    std::cout << blob.log;
                }
            }
        }

        if (!options.dump_directory.empty()) {
            std::filesystem::create_directories(options.dump_directory);
            const std::filesystem::path path
                = options.dump_directory / (std::string{request.name} + ".spv");
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        }
    }

    std::cout << "\nworst case across the chain:\n"
              << "  sampled images   " << worst.sampled_images << '\n'
              << "  separate images  " << worst.separate_images << '\n'
              << "  storage images   " << worst.storage_images << '\n'
              << "  samplers         " << worst.samplers << '\n'
              << "  uniform buffers  " << worst.uniform_buffers << '\n'
              << "  storage buffers  " << worst.storage_buffers << '\n'
              << "  highest set      " << worst.highest_set << '\n'
              << "  highest binding  " << worst.highest_binding << '\n'
              << "  specialised workgroup size " << (any_specialised_workgroup ? "yes" : "no") << '\n';

    if (options.translate) {
        const lsfg::translate::Limits limits = lsfg::translate::Options{}.limits;
        std::cout << "\ntranslation to GLSL:\n"
                  << "  modules          " << modules_translated << " of " << requests.size() << '\n'
                  << "  total GLSL       " << glsl_bytes << " bytes\n"
                  << "  textures         " << worst_translation.texture_count << " of "
                  << limits.textures << '\n'
                  << "  storage images   " << worst_translation.storage_image_count << " of "
                  << limits.storage_images << '\n'
                  << "  uniform buffers  " << worst_translation.uniform_buffer_count << " of "
                  << limits.uniform_buffers << '\n'
                  << "  storage images given a format  " << storage_images_formatted << '\n'
                  << "  modules needing a sampler for an unsampled image  "
                  << modules_needing_a_dummy_sampler << '\n';

        if (lsfg::dksh::compiler_available()) {
            std::cout << "\ncompilation to DKSH:\n"
                      << "  modules          " << modules_compiled << " of " << requests.size()
                      << '\n'
                      << "  total DKSH       " << dksh_bytes << " bytes\n"
                      << "  most registers   " << worst_gprs << '\n'
                      << "  most scratch     " << worst_scratch << " bytes per warp\n"
                      << "  most shared mem  " << worst_shared_memory << " bytes\n";
        } else {
            std::cout << "\nthis build has no GLSL to DKSH compiler."
                         " Configure with -DLSFG_BUILD_UAM=ON to add it\n";
        }
    }

    std::ranges::sort(unique_capabilities);
    std::cout << "\ncapabilities declared anywhere in the chain:\n";
    for (const std::uint32_t capability : unique_capabilities) {
        std::cout << "  " << std::setw(5) << capability << "  "
                  << lsfg::spirv::capability_name(capability) << '\n';
    }

    if (!options.dump_directory.empty()) {
        std::cout << "\nmodules written to " << options.dump_directory.string()
                  << ". These are extracted from a proprietary DLL and must not be published\n";
    }

    return EXIT_SUCCESS;
}

} // namespace

int main(const int argc, const char* const* const argv) {
    std::vector<std::string_view> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    const Options options = parse_arguments(arguments);
    if (!options.valid) {
        std::cerr << "lsfg-inspect " << lsfg::version::git_revision << '\n'
                  << "usage: lsfg-inspect <Lossless.dll> [--all] [--translate]\n"
                     "                    [--dump <directory>] [--glsl <directory>]\n";
        return EXIT_FAILURE;
    }

    return report(options);
}
