// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

// Host-side counterpart of the preparation app. It reads a DLL and reports what
// the preparation app would find in it, and on request produces the same cache.

#include <lsfg/backend/binding.hpp>
#include <lsfg/backend/cache_load.hpp>
#include <lsfg/backend/layout.hpp>
#include <lsfg/backend/schedule.hpp>

#include <lsfg/common/cache_format.hpp>
#include <lsfg/common/cache_store.hpp>
#include <lsfg/common/dksh.hpp>
#include <lsfg/common/image_graph.hpp>
#include <lsfg/common/pe_resources.hpp>
#include <lsfg/common/prepare.hpp>
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
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::filesystem::path dll;
    std::filesystem::path dump_directory;
    std::filesystem::path glsl_directory;
    std::filesystem::path cache_directory;
    std::filesystem::path load_directory;
    lsfg::graph::Extent extent{.width = 1280, .height = 720};
    bool list_all{};
    bool translate{};
    bool performance{};
    bool graph{};
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
        } else if (argument == "--performance") {
            options.performance = true;
        } else if (argument == "--graph") {
            options.graph = true;
        } else if (argument == "--dump" && index + 1U < arguments.size()) {
            options.dump_directory = arguments[++index];
        } else if (argument == "--glsl" && index + 1U < arguments.size()) {
            options.glsl_directory = arguments[++index];
            options.translate = true;
        } else if (argument == "--cache" && index + 1U < arguments.size()) {
            options.cache_directory = arguments[++index];
            options.translate = true;
        } else if (argument == "--load" && index + 1U < arguments.size()) {
            options.load_directory = arguments[++index];
        } else if (argument == "--extent" && index + 1U < arguments.size()) {
            const std::string_view extent = arguments[++index];
            const std::size_t separator = extent.find('x');
            if (separator == std::string_view::npos) {
                return options;
            }
            options.extent.width = static_cast<std::uint32_t>(
                std::strtoul(std::string{extent.substr(0, separator)}.c_str(), nullptr, 10));
            options.extent.height = static_cast<std::uint32_t>(
                std::strtoul(std::string{extent.substr(separator + 1U)}.c_str(), nullptr, 10));
            if (options.extent.width == 0 || options.extent.height == 0) {
                return options;
            }
        } else if (argument.starts_with("--")) {
            return options;
        } else if (options.dll.empty()) {
            options.dll = argument;
        } else {
            return options;
        }
    }

    options.valid = !options.dll.empty() || !options.load_directory.empty();
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

void print_slots(const std::span<const lsfg::cache::SlotEntry> slots) {
    for (const lsfg::cache::SlotEntry& slot : slots) {
        const auto kind = static_cast<lsfg::translate::SlotKind>(slot.kind);
        std::cout << "        " << std::setw(14) << std::left << lsfg::translate::slot_kind_name(kind)
                  << std::right << std::setw(3) << static_cast<int>(slot.slot) << "  resource "
                  << std::setw(3) << static_cast<int>(slot.ordinal);
        if (kind == lsfg::translate::SlotKind::texture) {
            if (slot.sampler_ordinal == lsfg::cache::introduced_sampler) {
                std::cout << " with an introduced sampler";
            } else {
                std::cout << " with sampler " << static_cast<int>(slot.sampler_ordinal);
            }
        }
        std::cout << '\n';
    }
}

