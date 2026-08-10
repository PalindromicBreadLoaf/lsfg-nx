// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/backend/binding.hpp>
#include <lsfg/backend/cache_load.hpp>
#include <lsfg/backend/layout.hpp>
#include <lsfg/backend/schedule.hpp>

#include <lsfg/common/cache_store.hpp>
#include <lsfg/common/dksh.hpp>
#include <lsfg/common/image_graph.hpp>
#include <lsfg/common/shader_set.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void require_accepted(
    const bool accepted,
    const lsfg::backend::Rejection& why,
    const char* const message) {
    if (!accepted) {
        std::cerr << "refused: " << lsfg::error_name(why.code) << ", " << why.reason << " (pass "
                  << why.pass << ", " << why.observed << " against " << why.allowed << ")\n";
    }
    require(accepted, message);
}

struct Interface {
    std::uint32_t block_index;
    std::uint32_t images;
    std::uint32_t storage_images;
    std::uint32_t samplers;
    std::uint32_t uniform_buffers;
    std::uint16_t workgroup;
};

const std::vector<Interface>& interfaces() {
    static const std::vector<Interface> table{
        {1, 1, 7, 1, 1, 32},  {2, 5, 1, 2, 1, 16},  {3, 9, 3, 2, 1, 8},   {4, 10, 2, 2, 1, 8},
        {5, 3, 4, 1, 0, 8},   {6, 4, 4, 1, 0, 8},   {7, 4, 4, 1, 0, 8},   {8, 6, 1, 2, 1, 8},
        {9, 3, 4, 1, 0, 8},   {10, 4, 4, 1, 0, 8},  {11, 4, 4, 1, 0, 8},  {12, 6, 1, 2, 1, 8},
        {13, 1, 2, 1, 0, 8},  {14, 2, 2, 1, 0, 8},  {15, 2, 4, 1, 0, 8},  {16, 4, 4, 1, 0, 8},
        {17, 2, 2, 1, 0, 8},  {18, 2, 2, 1, 0, 8},  {19, 2, 2, 1, 0, 8},  {20, 3, 1, 2, 1, 8},
        {21, 12, 2, 1, 0, 8}, {22, 2, 2, 1, 0, 8},  {23, 2, 2, 1, 0, 8},  {24, 2, 2, 1, 0, 8},
        {25, 2, 6, 1, 1, 32},
    };
    return table;
}

struct Program {
    std::uint32_t workgroup{};
    std::uint32_t gprs{};
    std::uint32_t scratch{};
    std::uint32_t shared_memory{};
};

// A blob shaped like what the compiler emits, since the runtime reads the
// module's own requirements back out of it rather than trusting the manifest.
std::vector<std::uint8_t> build_dksh(const Program& program) {
    constexpr std::uint32_t control_size = 256;
    constexpr std::uint32_t code_size = 256;
    constexpr std::uint32_t programs_offset = sizeof(lsfg::dksh::FileHeader);

    std::vector<std::uint8_t> bytes(control_size + code_size, 0);

    const lsfg::dksh::FileHeader header{
        .magic = lsfg::dksh::magic,
        .header_size = sizeof(lsfg::dksh::FileHeader),
        .control_size = control_size,
        .code_size = code_size,
        .programs_offset = programs_offset,
        .program_count = 1,
    };
    std::memcpy(bytes.data(), &header, sizeof(header));

    const auto write = [&bytes](const std::size_t offset, const std::uint32_t value) {
        std::memcpy(bytes.data() + programs_offset + offset, &value, sizeof(value));
    };
    write(0, static_cast<std::uint32_t>(lsfg::dksh::ProgramType::compute));
    write(4, 0);  // entry point
    write(8, program.gprs);
    write(20, program.scratch);
    write(24, program.workgroup);
    write(28, program.workgroup);
    write(32, 1);
    write(36, program.shared_memory);

    return bytes;
}

struct Fixture {
    lsfg::cache::Contents contents;
    std::vector<std::vector<std::uint8_t>> modules;
};

