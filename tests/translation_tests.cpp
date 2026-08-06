// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

// The fixtures are compiled from shaders written for these checks, so nothing
// here needs Lossless. Compilation to DKSH is only exercised in builds
// configured with the compiler.

#include "spirv_fixtures.hpp"

#include <lsfg/common/dksh.hpp>
#include <lsfg/common/translate.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

template <std::size_t N>
std::span<const std::uint8_t> module_bytes(const std::uint32_t (&words)[N]) {
    return {reinterpret_cast<const std::uint8_t*>(words), N * sizeof(std::uint32_t)};
}

const lsfg::translate::SlotAssignment* find(
    const lsfg::translate::Module& module,
    const lsfg::translate::SlotKind kind,
    const std::uint32_t slot) {
    for (const lsfg::translate::SlotAssignment& assignment : module.slots) {
        if (assignment.kind == kind && assignment.slot == slot) {
            return &assignment;
        }
    }
    return nullptr;
}

lsfg::translate::Module translate(const std::span<const std::uint8_t> bytes) {
    lsfg::translate::Module module;
    const lsfg::ErrorCode result
        = lsfg::translate::to_glsl(bytes, lsfg::translate::Options{}, module);
    require(lsfg::succeeded(result), "the fixture translates");
    return module;
}

void test_produces_desktop_glsl() {
    const lsfg::translate::Module module = translate(module_bytes(lsfg::test::formatted_module));

    require(module.glsl.starts_with("#version 450"), "the output declares its version first");
    require(
        module.glsl.find("layout(local_size_x = 8, local_size_y = 4, local_size_z = 1) in;")
            != std::string::npos,
        "the workgroup size survives translation");
    require(module.local_size_x == 8 && module.local_size_y == 4 && module.local_size_z == 1,
            "the workgroup size is reported");

    // Desktop GLSL has neither of these.
    require(module.glsl.find("texture2D src") == std::string::npos, "no separate image survives");
    require(module.glsl.find("uniform sampler ") == std::string::npos, "no separate sampler survives");
    require(module.glsl.find("set = ") == std::string::npos, "no descriptor set survives");
    require(module.glsl.find("sampler2D") != std::string::npos, "the pair became a combined sampler");
}

// Declaring gl_WorkGroupSize is not the same as making it specialisable, and
// treating it as such refuses modules whose size is a plain constant.
void test_a_declared_workgroup_size_is_not_a_specialised_one() {
    lsfg::spirv::Inventory inventory;
    require(
        lsfg::succeeded(
            lsfg::spirv::inspect_bytes(module_bytes(lsfg::test::formatted_module), inventory)),
        "the fixture is inspected");

    require(!inventory.local_size_is_specialised, "a constant workgroup size is not specialised");
    require(inventory.local_size_x == 8 && inventory.local_size_y == 4, "the size is read");
}

void test_slots_start_at_zero_in_each_space() {
    const lsfg::translate::Module module = translate(module_bytes(lsfg::test::formatted_module));

    require(module.uniform_buffer_count == 1, "one uniform buffer");
    require(module.storage_image_count == 1, "one storage image");
    // The image is sampled and also queried for its size, so it is combined
    // twice.
    require(module.texture_count == 2, "the image combines with two samplers");
    require(module.needed_dummy_sampler, "the size query needs a sampler of its own");

    require(find(module, lsfg::translate::SlotKind::uniform_buffer, 0) != nullptr,
            "the uniform buffer lands in slot 0");
    require(find(module, lsfg::translate::SlotKind::storage_image, 0) != nullptr,
            "the storage image lands in slot 0");
    require(find(module, lsfg::translate::SlotKind::texture, 0) != nullptr,
            "the first texture lands in slot 0");

    require(module.slots.size() == 4, "every resource is recorded");
    for (const lsfg::translate::SlotAssignment& slot : module.slots) {
        if (slot.kind == lsfg::translate::SlotKind::texture) {
            require(slot.spirv_binding == 32, "the texture came from the image binding range");
            require(slot.uses_dummy_sampler || slot.spirv_sampler_binding == 16,
                    "a real sampler came from the sampler binding range");
        } else if (slot.kind == lsfg::translate::SlotKind::uniform_buffer) {
            require(slot.spirv_binding == 0, "the uniform buffer came from binding 0");
        } else {
            require(slot.spirv_binding == 48, "the storage image came from the image range");
        }
    }

    // Both textures address the same image, which the runtime can only know
    // from the recorded bindings.
    const lsfg::translate::SlotAssignment* const first
        = find(module, lsfg::translate::SlotKind::texture, 0);
    const lsfg::translate::SlotAssignment* const second
        = find(module, lsfg::translate::SlotKind::texture, 1);
    require(first != nullptr && second != nullptr, "both textures are recorded");
    require(first->spirv_binding == second->spirv_binding, "both name the same image");
    require(first->uses_dummy_sampler != second->uses_dummy_sampler,
            "exactly one of them uses the introduced sampler");
}

void test_unformatted_storage_image_is_given_a_format() {
    lsfg::translate::Options options;
    options.unformatted_storage_image_format = lsfg::spirv::ImageFormat::rgba8;

    lsfg::translate::Module module;
    require(
        lsfg::succeeded(lsfg::translate::to_glsl(
            module_bytes(lsfg::test::unformatted_module), options, module)),
        "the unformatted fixture translates");

    require(module.patch.images_formatted == 1, "the one unformatted image was given a format");
    require(module.glsl.find("rgba8") != std::string::npos, "the format reaches the GLSL");
}