void print_graph(const lsfg::graph::Graph& graph, const lsfg::graph::Extent extent) {
    const lsfg::graph::Extent flow = lsfg::graph::flow_extent(graph.config, extent);

    std::map<std::string, std::uint32_t> per_stage;
    for (const lsfg::graph::DispatchEntry& dispatch : graph.dispatches) {
        const std::string name{lsfg::shaders::chain_slots()[dispatch.slot].name};
        per_stage[name.substr(0, name.find('.'))] += 1U;
    }

    std::uint32_t prepass = 0;
    for (const lsfg::graph::DispatchEntry& dispatch : graph.dispatches) {
        prepass += dispatch.stage == lsfg::graph::prepass_stage ? 1U : 0U;
    }

    std::cout << "\nimage graph:\n"
              << "  dispatches       " << graph.dispatches.size() << " per generated frame, " << prepass
              << " of them shared\n"
              << "  descriptor sets  " << graph.variants.size() << '\n'
              << "  images           " << graph.images.size() << '\n'
              << "  uniform buffers  " << graph.uniform_buffer_count << '\n';

    std::cout << "  per stage        ";
    for (const auto& [stage, count] : per_stage) {
        std::cout << stage << ' ' << count << "  ";
    }
    std::cout << '\n';

    std::map<std::string, std::uint64_t> per_format;
    std::uint32_t widest = 0;
    std::uint32_t narrowest = 0xFFFF'FFFFU;
    for (const lsfg::graph::ImageDesc& desc : graph.images) {
        const auto role = static_cast<lsfg::graph::ImageRole>(desc.role);
        if (role == lsfg::graph::ImageRole::history || role == lsfg::graph::ImageRole::generated) {
            continue;
        }

        const lsfg::graph::Extent size = lsfg::graph::evaluate(desc, extent, flow);
        per_format[std::string{lsfg::graph::format_name(static_cast<lsfg::graph::Format>(desc.format))}]
            += static_cast<std::uint64_t>(size.width) * size.height
            * lsfg::graph::bytes_per_pixel(static_cast<lsfg::graph::Format>(desc.format));
        widest = std::max(widest, size.width);
        narrowest = std::min(narrowest, size.width);
    }

    const std::uint64_t memory = lsfg::graph::owned_memory_bytes(graph, extent);
    std::cout << "\nat " << extent.width << 'x' << extent.height << ":\n"
              << "  chain memory     " << (memory / 1024U) << " KiB\n"
              << "  widest image     " << widest << " px\n"
              << "  narrowest image  " << narrowest << " px\n";
    for (const auto& [format, bytes] : per_format) {
        std::cout << "  " << std::setw(16) << std::left << format << std::right << (bytes / 1024U)
                  << " KiB\n";
    }
}