// A cache with the real chain's shape and modules whose blobs say what the
// manifest says they say.
void build_fixture(Fixture& out) {
    out = Fixture{};

    lsfg::cache::initialize(out.contents.header);
    out.contents.header.dll_size = 7'521'280;
    out.contents.header.shader_first_resource_id = 303;
    out.contents.header.shader_block_size = 49;
    out.contents.header.shader_precision
        = static_cast<std::uint32_t>(lsfg::cache::Precision::high);

    require(
        lsfg::succeeded(lsfg::graph::build(lsfg::graph::Config{}, out.contents.graph)),
        "the graph builds");

    out.modules.reserve(interfaces().size());

    for (const Interface& module : interfaces()) {
        const Program program{
            .workgroup = module.workgroup,
            .gprs = 32U + module.block_index,
            .scratch = 0,
            .shared_memory = 0,
        };
        out.modules.push_back(build_dksh(program));

        lsfg::cache::PassInput pass;
        pass.entry.resource_id = 352U + module.block_index;
        pass.entry.block_index = module.block_index;
        pass.entry.workgroup_x = module.workgroup;
        pass.entry.workgroup_y = module.workgroup;
        pass.entry.workgroup_z = 1;
        pass.entry.image_count = module.images;
        pass.entry.storage_image_count = module.storage_images;
        pass.entry.sampler_count = module.samplers;
        pass.entry.uniform_buffer_count = module.uniform_buffers;
        pass.entry.texture_slot_count = module.images + 1U;
        pass.entry.register_count = program.gprs;
        pass.entry.scratch_memory_bytes = program.scratch;
        pass.entry.shared_memory_bytes = program.shared_memory;

        for (std::uint32_t index = 0; index < module.uniform_buffers; ++index) {
            pass.slots.push_back(lsfg::cache::SlotEntry{
                .kind = static_cast<std::uint8_t>(lsfg::cache::SlotKind::uniform_buffer),
                .slot = static_cast<std::uint8_t>(index),
                .ordinal = static_cast<std::uint8_t>(index)});
        }
        for (std::uint32_t index = 0; index < module.images; ++index) {
            pass.slots.push_back(lsfg::cache::SlotEntry{
                .kind = static_cast<std::uint8_t>(lsfg::cache::SlotKind::texture),
                .slot = static_cast<std::uint8_t>(index),
                .ordinal = static_cast<std::uint8_t>(index)});
        }
        pass.slots.push_back(lsfg::cache::SlotEntry{
            .kind = static_cast<std::uint8_t>(lsfg::cache::SlotKind::texture),
            .slot = static_cast<std::uint8_t>(module.images),
            .ordinal = 0,
            .sampler_ordinal = lsfg::cache::introduced_sampler});
        for (std::uint32_t index = 0; index < module.storage_images; ++index) {
            pass.slots.push_back(lsfg::cache::SlotEntry{
                .kind = static_cast<std::uint8_t>(lsfg::cache::SlotKind::storage_image),
                .slot = static_cast<std::uint8_t>(index),
                .ordinal = static_cast<std::uint8_t>(index)});
        }

        pass.entry.slot_count = static_cast<std::uint32_t>(pass.slots.size());
        out.contents.passes.push_back(std::move(pass));
    }

    for (std::size_t index = 0; index < out.contents.passes.size(); ++index) {
        out.contents.passes[index].dksh = out.modules[index];
    }
}

// The cache as the runtime sees it, which is only ever what came off the card.
void build_loaded(lsfg::cache::Loaded& out) {
    const std::filesystem::path root
        = std::filesystem::temp_directory_path() / "lsfg-nx-backend-tests";
    std::filesystem::remove_all(root);

    const std::string directory = lsfg::cache::directory_for(root.string(), lsfg::Digest{});

    Fixture fixture;
    build_fixture(fixture);
    require(lsfg::succeeded(lsfg::cache::write(directory, fixture.contents)), "the cache is written");
    require(lsfg::succeeded(lsfg::cache::read(directory, out)), "the cache is read back");

    std::filesystem::remove_all(root);
}

lsfg::backend::Request handheld_request() {
    return lsfg::backend::Request{
        .config = lsfg::graph::Config{},
        .precision = lsfg::cache::Precision::high,
        .output = lsfg::graph::Extent{.width = 1280, .height = 720},
    };
}

