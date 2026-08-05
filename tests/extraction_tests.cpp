// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

// Every fixture here is synthesised. The checks must run on a machine that has
// never seen Lossless.dll.

#include <lsfg/common/cache_format.hpp>
#include <lsfg/common/pe_resources.hpp>
#include <lsfg/common/sha256.hpp>
#include <lsfg/common/shader_set.hpp>
#include <lsfg/common/spirv_module.hpp>

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

std::span<const std::uint8_t> as_bytes(const std::string_view text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

std::string hex(const lsfg::Digest& digest) {
    return std::string{lsfg::to_hex(digest).data()};
}

void test_sha256_vectors() {
    require(
        hex(lsfg::sha256(as_bytes("")))
            == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "SHA-256 of the empty input");
    require(
        hex(lsfg::sha256(as_bytes("abc")))
            == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 of \"abc\"");
    require(
        hex(lsfg::sha256(as_bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")))
            == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
        "SHA-256 of the two-block vector");

    const std::vector<std::uint8_t> million(1'000'000, static_cast<std::uint8_t>('a'));
    require(
        hex(lsfg::sha256(million))
            == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
        "SHA-256 of a million 'a'");

    // The same bytes fed in uneven pieces must produce the same digest.
    lsfg::Sha256 streamed;
    streamed.update(as_bytes("ab"));
    streamed.update(as_bytes("c"));
    require(hex(streamed.finish()) == hex(lsfg::sha256(as_bytes("abc"))), "streaming matches one shot");
}

void test_cache_key_fields() {
    const auto key = [](const std::string_view spirv_cross, const std::string_view uam) {
        return hex(lsfg::cache::cache_key(lsfg::cache::CacheKeyInputs{
            .dll_bytes = as_bytes("dll"),
            .extractor_version = 1,
            .spirv_cross_revision = spirv_cross,
            .uam_revision = uam,
            .translation_options = 0,
            .backend_abi_version = 1,
        }));
    };

    require(key("ab", "c") == key("ab", "c"), "the same inputs produce the same key");
    require(key("ab", "c") != key("a", "bc"), "field boundaries are part of the key");

    lsfg::cache::CacheKeyInputs inputs{
        .dll_bytes = as_bytes("dll"),
        .extractor_version = 1,
        .spirv_cross_revision = "rev",
        .uam_revision = "rev",
        .translation_options = 0,
        .backend_abi_version = 1,
    };
    const std::string original = hex(lsfg::cache::cache_key(inputs));
    inputs.extractor_version = 2;
    require(hex(lsfg::cache::cache_key(inputs)) != original, "a new extractor retires the cache");
    inputs.extractor_version = 1;
    inputs.translation_options = 1;
    require(hex(lsfg::cache::cache_key(inputs)) != original, "translation options change the key");
}

// A minimal PE32+ image with one section holding a resource tree of RCDATA
// entries, each in its own language directory.
struct ResourceInput {
    std::uint32_t id{};
    std::vector<std::uint8_t> data;
};

void put_u16(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
}

void put_u32(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

constexpr std::size_t pe_offset = 0x80;
constexpr std::size_t optional_offset = pe_offset + 24U;
constexpr std::uint16_t optional_size = 240;
constexpr std::size_t sections_offset = optional_offset + optional_size;
constexpr std::size_t section_data_offset = 0x400;
constexpr std::uint32_t section_rva = 0x1000;

std::vector<std::uint8_t> build_pe(const std::vector<ResourceInput>& resources) {
    const std::size_t count = resources.size();

    const std::size_t root_dir = 0;
    const std::size_t type_dir = root_dir + 16U + 8U;
    const std::size_t lang_dirs = type_dir + 16U + (8U * count);
    const std::size_t data_entries = lang_dirs + (count * 24U);
    std::size_t payloads = data_entries + (count * 16U);
    payloads = (payloads + 3U) & ~std::size_t{3U};

    std::size_t section_size = payloads;
    for (const ResourceInput& resource : resources) {
        section_size += (resource.data.size() + 3U) & ~std::size_t{3U};
    }

    std::vector<std::uint8_t> image(section_data_offset + section_size, 0);

    put_u16(image, 0, 0x5a4d);
    put_u32(image, 0x3c, pe_offset);
    put_u32(image, pe_offset, 0x0000'4550);
    put_u16(image, pe_offset + 6U, 1);              // section count
    put_u16(image, pe_offset + 20U, optional_size); // optional header size
    put_u16(image, optional_offset, 0x020b);        // PE32+
    put_u32(image, optional_offset + 108U, 16);     // data directory count
    put_u32(image, optional_offset + 112U + 16U, section_rva);
    put_u32(image, optional_offset + 112U + 20U, static_cast<std::uint32_t>(section_size));

    put_u32(image, sections_offset + 8U, static_cast<std::uint32_t>(section_size)); // virtual size
    put_u32(image, sections_offset + 12U, section_rva);
    put_u32(image, sections_offset + 16U, static_cast<std::uint32_t>(section_size)); // raw size
    put_u32(image, sections_offset + 20U, section_data_offset);

    const auto at = [](const std::size_t relative) { return section_data_offset + relative; };

    put_u16(image, at(root_dir) + 14U, 1); // one id entry
    put_u32(image, at(root_dir) + 16U, lsfg::pe::resource_type_rcdata);
    put_u32(image, at(root_dir) + 20U, static_cast<std::uint32_t>(type_dir) | 0x8000'0000U);

    put_u16(image, at(type_dir) + 14U, static_cast<std::uint16_t>(count));

    std::size_t payload = payloads;
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t lang_dir = lang_dirs + (index * 24U);
        const std::size_t data_entry = data_entries + (index * 16U);

        put_u32(image, at(type_dir) + 16U + (index * 8U), resources[index].id);
        put_u32(
            image,
            at(type_dir) + 20U + (index * 8U),
            static_cast<std::uint32_t>(lang_dir) | 0x8000'0000U);

        put_u16(image, at(lang_dir) + 14U, 1);
        put_u32(image, at(lang_dir) + 16U, 1033);
        put_u32(image, at(lang_dir) + 20U, static_cast<std::uint32_t>(data_entry));

        put_u32(image, at(data_entry), section_rva + static_cast<std::uint32_t>(payload));
        put_u32(image, at(data_entry) + 4U, static_cast<std::uint32_t>(resources[index].data.size()));

        for (std::size_t byte = 0; byte < resources[index].data.size(); ++byte) {
            image[at(payload) + byte] = resources[index].data[byte];
        }
        payload += (resources[index].data.size() + 3U) & ~std::size_t{3U};
    }

    return image;
}

void test_pe_enumeration() {
    const std::vector<ResourceInput> inputs{
        {.id = 7, .data = {1, 2, 3, 4}},
        {.id = 8, .data = {9, 9, 9}},
    };
    const std::vector<std::uint8_t> image = build_pe(inputs);

    lsfg::pe::ResourceTable table;
    require(lsfg::succeeded(lsfg::pe::enumerate_resources(image, table)), "a well-formed image parses");
    require(table.resources.size() == 2, "both resources are found");
    require(table.named_entries_skipped == 0, "no name-keyed entries in the fixture");
    require(table.resources[0].type == lsfg::pe::resource_type_rcdata, "type is carried through");
    require(table.resources[0].id == 7 && table.resources[1].id == 8, "ids are carried through");
    require(table.resources[0].language == 1033, "language is carried through");

    const std::span<const std::uint8_t> first = lsfg::pe::resource_data(image, table.resources[0]);
    require(first.size() == 4 && first[0] == 1 && first[3] == 4, "payload bytes round-trip");
    require(lsfg::pe::resource_data(image, table.resources[1]).size() == 3, "unpadded size is reported");
}

void test_pe_rejects_damage() {
    const std::vector<ResourceInput> inputs{{.id = 7, .data = {1, 2, 3, 4}}};
    lsfg::pe::ResourceTable table;

    std::vector<std::uint8_t> no_dos_header = build_pe(inputs);
    no_dos_header[0] = 'X';
    require(
        lsfg::pe::enumerate_resources(no_dos_header, table) == lsfg::ErrorCode::unsupported,
        "a missing DOS header is refused");

    std::vector<std::uint8_t> wrong_bitness = build_pe(inputs);
    put_u16(wrong_bitness, optional_offset, 0x0107);
    require(
        lsfg::pe::enumerate_resources(wrong_bitness, table) == lsfg::ErrorCode::unsupported,
        "an unknown optional header is refused");

    std::vector<std::uint8_t> truncated = build_pe(inputs);
    truncated.resize(truncated.size() - 8U);
    require(
        !lsfg::succeeded(lsfg::pe::enumerate_resources(truncated, table)),
        "a truncated section is refused");
    require(table.resources.empty(), "a refused image yields no resources");

    std::vector<std::uint8_t> outside = build_pe(inputs);
    const std::size_t data_entry = section_data_offset + 24U + 16U + 8U + 24U;
    put_u32(outside, data_entry + 4U, 0x0fff'ffffU);
    require(
        !lsfg::succeeded(lsfg::pe::enumerate_resources(outside, table)),
        "a payload reaching past the section is refused");

    std::vector<std::uint8_t> empty;
    require(
        lsfg::pe::enumerate_resources(empty, table) == lsfg::ErrorCode::unsupported,
        "an empty image is refused");
}

// SPIR-V fixtures. Ids are assigned by hand so the expectations stay readable.
class ModuleBuilder {
public:
    explicit ModuleBuilder(const std::uint32_t bound)
        : words_{lsfg::spirv::magic, 0x0001'0000U, 0, bound, 0} {}

    void op(const std::uint16_t opcode, const std::vector<std::uint32_t>& operands) {
        const auto count = static_cast<std::uint32_t>(operands.size() + 1U);
        words_.push_back((count << 16U) | opcode);
        words_.insert(words_.end(), operands.begin(), operands.end());
    }

    void op_with_string(
        const std::uint16_t opcode,
        const std::vector<std::uint32_t>& operands,
        const std::string_view text) {
        std::vector<std::uint32_t> all = operands;
        std::uint32_t word = 0;
        std::uint32_t shift = 0;
        for (const char character : text) {
            word |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(character)) << shift;
            shift += 8U;
            if (shift == 32U) {
                all.push_back(word);
                word = 0;
                shift = 0;
            }
        }
        all.push_back(word);
        op(opcode, all);
    }

    [[nodiscard]] const std::vector<std::uint32_t>& words() const { return words_; }
    [[nodiscard]] std::vector<std::uint32_t> copy() const { return words_; }

    [[nodiscard]] std::vector<std::uint8_t> bytes() const {
        std::vector<std::uint8_t> out(words_.size() * 4U);
        for (std::size_t index = 0; index < words_.size(); ++index) {
            out[index * 4U] = static_cast<std::uint8_t>(words_[index] & 0xffU);
            out[(index * 4U) + 1U] = static_cast<std::uint8_t>((words_[index] >> 8U) & 0xffU);
            out[(index * 4U) + 2U] = static_cast<std::uint8_t>((words_[index] >> 16U) & 0xffU);
            out[(index * 4U) + 3U] = static_cast<std::uint8_t>((words_[index] >> 24U) & 0xffU);
        }
        return out;
    }

private:
    std::vector<std::uint32_t> words_;
};

// Mirrors the shape LSFG uses: separate images and samplers, one uniform
// buffer, and storage images including an array and one with no format.
ModuleBuilder build_compute_module(const bool with_float16) {
    ModuleBuilder builder(40);

    builder.op(17, {1}); // OpCapability Shader
    if (with_float16) {
        builder.op(17, {9}); // OpCapability Float16
    }
    builder.op(17, {56}); // OpCapability StorageImageWriteWithoutFormat

    builder.op_with_string(15, {5, 4}, "main"); // OpEntryPoint GLCompute %4 "main"
    builder.op(16, {4, 17, 8, 8, 1});           // OpExecutionMode %4 LocalSize 8 8 1

    builder.op(71, {20, 34, 0}); // uniform buffer: set 0
    builder.op(71, {20, 33, 0}); // binding 0
    builder.op(71, {10, 2});     // OpDecorate %10 Block
    builder.op(71, {21, 34, 0});
    builder.op(71, {21, 33, 16}); // sampler at binding 16
    builder.op(71, {22, 34, 0});
    builder.op(71, {22, 33, 32}); // separate image at binding 32
    builder.op(71, {23, 34, 0});
    builder.op(71, {23, 33, 48}); // storage image array at binding 48
    builder.op(71, {24, 34, 0});
    builder.op(71, {24, 33, 52}); // unformatted storage image at binding 52

    builder.op(22, {5, 32});     // OpTypeFloat %5 32
    builder.op(30, {10, 5});     // OpTypeStruct %10 %5
    builder.op(26, {11});        // OpTypeSampler %11
    builder.op(25, {12, 5, 1, 0, 0, 0, 1, 0});  // sampled image, format Unknown
    builder.op(25, {13, 5, 1, 0, 0, 0, 2, 4});  // storage image, Rgba8
    builder.op(25, {14, 5, 1, 0, 0, 0, 2, 0});  // storage image, no format
    builder.op(21, {6, 32, 0});  // OpTypeInt %6 32 unsigned
    builder.op(43, {6, 7, 3});   // OpConstant %7 = 3
    builder.op(28, {15, 13, 7}); // OpTypeArray %15 of %13, length %7

    builder.op(32, {16, 2, 10});  // pointer, Uniform
    builder.op(32, {17, 0, 11});  // pointer, UniformConstant
    builder.op(32, {18, 0, 12});
    builder.op(32, {19, 0, 15});
    builder.op(32, {25, 0, 14});

    builder.op(59, {16, 20, 2});
    builder.op(59, {17, 21, 0});
    builder.op(59, {18, 22, 0});
    builder.op(59, {19, 23, 0});
    builder.op(59, {25, 24, 0});

    return builder;
}

void test_spirv_inventory() {
    const ModuleBuilder builder = build_compute_module(false);

    lsfg::spirv::Inventory inventory;
    require(lsfg::succeeded(lsfg::spirv::inspect(builder.words(), inventory)), "the module parses");
    require(inventory.entry_point == "main", "entry point name");
    require(
        inventory.execution_model == static_cast<std::uint32_t>(lsfg::spirv::ExecutionModel::gl_compute),
        "execution model");
    require(
        inventory.local_size_x == 8 && inventory.local_size_y == 8 && inventory.local_size_z == 1,
        "workgroup size");
    require(!inventory.local_size_is_specialised, "a literal workgroup size is not specialised");
    require(
        lsfg::spirv::has_capability(inventory, lsfg::spirv::Capability::shader),
        "declared capabilities are reported");
    require(
        !lsfg::spirv::has_capability(inventory, lsfg::spirv::Capability::float16),
        "undeclared capabilities are not reported");

    require(inventory.bindings.size() == 5, "every decorated variable is reported");
    require(inventory.bindings[0].binding == 0, "bindings are sorted");
    require(
        inventory.bindings[0].kind == lsfg::spirv::ResourceKind::uniform_buffer,
        "a Block struct in Uniform storage is a uniform buffer");
    require(inventory.bindings[1].kind == lsfg::spirv::ResourceKind::sampler, "sampler");
    require(
        inventory.bindings[2].kind == lsfg::spirv::ResourceKind::separate_image,
        "an image with Sampled=1 is a separate image");
    require(
        inventory.bindings[3].kind == lsfg::spirv::ResourceKind::storage_image
            && inventory.bindings[3].array_size == 3,
        "an array of storage images reports its length");
    require(
        inventory.bindings[3].image_format == static_cast<std::uint32_t>(lsfg::spirv::ImageFormat::rgba8),
        "the declared storage image format is reported");
    require(
        inventory.bindings[4].image_format == static_cast<std::uint32_t>(lsfg::spirv::ImageFormat::unknown),
        "an undeclared format is reported as unknown");

    const lsfg::spirv::DescriptorCounts counts = lsfg::spirv::count_descriptors(inventory);
    require(counts.uniform_buffers == 1, "uniform buffer count");
    require(counts.samplers == 1, "sampler count");
    require(counts.separate_images == 1, "separate image count");
    require(counts.storage_images == 4, "array slots count towards the storage image total");
    require(counts.highest_set == 0, "highest set");
    require(counts.highest_binding == 52, "an array occupies the slots it spans");
}

void test_spirv_rejects_damage() {
    lsfg::spirv::Inventory inventory;

    const std::vector<std::uint32_t> empty;
    require(
        lsfg::spirv::inspect(empty, inventory) == lsfg::ErrorCode::invalid_argument,
        "an empty module is refused");

    std::vector<std::uint32_t> swapped = build_compute_module(false).copy();
    swapped[0] = 0x0302'2307U;
    require(
        lsfg::spirv::inspect(swapped, inventory) == lsfg::ErrorCode::unsupported,
        "a byte-swapped module is refused rather than misread");

    std::vector<std::uint32_t> zero_length = build_compute_module(false).copy();
    zero_length[lsfg::spirv::header_words] = 0;
    require(
        lsfg::spirv::inspect(zero_length, inventory) == lsfg::ErrorCode::invalid_argument,
        "a zero-length instruction is refused instead of looping");

    std::vector<std::uint32_t> overrun = build_compute_module(false).copy();
    overrun[lsfg::spirv::header_words] = (0xffffU << 16U) | 17U;
    require(
        lsfg::spirv::inspect(overrun, inventory) == lsfg::ErrorCode::invalid_argument,
        "an instruction reaching past the module is refused");

    const std::vector<std::uint8_t> unaligned{0x03, 0x02, 0x23, 0x07, 0x00};
    require(
        lsfg::spirv::inspect_bytes(unaligned, inventory) == lsfg::ErrorCode::invalid_argument,
        "a payload that is not a whole number of words is refused");
}

void test_spirv_patch() {
    std::vector<std::uint32_t> words = build_compute_module(false).copy();

    lsfg::spirv::PatchSummary summary;
    require(
        lsfg::succeeded(lsfg::spirv::patch_storage_image_format(
            words, lsfg::spirv::ImageFormat::rgba8, summary)),
        "the patch applies");
    require(summary.capabilities_replaced == 1, "the write-without-format capability is replaced");
    require(summary.images_formatted == 1, "only the unformatted storage image is stamped");

    lsfg::spirv::Inventory inventory;
    require(lsfg::succeeded(lsfg::spirv::inspect(words, inventory)), "the patched module still parses");
    require(
        !lsfg::spirv::has_capability(
            inventory, lsfg::spirv::Capability::storage_image_write_without_format),
        "the capability is gone");
    require(
        inventory.bindings[4].image_format == static_cast<std::uint32_t>(lsfg::spirv::ImageFormat::rgba8),
        "the previously unformatted image now has a format");
    require(
        inventory.bindings[3].image_format == static_cast<std::uint32_t>(lsfg::spirv::ImageFormat::rgba8),
        "an already formatted image is left alone");

    lsfg::spirv::PatchSummary again;
    require(
        lsfg::succeeded(lsfg::spirv::patch_storage_image_format(
            words, lsfg::spirv::ImageFormat::rgba16f, again)),
        "the patch is idempotent");
    require(again.images_formatted == 0 && again.capabilities_replaced == 0, "nothing is left to patch");
}

std::vector<std::uint8_t> shader_bytes(const bool with_float16) {
    return build_compute_module(with_float16).bytes();
}

std::vector<ResourceInput> build_shader_blocks(
    const std::uint32_t first_id,
    const std::uint32_t block_size,
    const bool first_block_is_float16) {
    std::vector<ResourceInput> inputs;
    for (std::uint32_t index = 0; index < 2U * block_size; ++index) {
        const bool first_block = index < block_size;
        inputs.push_back(ResourceInput{
            .id = first_id + index,
            .data = shader_bytes(first_block == first_block_is_float16),
        });
    }
    return inputs;
}

void test_shader_set_identification() {
    constexpr std::uint32_t block_size = 30;
    const std::vector<std::uint8_t> image = build_pe(build_shader_blocks(303, block_size, true));

    lsfg::pe::ResourceTable table;
    require(lsfg::succeeded(lsfg::pe::enumerate_resources(image, table)), "the fixture parses");

    lsfg::shaders::ShaderSet set;
    require(lsfg::succeeded(lsfg::shaders::identify(image, table.resources, set)), "the set is recognised");
    require(set.first_resource_id == 303, "the first module anchors the set");
    require(set.block_size == block_size, "the block size is halved from the module count");
    require(set.low_precision_block == 0, "the block declaring Float16 is the low precision one");
    require(set.high_precision_block == 1, "the other block is the high precision one");

    require(
        lsfg::shaders::resource_id_for(set, lsfg::shaders::Precision::high, 1) == 303 + block_size + 1,
        "a high precision module is addressed through the second block");
    require(
        lsfg::shaders::resource_id_for(set, lsfg::shaders::Precision::low, 1) == 304,
        "a low precision module is addressed through the first block");

    const std::vector<std::uint8_t> flipped = build_pe(build_shader_blocks(303, block_size, false));
    lsfg::pe::ResourceTable flipped_table;
    require(
        lsfg::succeeded(lsfg::pe::enumerate_resources(flipped, flipped_table)),
        "the flipped fixture parses");
    lsfg::shaders::ShaderSet flipped_set;
    require(
        lsfg::succeeded(lsfg::shaders::identify(flipped, flipped_table.resources, flipped_set)),
        "block order is not assumed");
    require(flipped_set.low_precision_block == 1, "the precision blocks are detected, not positional");
}

void test_shader_set_refuses_unknown_layouts() {
    lsfg::pe::ResourceTable table;
    lsfg::shaders::ShaderSet set;

    const auto identify_of = [&](const std::vector<ResourceInput>& inputs) {
        const std::vector<std::uint8_t> image = build_pe(inputs);
        table = lsfg::pe::ResourceTable{};
        if (!lsfg::succeeded(lsfg::pe::enumerate_resources(image, table))) {
            return lsfg::ErrorCode::io_error;
        }
        return lsfg::shaders::identify(image, table.resources, set);
    };

    std::vector<ResourceInput> odd = build_shader_blocks(303, 30, true);
    odd.pop_back();
    require(identify_of(odd) == lsfg::ErrorCode::shader_set_unknown, "an odd module count is refused");

    std::vector<ResourceInput> gapped = build_shader_blocks(303, 30, true);
    gapped.back().id += 5;
    require(
        identify_of(gapped) == lsfg::ErrorCode::shader_set_unknown,
        "a non-contiguous id range is refused");

    const std::vector<ResourceInput> uniform = [] {
        std::vector<ResourceInput> inputs;
        for (std::uint32_t index = 0; index < 60U; ++index) {
            inputs.push_back(ResourceInput{.id = 303 + index, .data = shader_bytes(true)});
        }
        return inputs;
    }();
    require(
        identify_of(uniform) == lsfg::ErrorCode::shader_set_unknown,
        "two blocks of the same precision cannot be told apart and are refused");

    const std::vector<ResourceInput> too_small = build_shader_blocks(303, 10, true);
    require(
        identify_of(too_small) == lsfg::ErrorCode::shader_set_unknown,
        "a set too small to hold the chain is refused");

    const std::vector<ResourceInput> not_shaders{
        {.id = 1, .data = {1, 2, 3, 4}},
        {.id = 2, .data = {5, 6, 7, 8}},
    };
    require(
        identify_of(not_shaders) == lsfg::ErrorCode::shader_set_unknown,
        "a DLL with no SPIR-V is refused");
}

void test_chain_resolution() {
    constexpr std::uint32_t block_size = 49;
    lsfg::shaders::ShaderSet set{
        .first_resource_id = 303,
        .block_size = block_size,
        .low_precision_block = 0,
        .high_precision_block = 1,
    };

    std::vector<lsfg::shaders::ModuleRequest> requests;
    require(
        lsfg::succeeded(
            lsfg::shaders::required_modules(set, lsfg::shaders::Precision::high, false, requests)),
        "the chain resolves against a large enough set");
    require(
        requests.size() < lsfg::shaders::chain_slots().size(),
        "chain slots that share a module are compiled once");

    for (std::size_t index = 1; index < requests.size(); ++index) {
        require(
            requests[index].block_index > requests[index - 1U].block_index,
            "modules are ordered and unique");
    }
    for (const lsfg::shaders::ModuleRequest& request : requests) {
        require(
            request.resource_id == 303 + block_size + request.block_index,
            "resource ids follow the identified block layout");
    }

    std::vector<lsfg::shaders::ModuleRequest> performance;
    require(
        lsfg::succeeded(
            lsfg::shaders::required_modules(set, lsfg::shaders::Precision::high, true, performance)),
        "the performance chain resolves");
    require(performance.size() == requests.size(), "both presets need the same number of modules");
    require(
        performance.front().block_index == requests.front().block_index,
        "modules without a performance variant are shared");
    require(
        performance.back().block_index == requests.back().block_index + lsfg::shaders::performance_offset,
        "modules with a performance variant are offset");

    lsfg::shaders::ShaderSet small{
        .first_resource_id = 303,
        .block_size = 26,
        .low_precision_block = 0,
        .high_precision_block = 1,
    };
    std::vector<lsfg::shaders::ModuleRequest> refused;
    require(
        lsfg::shaders::required_modules(small, lsfg::shaders::Precision::high, true, refused)
            == lsfg::ErrorCode::shader_set_unknown,
        "a set with no room for the performance variants is refused");
}

} // namespace

int main() {
    test_sha256_vectors();
    test_cache_key_fields();
    test_pe_enumeration();
    test_pe_rejects_damage();
    test_spirv_inventory();
    test_spirv_rejects_damage();
    test_spirv_patch();
    test_shader_set_identification();
    test_shader_set_refuses_unknown_layouts();
    test_chain_resolution();
    std::cout << "All extraction checks passed\n";
    return EXIT_SUCCESS;
}