// What the injected runtime would decide about a cache, and what it would
// allocate if it took it.
int report_acceptance(const lsfg::cache::Loaded& loaded, const Options& options) {
    lsfg::backend::Request request;
    request.config.performance = options.performance;
    request.output = options.extent;

    lsfg::backend::Plan plan;
    lsfg::backend::Rejection why;
    if (!lsfg::backend::accept(loaded, request, plan, why)) {
        std::cerr << "\nthe runtime would refuse this cache: " << lsfg::error_name(why.code) << '\n'
                  << "  " << why.reason << '\n';
        if (why.pass != lsfg::backend::no_pass) {
            std::cerr << "  module " << why.pass << '\n';
        }
        if (why.allowed != 0) {
            std::cerr << "  " << why.observed << " against " << why.allowed << " allowed\n";
        }
        return EXIT_FAILURE;
    }

    lsfg::backend::DescriptorLayout layout;
    if (const lsfg::ErrorCode code = lsfg::backend::describe(loaded.graph, layout);
        !lsfg::succeeded(code)) {
        std::cerr << "\nthe chain's bindings do not describe: " << lsfg::error_name(code) << '\n';
        return EXIT_FAILURE;
    }

    std::uint32_t widest_groups = 0;
    for (const lsfg::backend::DispatchPlan& dispatch : plan.dispatches) {
        widest_groups = std::max(widest_groups, dispatch.groups_x * dispatch.groups_y);
    }

    // Every descriptor set is bound here rather than counted.
    std::uint32_t bound_textures = 0;
    std::uint32_t bound_storage_images = 0;
    std::uint32_t widest_textures = 0;
    std::uint32_t widest_storage_images = 0;
    for (std::uint32_t index = 0; index < plan.dispatches.size(); ++index) {
        const lsfg::graph::DispatchEntry& entry = loaded.graph.dispatches[index];
        for (std::uint32_t phase = 0; phase < entry.variant_count; ++phase) {
            lsfg::backend::DispatchBinding binding;
            if (const lsfg::ErrorCode code
                = lsfg::backend::bind(loaded, plan, layout, index, phase, binding);
                !lsfg::succeeded(code)) {
                std::cerr << "\ndispatch " << index << " of " << plan.dispatches.size()
                          << " does not bind: " << lsfg::error_name(code) << '\n';
                return EXIT_FAILURE;
            }

            bound_textures += binding.texture_count;
            bound_storage_images += binding.storage_count;
            widest_textures = std::max(widest_textures, binding.texture_count);
            widest_storage_images = std::max(widest_storage_images, binding.storage_count);
        }
    }

    lsfg::backend::Schedule order;
    if (const lsfg::ErrorCode code = lsfg::backend::schedule(loaded.graph, order);
        !lsfg::succeeded(code)) {
        std::cerr << "\nthe chain cannot be run in the order it is recorded in: "
                  << lsfg::error_name(code) << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "\nthe runtime accepts this cache at " << plan.output.width << 'x'
              << plan.output.height << ":\n"
              << "  modules          " << loaded.passes.size() << '\n'
              << "  images           " << plan.images.size() << ", " << plan.owned_images
              << " allocated here and " << plan.imported_images << " from presentation\n"
              << "  owned memory     " << (plan.owned_image_bytes / 1024U) << " KiB of "
              << (request.memory_budget_bytes / 1024U) << " KiB allowed\n"
              << "  dispatches       " << plan.dispatches.size() << ", " << plan.prepass_dispatches
              << " shared and " << plan.generated_frame_dispatches << " per generated frame\n"
              << "  descriptor sets  " << plan.descriptor_sets << '\n'
              << "  uniform buffers  " << plan.uniform_buffers << '\n'
              << "  descriptors      " << layout.image_descriptors << " images, "
              << layout.sampled_images << " sampled and " << layout.storage_images << " written, "
              << lsfg::backend::sampler_descriptor_count << " samplers\n"
              << "  bindings         " << bound_textures << " texture slots and "
              << bound_storage_images << " storage images filled, at most " << widest_textures
              << " and " << widest_storage_images << " in one dispatch\n"
              << "  most registers   " << plan.max_registers << '\n'
              << "  most scratch     " << plan.max_scratch_bytes_per_warp << " B per warp\n"
              << "  most shared mem  " << plan.max_shared_memory_bytes << " B\n"
              << "  largest dispatch " << widest_groups << " workgroups\n";

    std::cout << "  chain order      " << order.stages[0].count << " in the prepass";
    for (std::uint32_t frame = 1; frame < order.stages.size(); ++frame) {
        std::cout << ", then " << order.stages[frame].count << " for generated frame "
                  << (frame - 1U);
    }
    std::cout << "\n  real frames      repeat every " << order.cycle << ", after "
              << order.warmup_frames << " of warm-up\n";
    return EXIT_SUCCESS;
}

int load_cache(const Options& options) {
    lsfg::cache::Loaded loaded;
    if (const lsfg::ErrorCode code
        = lsfg::cache::read(options.load_directory.string(), loaded);
        !lsfg::succeeded(code)) {
        std::cerr << "not a cache this build can read: " << lsfg::error_name(code) << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "cache     " << options.load_directory.string() << '\n'
              << "dll       " << lsfg::to_hex(loaded.header.dll_hash).data() << '\n'
              << "size      " << loaded.header.dll_size << " bytes\n"
              << "modules   " << loaded.passes.size() << '\n'
              << "preset    " << (loaded.graph.config.performance ? "performance" : "quality")
              << ", " << (loaded.graph.config.hdr ? "HDR" : "SDR") << ", "
              << (loaded.header.shader_precision
                          == static_cast<std::uint32_t>(lsfg::cache::Precision::high)
                      ? "high"
                      : "low")
              << " precision, " << loaded.graph.config.generated_frames << " generated frame(s)\n";

    print_graph(loaded.graph, options.extent);
    return report_acceptance(loaded, options);
}

int write_cache(const Options& options, const lsfg::prepare::Result& result) {
    const std::string directory
        = lsfg::prepare::directory_for(options.cache_directory.string(), result);

    lsfg::cache::Contents contents = result.contents;

    lsfg::cache::Loaded existing;
    if (lsfg::succeeded(lsfg::cache::read(directory, existing))) {
        const lsfg::cache::Comparison comparison = lsfg::cache::compare(existing, contents);
        std::cout << "\nagainst the cache already here:\n";
        if (comparison.identical()) {
            std::cout << "  all " << comparison.modules << " modules identical\n";
        } else if (!comparison.same_shape) {
            std::cout << "  a different set of modules, not the same chain\n";
        } else {
            std::cout << "  " << comparison.differing_modules << " of " << comparison.modules
                      << " modules differ, " << comparison.differing_bytes << " bytes in total\n";
        }
    }

    if (const lsfg::ErrorCode code = lsfg::cache::write(directory, contents); !lsfg::succeeded(code)) {
        std::cerr << "cache not written: " << lsfg::error_name(code) << '\n';
        return EXIT_FAILURE;
    }

    lsfg::cache::Loaded loaded;
    if (const lsfg::ErrorCode code = lsfg::cache::read(directory, loaded); !lsfg::succeeded(code)) {
        std::cerr << "the cache just written does not read back: " << lsfg::error_name(code) << '\n';
        return EXIT_FAILURE;
    }
    if (!lsfg::cache::same_modules(loaded, contents)) {
        std::cerr << "the cache just written does not hold what was compiled\n";
        return EXIT_FAILURE;
    }

    std::cout << "\ncache written to " << directory << '\n'
              << "  " << loaded.passes.size() << " modules and a manifest, read back and verified\n"
              << "  it holds compiled shaders derived from a proprietary DLL"
                 " and must not be published\n";
    return report_acceptance(loaded, options);
}

int report(const Options& options) {
    const std::vector<std::uint8_t> image = read_file(options.dll);
    if (image.empty()) {
        std::cerr << "cannot read " << options.dll << '\n';
        return EXIT_FAILURE;
    }

    lsfg::prepare::Options preparation;
    preparation.graph.performance = options.performance;
    preparation.keep_glsl = !options.glsl_directory.empty();

    const lsfg::Digest digest = lsfg::sha256(image);
    std::cout << "file      " << options.dll.string() << '\n'
              << "size      " << image.size() << " bytes\n"
              << "sha256    " << lsfg::to_hex(digest).data() << '\n';

    const lsfg::Digest key = lsfg::cache::cache_key(lsfg::cache::CacheKeyInputs{
        .dll_bytes = image,
        .extractor_version = lsfg::cache::extractor_version,
        .spirv_cross_revision = lsfg::version::spirv_cross_revision,
        .uam_revision = lsfg::version::uam_revision,
        .translation_options = preparation.translation.glsl_version,
        .graph_options
        = lsfg::cache::pack_options(lsfg::cache::Precision::high, preparation.graph),
        .backend_abi_version = lsfg::cache::backend_abi_version,
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
    if (const lsfg::ErrorCode result = lsfg::shaders::required_modules(
            set, lsfg::shaders::Precision::high, options.performance, requests);
        !lsfg::succeeded(result)) {
        std::cerr << "chain does not fit the detected set: " << lsfg::error_name(result) << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "chain     " << lsfg::shaders::chain_slots().size() << " dispatch slots use "
              << requests.size() << " distinct modules\n\n";

    lsfg::spirv::DescriptorCounts worst{};
    std::vector<std::uint32_t> unique_capabilities;
    bool any_specialised_workgroup = false;

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

        if (!options.dump_directory.empty()) {
            std::filesystem::create_directories(options.dump_directory);
            const std::filesystem::path path
                = options.dump_directory / (std::string{request.name} + ".spv");
            std::ofstream out(path, std::ios::binary);
            out.write(
                reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
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

    std::ranges::sort(unique_capabilities);
    std::cout << "\ncapabilities declared anywhere in the chain:\n";
    for (const std::uint32_t capability : unique_capabilities) {
        std::cout << "  " << std::setw(5) << capability << "  "
                  << lsfg::spirv::capability_name(capability) << '\n';
    }

    if (options.graph && !options.translate) {
        lsfg::graph::Graph graph;
        if (const lsfg::ErrorCode result = lsfg::graph::build(preparation.graph, graph);
            !lsfg::succeeded(result)) {
            std::cerr << "the chain does not describe: " << lsfg::error_name(result) << '\n';
            return EXIT_FAILURE;
        }
        print_graph(graph, options.extent);
    }

    if (!options.translate) {
        if (!options.dump_directory.empty()) {
            std::cout << "\nmodules written to " << options.dump_directory.string()
                      << ". These are extracted from a proprietary DLL and must not be published\n";
        }
        return EXIT_SUCCESS;
    }

    if (!lsfg::dksh::compiler_available()) {
        std::cout << "\nthis build has no GLSL to DKSH compiler."
                     " Configure with -DLSFG_BUILD_UAM=ON to add it\n";
        return EXIT_SUCCESS;
    }

    std::cout << "\ntranslating and compiling:\n";
    lsfg::prepare::Result result;
    if (const lsfg::ErrorCode code = lsfg::prepare::run(
            image,
            preparation,
            result,
            [](const lsfg::prepare::ModuleReport& module) {
                std::cout << "  " << std::setw(14) << std::left << module.name << std::right
                          << std::setw(7) << module.glsl_bytes << " B GLSL " << std::setw(6)
                          << module.dksh_bytes << " B DKSH " << std::setw(4) << module.registers
                          << " regs " << std::setw(5) << module.scratch_bytes << " B scratch\n";
            });
        !lsfg::succeeded(code)) {
        std::cerr << "preparation failed: " << lsfg::error_name(code) << '\n';
        return EXIT_FAILURE;
    }

    std::size_t glsl_bytes = 0;
    std::size_t dksh_bytes = 0;
    std::uint32_t worst_gprs = 0;
    std::uint32_t worst_scratch = 0;
    std::uint32_t worst_shared_memory = 0;
    std::uint32_t introduced_samplers = 0;
    std::uint32_t storage_images_formatted = 0;

    for (const lsfg::prepare::ModuleReport& module : result.reports) {
        glsl_bytes += module.glsl_bytes;
        dksh_bytes += module.dksh_bytes;
        worst_gprs = std::max(worst_gprs, module.registers);
        worst_scratch = std::max(worst_scratch, module.scratch_bytes);
        worst_shared_memory = std::max(worst_shared_memory, module.shared_memory_bytes);
        introduced_samplers += module.needed_introduced_sampler ? 1U : 0U;
        storage_images_formatted += module.storage_images_formatted;

        if (!options.glsl_directory.empty()) {
            std::filesystem::create_directories(options.glsl_directory);
            std::ofstream out(options.glsl_directory / (module.name + ".comp"));
            out << module.glsl;
        }
    }

    std::uint32_t worst_textures = 0;
    std::uint32_t worst_storage = 0;
    for (const lsfg::cache::PassInput& pass : result.contents.passes) {
        worst_textures = std::max(worst_textures, pass.entry.texture_slot_count);
        worst_storage = std::max(worst_storage, pass.entry.storage_image_count);
    }

    const lsfg::translate::Limits limits = preparation.translation.limits;
    std::cout << "\ntranslation and compilation:\n"
              << "  modules          " << result.reports.size() << " of " << requests.size() << '\n'
              << "  total GLSL       " << glsl_bytes << " bytes\n"
              << "  total DKSH       " << dksh_bytes << " bytes\n"
              << "  texture slots    " << worst_textures << " of " << limits.textures << '\n'
              << "  storage images   " << worst_storage << " of " << limits.storage_images << '\n'
              << "  most registers   " << worst_gprs << '\n'
              << "  most scratch     " << worst_scratch << " bytes per warp\n"
              << "  most shared mem  " << worst_shared_memory << " bytes\n"
              << "  storage images given a format  " << storage_images_formatted << '\n'
              << "  modules needing a sampler for an unsampled image  " << introduced_samplers << '\n';

    if (options.list_all) {
        std::cout << "\nslot tables:\n";
        for (std::size_t index = 0; index < result.contents.passes.size(); ++index) {
            std::cout << "  " << result.reports[index].name << '\n';
            print_slots(result.contents.passes[index].slots);
        }
    }

    print_graph(result.contents.graph, options.extent);

    if (!options.cache_directory.empty()) {
        return write_cache(options, result);
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
                  << "usage: lsfg-inspect <Lossless.dll> [--all] [--translate] [--graph]\n"
                     "                    [--performance] [--extent <width>x<height>]\n"
                     "                    [--dump <directory>] [--glsl <directory>]\n"
                     "                    [--cache <directory>]\n"
                     "       lsfg-inspect --load <cache directory> [--performance]\n"
                     "                    [--extent <width>x<height>]\n";
        return EXIT_FAILURE;
    }

    if (!options.load_directory.empty()) {
        return load_cache(options);
    }

    return report(options);
}