void test_a_prepared_cache_becomes_a_plan() {
    lsfg::cache::Loaded loaded;
    build_loaded(loaded);

    lsfg::backend::Plan plan;
    lsfg::backend::Rejection why;
    require_accepted(
        lsfg::backend::accept(loaded, handheld_request(), plan, why),
        why,
        "the cache preparation writes is one the runtime runs");

    require(plan.images.size() == loaded.graph.images.size(), "every image is planned");
    require(plan.dispatches.size() == 100, "every dispatch is planned");
    require(
        plan.prepass_dispatches + plan.generated_frame_dispatches == plan.dispatches.size(),
        "every dispatch belongs to a stage");
    require(plan.prepass_dispatches == 34, "the prepass is shared by every generated frame");
    require(plan.owned_images + plan.imported_images == plan.images.size(), "every image is owned or imported");
    require(plan.imported_images == 3, "two history frames and one generated frame come from presentation");
    require(
        plan.owned_image_bytes == lsfg::graph::owned_memory_bytes(loaded.graph, plan.output),
        "the plan costs what the graph says it costs");
    require(plan.descriptor_sets == 128, "every descriptor set is planned");

    for (const lsfg::backend::DispatchPlan& dispatch : plan.dispatches) {
        require(dispatch.pass < loaded.passes.size(), "every dispatch names a module the cache holds");
        require(dispatch.groups_x != 0 && dispatch.groups_y != 0, "every dispatch covers something");
    }

    // The first dispatch builds the mip pyramid over the whole output, 64
    // pixels of it per workgroup.
    require(plan.dispatches.front().groups_x == 20, "the pyramid covers the output across");
    require(plan.dispatches.front().groups_y == 12, "the pyramid covers the output down");

    require(plan.output.width == 1280 && plan.flow.width == 1280, "the flow extent follows the output");
}

void test_a_cache_for_another_build_is_refused() {
    lsfg::cache::Loaded loaded;
    build_loaded(loaded);

    lsfg::backend::Plan plan;
    lsfg::backend::Rejection why;

    lsfg::cache::Loaded other = loaded;
    other.header.backend_abi_version = lsfg::cache::backend_abi_version + 1U;
    require(
        !lsfg::backend::accept(other, handheld_request(), plan, why)
            && why.code == lsfg::ErrorCode::cache_version_mismatch,
        "a cache prepared for a different backend is refused");

    other = loaded;
    other.header.shader_precision = static_cast<std::uint32_t>(lsfg::cache::Precision::low);
    require(
        !lsfg::backend::accept(other, handheld_request(), plan, why)
            && why.code == lsfg::ErrorCode::cache_configuration_mismatch,
        "a cache prepared at the other precision is refused");

    lsfg::backend::Request request = handheld_request();
    request.config.generated_frames = 2;
    require(
        !lsfg::backend::accept(loaded, request, plan, why)
            && why.code == lsfg::ErrorCode::cache_configuration_mismatch,
        "a cache prepared for one generated frame is not used for two");

    request = handheld_request();
    request.config.performance = true;
    require(
        !lsfg::backend::accept(loaded, request, plan, why)
            && why.code == lsfg::ErrorCode::cache_configuration_mismatch,
        "a cache prepared for the quality preset is not used for the performance one");
}

void test_a_module_that_does_not_match_its_entry_is_refused() {
    lsfg::cache::Loaded loaded;
    build_loaded(loaded);

    lsfg::backend::Plan plan;
    lsfg::backend::Rejection why;

    lsfg::cache::Loaded other = loaded;
    other.passes[2].dksh = build_dksh(Program{
        .workgroup = 16,
        .gprs = other.passes[2].entry.register_count,
        .scratch = 0,
        .shared_memory = 0});
    require(
        !lsfg::backend::accept(other, handheld_request(), plan, why)
            && why.code == lsfg::ErrorCode::shader_interface_mismatch && why.pass == 2,
        "a module whose workgroup is not the recorded one is refused");

    other = loaded;
    other.passes[4].dksh = build_dksh(Program{
        .workgroup = other.passes[4].entry.workgroup_x,
        .gprs = other.passes[4].entry.register_count,
        .scratch = 512,
        .shared_memory = 0});
    require(
        !lsfg::backend::accept(other, handheld_request(), plan, why)
            && why.code == lsfg::ErrorCode::cache_integrity_failure && why.pass == 4,
        "a module needing scratch the manifest never recorded is refused");

    other = loaded;
    other.passes[1].dksh.assign(64, 0);
    require(
        !lsfg::backend::accept(other, handheld_request(), plan, why) && why.pass == 1,
        "a module the executor cannot load is refused");
}

