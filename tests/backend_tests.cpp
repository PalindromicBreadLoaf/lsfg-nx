// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/backend/cache_load.hpp>

#include <lsfg/common/cache_store.hpp>
#include <lsfg/common/dksh.hpp>
#include <lsfg/common/image_graph.hpp>
#include <lsfg/common/shader_set.hpp>

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

} // namespace

int main() {
    test_a_prepared_cache_becomes_a_plan();
    test_a_cache_for_another_build_is_refused();
    test_a_module_that_does_not_match_its_entry_is_refused();
    test_a_module_the_executor_cannot_bind_is_refused();
    test_the_chain_is_refused_before_it_allocates();
    test_a_missing_cache_leaves_the_runtime_alone();

    std::cout << "backend cache tests passed\n";
    return EXIT_SUCCESS;
}