void test_limits_are_refused_rather_than_exceeded() {
    lsfg::translate::Options options;
    options.limits.storage_images = 0;

    lsfg::translate::Module module;
    require(
        lsfg::translate::to_glsl(module_bytes(lsfg::test::formatted_module), options, module)
            == lsfg::ErrorCode::unsupported,
        "a module over the storage image limit is refused");

    options = lsfg::translate::Options{};
    options.limits.uniform_buffers = 0;
    require(
        lsfg::translate::to_glsl(module_bytes(lsfg::test::formatted_module), options, module)
            == lsfg::ErrorCode::unsupported,
        "a module over the uniform buffer limit is refused");
}

void test_rubbish_input_fails_closed() {
    lsfg::translate::Module module;
    const lsfg::translate::Options options;

    require(
        lsfg::translate::to_glsl({}, options, module) == lsfg::ErrorCode::invalid_argument,
        "an empty module is refused");

    const std::vector<std::uint8_t> not_spirv(64, 0xAB);
    require(
        lsfg::translate::to_glsl(not_spirv, options, module) == lsfg::ErrorCode::invalid_argument,
        "a module without the magic is refused");

    // A whole-word count is part of being a module at all.
    std::vector<std::uint8_t> truncated(
        reinterpret_cast<const std::uint8_t*>(lsfg::test::formatted_module),
        reinterpret_cast<const std::uint8_t*>(lsfg::test::formatted_module) + 41);
    require(
        lsfg::translate::to_glsl(truncated, options, module) == lsfg::ErrorCode::invalid_argument,
        "a module that is not a whole number of words is refused");

    // Declarations that parse but whose body has been destroyed must not reach
    // the caller as a half-translated module.
    std::vector<std::uint8_t> corrupt(
        reinterpret_cast<const std::uint8_t*>(lsfg::test::formatted_module),
        reinterpret_cast<const std::uint8_t*>(lsfg::test::formatted_module)
            + sizeof(lsfg::test::formatted_module));
    std::fill(corrupt.end() - 32, corrupt.end(), std::uint8_t{0xFF});
    const lsfg::ErrorCode result = lsfg::translate::to_glsl(corrupt, options, module);
    require(!lsfg::succeeded(result), "a corrupt body does not translate");
}

void test_dksh_rejects_what_is_not_a_module() {
    lsfg::dksh::ComputeProgram program;

    require(
        lsfg::dksh::validate({}, program) == lsfg::ErrorCode::cache_integrity_failure,
        "an empty blob is not a module");

    const std::vector<std::uint8_t> rubbish(512, 0x00);
    require(
        lsfg::dksh::validate(rubbish, program) == lsfg::ErrorCode::cache_integrity_failure,
        "zeroes are not a module");

    // A well-formed header whose sections do not add up to the blob must be
    // refused rather than read past.
    std::vector<std::uint8_t> header(512, 0x00);
    const lsfg::dksh::FileHeader lying{
        .magic = lsfg::dksh::magic,
        .header_size = sizeof(lsfg::dksh::FileHeader),
        .control_size = 256,
        .code_size = 4096,
        .programs_offset = sizeof(lsfg::dksh::FileHeader),
        .program_count = 1,
    };
    std::copy_n(reinterpret_cast<const std::uint8_t*>(&lying), sizeof(lying), header.begin());
    require(
        lsfg::dksh::validate(header, program) == lsfg::ErrorCode::cache_integrity_failure,
        "a blob shorter than its own header claims is refused");
}

void test_compiler_presence_is_reported_honestly() {
    lsfg::dksh::Blob blob;
    const lsfg::ErrorCode result = lsfg::dksh::compile("#version 450\nvoid main() {}\n", blob);

    if (lsfg::dksh::compiler_available()) {
        // Not a compute shader, so this must fail with diagnostics rather than
        // produce something.
        require(!lsfg::succeeded(result), "a shader with no compute entry point does not compile");
        require(blob.bytes.empty(), "a failed compile returns no blob");
    } else {
        require(result == lsfg::ErrorCode::unsupported, "a build without the compiler says so");
    }
}

void test_the_whole_stage_when_the_compiler_is_present() {
    if (!lsfg::dksh::compiler_available()) {
        return;
    }

    const lsfg::translate::Module module = translate(module_bytes(lsfg::test::formatted_module));

    lsfg::dksh::Blob blob;
    require(lsfg::succeeded(lsfg::dksh::compile(module.glsl, blob)), "the translation compiles");
    require(!blob.bytes.empty(), "the compile produced a blob");

    require(blob.program.block_dim_x == module.local_size_x, "the workgroup width survives DKSH");
    require(blob.program.block_dim_y == module.local_size_y, "the workgroup height survives DKSH");
    require(blob.program.block_dim_z == module.local_size_z, "the workgroup depth survives DKSH");

    lsfg::dksh::ComputeProgram checked;
    require(lsfg::succeeded(lsfg::dksh::validate(blob.bytes, checked)), "the blob validates");

    // The same source has to compile to the same bytes, or a cache keyed on
    // the inputs cannot be trusted.
    lsfg::dksh::Blob again;
    require(lsfg::succeeded(lsfg::dksh::compile(module.glsl, again)), "the second compile succeeds");
    require(again.bytes == blob.bytes, "compiling twice produces identical DKSH");
}

} // namespace

int main() {
    test_produces_desktop_glsl();
    test_a_declared_workgroup_size_is_not_a_specialised_one();
    test_slots_start_at_zero_in_each_space();
    test_unformatted_storage_image_is_given_a_format();
    test_limits_are_refused_rather_than_exceeded();
    test_rubbish_input_fails_closed();
    test_dksh_rejects_what_is_not_a_module();
    test_compiler_presence_is_reported_honestly();
    test_the_whole_stage_when_the_compiler_is_present();

    std::cout << "translation tests passed\n";
    return EXIT_SUCCESS;
}