void test_a_module_the_executor_cannot_bind_is_refused() {
    lsfg::cache::Loaded loaded;
    build_loaded(loaded);

    lsfg::backend::Plan plan;
    lsfg::backend::Rejection why;

    // The pass that opens the chain writes 7 storage images, one below what the
    // compiler allows.
    lsfg::backend::Request request = handheld_request();
    request.limits.storage_images = 6;
    require(
        !lsfg::backend::accept(loaded, request, plan, why)
            && why.code == lsfg::ErrorCode::shader_interface_mismatch && why.observed == 7,
        "a module writing more storage images than the executor binds is refused");

    request = handheld_request();
    request.limits.workgroup_invocations = 256;
    require(
        !lsfg::backend::accept(loaded, request, plan, why)
            && why.code == lsfg::ErrorCode::shader_interface_mismatch,
        "a workgroup larger than the executor dispatches is refused");
}

void test_the_chain_is_refused_before_it_allocates() {
    lsfg::cache::Loaded loaded;
    build_loaded(loaded);

    lsfg::backend::Plan plan;
    lsfg::backend::Rejection why;

    lsfg::backend::Request request = handheld_request();
    request.memory_budget_bytes = 16ULL * 1024U * 1024U;
    require(
        !lsfg::backend::accept(loaded, request, plan, why)
            && why.code == lsfg::ErrorCode::out_of_memory,
        "a chain larger than its budget is refused rather than half allocated");

    // 720p and 1080p both fit the default budget.
    request = handheld_request();
    require_accepted(
        lsfg::backend::accept(loaded, request, plan, why), why, "720p fits the default budget");
    request.output = lsfg::graph::Extent{.width = 1920, .height = 1080};
    require_accepted(
        lsfg::backend::accept(loaded, request, plan, why), why, "1080p fits the default budget");

    request.output = lsfg::graph::Extent{.width = 32, .height = 32};
    require(
        !lsfg::backend::accept(loaded, request, plan, why)
            && why.code == lsfg::ErrorCode::unsupported,
        "an output the chain rounds away to nothing is refused");

    request.output = lsfg::graph::Extent{};
    require(
        !lsfg::backend::accept(loaded, request, plan, why)
            && why.code == lsfg::ErrorCode::invalid_argument,
        "no output extent is not an extent to plan at");
}

void test_a_missing_cache_leaves_the_runtime_alone() {
    const std::filesystem::path root
        = std::filesystem::temp_directory_path() / "lsfg-nx-backend-tests-empty";
    std::filesystem::remove_all(root);

    lsfg::cache::Loaded loaded;
    lsfg::backend::Plan plan;
    lsfg::backend::Rejection why;
    require(
        !lsfg::backend::load(root.string(), lsfg::Digest{}, handheld_request(), loaded, plan, why)
            && why.code == lsfg::ErrorCode::cache_missing,
        "a cache that was never prepared is a refusal like any other");
    require(!why.reason.empty(), "a refusal says why without allocating");
}

void test_every_image_carries_the_descriptors_it_is_reached_through() {
    lsfg::graph::Graph graph;
    require(lsfg::succeeded(lsfg::graph::build(lsfg::graph::Config{}, graph)), "the graph builds");

    lsfg::backend::DescriptorLayout layout;
    require(lsfg::succeeded(lsfg::backend::describe(graph, layout)), "the chain is described");

    require(layout.images.size() == graph.images.size(), "every image is described");
    require(
        layout.image_descriptors == layout.sampled_images + layout.storage_images,
        "a descriptor is either a sampled one or a storage one");

    std::vector<bool> seen(layout.image_descriptors, false);
    for (std::size_t index = 0; index < layout.images.size(); ++index) {
        const lsfg::backend::ImageDescriptors& entry = layout.images[index];
        require(
            entry.sampled != lsfg::backend::no_descriptor
                || entry.storage != lsfg::backend::no_descriptor,
            "no image is allocated that nothing reaches");

        for (const std::uint32_t descriptor : {entry.sampled, entry.storage}) {
            if (descriptor == lsfg::backend::no_descriptor) {
                continue;
            }
            require(descriptor < seen.size(), "a descriptor is inside the table");
            require(!seen[descriptor], "no two images share a descriptor");
            seen[descriptor] = true;
        }

        // A real frame is only ever read and a generated one only ever written,
        // so neither needs the descriptor the other would.
        const auto role = static_cast<lsfg::graph::ImageRole>(graph.images[index].role);
        if (role == lsfg::graph::ImageRole::history) {
            require(
                entry.storage == lsfg::backend::no_descriptor,
                "a real frame is never given a storage descriptor");
        }
        if (role == lsfg::graph::ImageRole::generated) {
            require(
                entry.sampled == lsfg::backend::no_descriptor,
                "a generated frame is never given a sampled descriptor");
        }
    }

    for (const bool used : seen) {
        require(used, "the descriptor table has no holes in it");
    }
}

void test_a_binding_outside_the_graph_is_refused() {
    lsfg::graph::Graph graph;
    require(lsfg::succeeded(lsfg::graph::build(lsfg::graph::Config{}, graph)), "the graph builds");

    lsfg::backend::DescriptorLayout layout;

    lsfg::graph::Graph broken = graph;
    broken.bindings.back() = static_cast<std::uint32_t>(broken.images.size());
    require(
        lsfg::backend::describe(broken, layout) == lsfg::ErrorCode::cache_integrity_failure,
        "a binding naming an image the graph does not have is refused");

    broken = graph;
    broken.bindings.pop_back();
    require(
        lsfg::backend::describe(broken, layout) == lsfg::ErrorCode::cache_integrity_failure,
        "a variant reaching past the bindings is refused");
}

struct Bound {
    lsfg::cache::Loaded loaded;
    lsfg::backend::Plan plan;
    lsfg::backend::DescriptorLayout descriptors;
};

void build_bound(Bound& out) {
    build_loaded(out.loaded);

    lsfg::backend::Rejection why;
    require_accepted(
        lsfg::backend::accept(out.loaded, handheld_request(), out.plan, why),
        why,
        "the cache is one the runtime runs");
    require(
        lsfg::succeeded(lsfg::backend::describe(out.loaded.graph, out.descriptors)),
        "the chain is described");
}

void test_every_dispatch_binds_what_its_module_declares() {
    Bound bound;
    build_bound(bound);

    for (std::uint32_t dispatch = 0; dispatch < bound.plan.dispatches.size(); ++dispatch) {
        for (std::uint32_t phase = 0; phase < 4; ++phase) {
            lsfg::backend::DispatchBinding binding;
            require(
                lsfg::succeeded(lsfg::backend::bind(
                    bound.loaded, bound.plan, bound.descriptors, dispatch, phase, binding)),
                "every dispatch of the chain binds");

            const lsfg::cache::PassEntry& entry = bound.loaded.passes[binding.pass].entry;
            require(
                binding.texture_count == entry.texture_slot_count,
                "every texture slot the module was translated onto is filled");
            require(
                binding.storage_count == entry.storage_image_count,
                "every storage image the module declares is bound");
            require(
                (binding.uniform_slot != lsfg::backend::no_slot)
                    == (entry.uniform_buffer_count != 0),
                "a uniform buffer is bound exactly when the module takes one");

            std::vector<bool> texture_slots(lsfg::backend::max_texture_slots, false);
            for (std::uint32_t index = 0; index < binding.texture_count; ++index) {
                const lsfg::backend::TextureBinding& texture = binding.textures[index];
                require(!texture_slots[texture.slot], "no two textures share a slot");
                texture_slots[texture.slot] = true;
                require(
                    texture.descriptor < bound.descriptors.image_descriptors,
                    "a texture names a descriptor inside the table");
                require(
                    texture.sampler < lsfg::backend::sampler_descriptor_count,
                    "a texture names a sampler the backend built");
            }

            std::vector<bool> storage_slots(lsfg::backend::max_storage_slots, false);
            for (std::uint32_t index = 0; index < binding.storage_count; ++index) {
                const lsfg::backend::StorageBinding& storage = binding.storages[index];
                require(!storage_slots[storage.slot], "no two storage images share a slot");
                storage_slots[storage.slot] = true;
                require(
                    storage.descriptor < bound.descriptors.image_descriptors,
                    "a storage image names a descriptor inside the table");
            }
        }
    }
}

void test_the_pass_that_opens_the_chain_binds_the_pyramid() {
    Bound bound;
    build_bound(bound);

    lsfg::backend::DispatchBinding first;
    require(
        lsfg::succeeded(
            lsfg::backend::bind(bound.loaded, bound.plan, bound.descriptors, 0, 0, first)),
        "the first dispatch binds");

    require(first.groups_x == 20 && first.groups_y == 12, "it covers the output");
    require(first.storage_count == 7, "it writes the whole pyramid at once");
    require(first.texture_count == 2, "it reads one image through two texture slots");
    require(
        first.textures[0].image == first.textures[1].image,
        "the slot translation introduced reaches the image the module declared");
    require(
        first.textures[0].sampler != first.textures[1].sampler,
        "the introduced slot carries the sampler the module never declared");
    require(first.uniform_slot != lsfg::backend::no_slot, "it takes the shared constants");

    std::vector<std::uint32_t> written;
    for (std::uint32_t index = 0; index < first.storage_count; ++index) {
        written.push_back(first.storages[index].image);
    }
    require(
        std::adjacent_find(written.begin(), written.end()) == written.end(),
        "no level of the pyramid is written twice");

    lsfg::backend::DispatchBinding second;
    require(
        lsfg::succeeded(
            lsfg::backend::bind(bound.loaded, bound.plan, bound.descriptors, 0, 1, second)),
        "the next real frame binds");
    require(
        first.textures[0].image != second.textures[0].image,
        "consecutive real frames read different history slots");
    require(first.variant != second.variant, "each is its own descriptor set");

    lsfg::backend::DispatchBinding third;
    require(
        lsfg::succeeded(
            lsfg::backend::bind(bound.loaded, bound.plan, bound.descriptors, 0, 2, third)),
        "the frame after that binds");
    require(third.variant == first.variant, "the history slots alternate rather than run out");
}

void test_a_slot_table_that_disagrees_with_the_chain_is_refused() {
    Bound bound;
    build_bound(bound);

    lsfg::backend::DispatchBinding binding;

    lsfg::cache::Loaded other = bound.loaded;
    other.passes[0].slots[1].ordinal = 4;
    require(
        lsfg::backend::bind(other, bound.plan, bound.descriptors, 0, 0, binding)
            == lsfg::ErrorCode::shader_interface_mismatch,
        "a texture slot reading past what the dispatch binds is refused");

    other = bound.loaded;
    other.passes[0].entry.image_count = 2;
    require(
        lsfg::backend::bind(other, bound.plan, bound.descriptors, 0, 0, binding)
            == lsfg::ErrorCode::shader_interface_mismatch,
        "a module wanting more images than the dispatch has is refused");

    other = bound.loaded;
    other.passes[0].slots[0].kind = 0xFF;
    require(
        lsfg::backend::bind(other, bound.plan, bound.descriptors, 0, 0, binding)
            == lsfg::ErrorCode::shader_interface_mismatch,
        "a slot of no kind the executor binds is refused");

    other = bound.loaded;
    other.passes[0].slots.pop_back();
    require(
        lsfg::backend::bind(other, bound.plan, bound.descriptors, 0, 0, binding)
            == lsfg::ErrorCode::cache_integrity_failure,
        "a slot table shorter than the entry says is refused");

    require(
        lsfg::backend::bind(
            bound.loaded,
            bound.plan,
            bound.descriptors,
            static_cast<std::uint32_t>(bound.plan.dispatches.size()),
            0,
            binding)
            == lsfg::ErrorCode::invalid_argument,
        "a dispatch the chain does not have is not bound");
}

void test_allocations_are_placed_and_overflow_is_caught() {
    lsfg::backend::Arena arena;

    require(arena.place(100, 256) == 0, "the first allocation starts at the beginning");
    require(arena.place(100, 256) == 256, "the next one is aligned past it");
    require(arena.place(1, 1) == 356, "an unaligned allocation follows immediately");
    require(arena.used() == 357, "the arena is as long as what it holds");
    require(!arena.overflowed(), "nothing has overflowed");
    require(
        arena.block_size() == lsfg::backend::memory_block_alignment,
        "a memory block is a whole number of pages");

    lsfg::backend::Arena empty;
    require(empty.block_size() == 0, "an arena holding nothing needs no block");

    lsfg::backend::Arena odd;
    static_cast<void>(odd.place(16, 3));
    require(odd.overflowed(), "an alignment that is not a power of two is not honoured quietly");

    lsfg::backend::Arena full;
    static_cast<void>(full.place(lsfg::backend::max_memory_block_bytes, 1));
    require(!full.overflowed(), "the largest block a 32-bit offset reaches is allowed");
    static_cast<void>(full.place(1, 1));
    require(full.overflowed(), "one byte past it is not");
    require(full.block_size() == 0, "an arena that overflowed sizes no block");
}

void test_the_chain_runs_in_stage_order() {
    lsfg::graph::Graph graph;
    require(lsfg::succeeded(lsfg::graph::build(lsfg::graph::Config{}, graph)), "the graph builds");

    lsfg::backend::Schedule order;
    require(
        lsfg::succeeded(lsfg::backend::schedule(graph, order)), "the chain is put in run order");

    require(order.stages.size() == 2, "one generated frame is one stage after the prepass");
    require(order.stages[0].first == 0, "the prepass opens the chain");
    require(order.stages[0].count == 34, "the prepass is 34 dispatches");
    require(order.stages[1].first == 34, "the generated frame follows it");
    require(order.stages[1].count == 66, "a generated frame is 66 dispatches");
    require(order.dispatches() == graph.dispatches.size(), "every dispatch is in a stage");
    require(order.generated_frames() == 1, "one generated frame is scheduled");
    require(order.cycle == 6, "the real frame index repeats every six frames");
    require(order.warmup_frames == 2, "two real frames go in before one comes out whole");

    lsfg::graph::Config pair;
    pair.generated_frames = 2;

    lsfg::graph::Graph second;
    require(lsfg::succeeded(lsfg::graph::build(pair, second)), "a two-frame graph builds");
    require(lsfg::succeeded(lsfg::backend::schedule(second, order)), "and is put in run order");
    require(order.stages.size() == 3, "each generated frame is a stage of its own");
    require(order.stages[0].count == 34, "which leaves the prepass shared");
    require(
        order.stages[1].count == order.stages[2].count, "and every generated frame the same size");
}

void test_a_stage_out_of_order_is_refused() {
    lsfg::graph::Graph graph;
    require(lsfg::succeeded(lsfg::graph::build(lsfg::graph::Config{}, graph)), "the graph builds");

    // The chain closes on the generated frame. Putting that dispatch back in
    // the prepass leaves the prepass in two pieces with a stage between them.
    graph.dispatches.back().stage = lsfg::graph::prepass_stage;

    lsfg::backend::Schedule order;
    require(
        lsfg::backend::schedule(graph, order) == lsfg::ErrorCode::presentation_sequence_invalid,
        "a stage split in two is refused");
}

void test_an_image_nothing_writes_is_refused() {
    lsfg::graph::Graph graph;
    require(lsfg::succeeded(lsfg::graph::build(lsfg::graph::Config{}, graph)), "the graph builds");

    const auto stray = static_cast<std::uint32_t>(graph.images.size());
    graph.images.push_back(lsfg::graph::ImageDesc{
        .base = static_cast<std::uint8_t>(lsfg::graph::ExtentBase::output),
        .format = static_cast<std::uint8_t>(lsfg::graph::Format::rgba8),
        .role = static_cast<std::uint8_t>(lsfg::graph::ImageRole::internal)});

    const lsfg::graph::VariantEntry& variant
        = graph.variants[graph.dispatches.back().variant_first];
    require(variant.texture_count != 0, "the closing pass reads something");
    graph.bindings[variant.binding_first] = stray;

    lsfg::backend::Schedule order;
    require(
        lsfg::backend::schedule(graph, order) == lsfg::ErrorCode::cache_integrity_failure,
        "a read of an image nothing fills is refused however long the chain runs");
}

} // namespace

int main() {
    test_a_prepared_cache_becomes_a_plan();
    test_a_cache_for_another_build_is_refused();
    test_a_module_that_does_not_match_its_entry_is_refused();
    test_a_module_the_executor_cannot_bind_is_refused();
    test_the_chain_is_refused_before_it_allocates();
    test_a_missing_cache_leaves_the_runtime_alone();
    test_every_image_carries_the_descriptors_it_is_reached_through();
    test_a_binding_outside_the_graph_is_refused();
    test_every_dispatch_binds_what_its_module_declares();
    test_the_pass_that_opens_the_chain_binds_the_pyramid();
    test_a_slot_table_that_disagrees_with_the_chain_is_refused();
    test_allocations_are_placed_and_overflow_is_caught();
    test_the_chain_runs_in_stage_order();
    test_a_stage_out_of_order_is_refused();
    test_an_image_nothing_writes_is_refused();

    std::cout << "backend cache tests passed\n";
    return EXIT_SUCCESS;
}
