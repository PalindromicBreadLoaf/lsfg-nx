// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "deferred.hpp"
#include "imports.hpp"
#include "nvn_hooks.hpp"
#include "report.hpp"

#include <lsfg/common/error.hpp>
#include <lsfg/common/profile.hpp>
#include <lsfg/common/version.hpp>

#include <switch.h>

#include <saltysd_core.h>
#include <saltysd_ipc.h>
#include <useful.h>

#include <array>
#include <cstdint>
#include <string_view>

extern "C" {

u32 __nx_applet_type = AppletType_None;

void __libnx_init(void* ctx, Handle main_thread, void* saved_lr);

void __libc_init_array();

// Internal to libnx and deliberately absent from its headers, but required
// before any allocation that maps address space.
void virtmemSetup();

} // extern "C"

namespace {

// The game owns the process heap. A fixed private arena keeps the plugin from
// perturbing the allocator the game is already using.
char g_heap[0x1000];

void* g_context = nullptr;
Handle g_main_thread = 0;
void* g_saved_lr = nullptr;

constexpr std::string_view profile_root = "sdmc:/SaltySD/plugins/lsfg-nx/profiles";
constexpr const char* log_flag = "sdmc:/SaltySD/flags/log.flag";
constexpr const char* trace_flag = "sdmc:/switch/lsfg-nx/trace.flag";
constexpr const char* permissive_flag = "sdmc:/switch/lsfg-nx/permissive.flag";
constexpr const char* imports_dump = "sdmc:/switch/lsfg-nx/imports.txt";

// Presents between pacing reports while tracing, which is a minute of a title
// running at thirty frames a second.
constexpr std::uint32_t trace_report_period = 1800;

std::array<char, lsfg::profile::max_text_size> g_profile_text{};
lsfg::profile::Profile g_profile{};
bool g_reporting_enabled{};
bool g_verbose_trace{};

[[nodiscard]] bool flag_present(const char* const path) noexcept {
    FILE* const file = SaltySDCore_fopen(path, "r");
    if (file == nullptr) {
        return false;
    }
    SaltySDCore_fclose(file);
    return true;
}

[[nodiscard]] lsfg::ErrorCode load_profile(const std::uint64_t title_id) noexcept {
    std::array<char, 128> path{};
    if (!lsfg::profile::path_for(profile_root, title_id, path)) {
        return lsfg::ErrorCode::invalid_argument;
    }

    FILE* const file = SaltySDCore_fopen(path.data(), "rb");
    if (file == nullptr) {
        return lsfg::ErrorCode::cache_missing;
    }

    const std::size_t read
        = SaltySDCore_fread(g_profile_text.data(), 1, g_profile_text.size(), file);
    SaltySDCore_fclose(file);

    return lsfg::profile::parse({g_profile_text.data(), read}, g_profile);
}

// What this title imports, and how.
void report_import_survey() {
    const lsfg::plugin::imports::Survey survey = lsfg::plugin::imports::survey();
    SaltySDCore_printf(
        "lsfg-nx: %u modules, %u symbol relocations: %u glob_dat, %u jump_slot, %u other\n",
        static_cast<unsigned>(survey.modules),
        static_cast<unsigned>(survey.symbol_relocations),
        static_cast<unsigned>(survey.glob_dat),
        static_cast<unsigned>(survey.jump_slot),
        static_cast<unsigned>(survey.other_kind));

    for (std::size_t index = 0; index < survey.named; ++index) {
        SaltySDCore_printf("lsfg-nx: glob_dat import: %s\n", survey.names[index]);
    }

    for (const char* const symbol : {"nvnBootstrapLoader", "_ZN2nn2oe16GetOperationModeEv"}) {
        const lsfg::plugin::imports::Sites sites = lsfg::plugin::imports::find(symbol);
        SaltySDCore_printf(
            "lsfg-nx: %s: %u sites, %u glob_dat, %u jump_slot, %u other\n",
            symbol,
            static_cast<unsigned>(sites.count),
            static_cast<unsigned>(sites.glob_dat),
            static_cast<unsigned>(sites.jump_slot),
            static_cast<unsigned>(sites.other_kind));
    }

    if (flag_present(imports_dump)) {
        SaltySDCore_printf("lsfg-nx: import table already written\n");
    } else {
        SaltySDCore_printf(
            "lsfg-nx: import table %s\n",
            lsfg::plugin::imports::dump(imports_dump) ? "written" : "not written");
    }
}

void install_hooks() {
    lsfg::plugin::nvn::Options options{};
    options.reporting_enabled = g_reporting_enabled;
    options.verbose_trace = g_verbose_trace;
    options.report_every = options.verbose_trace ? trace_report_period : 0;

    const lsfg::ErrorCode result = lsfg::plugin::nvn::install(options);
    lsfg::plugin::report::on_install(result);
}

} // namespace

extern "C" void __libnx_init(void* ctx, const Handle main_thread, void* saved_lr) {
    extern char* fake_heap_start;
    extern char* fake_heap_end;

    fake_heap_start = &g_heap[0];
    fake_heap_end = &g_heap[sizeof g_heap];

    g_context = ctx;
    g_main_thread = main_thread;
    g_saved_lr = saved_lr;

    envSetup(nullptr, main_thread, reinterpret_cast<LoaderReturnFn>(saved_lr));
    virtmemSetup();
    __libc_init_array();
}

int main() {
    u64 title_id = 0;
    svcGetInfo(&title_id, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0);
    const u64 build_id = SaltySD_GetBID();

    SaltySDCore_printf(
        "lsfg-nx: loaded (%s%s) tid=%016lX bid=%016lX\n",
        lsfg::version::git_revision.data(),
        lsfg::version::dirty ? "-dirty" : "",
        title_id,
        build_id);

    const lsfg::ErrorCode loaded = load_profile(title_id);
    if (!lsfg::succeeded(loaded)) {
        SaltySDCore_printf(
            "lsfg-nx: no usable profile for this title (%s), staying out of the way\n",
            lsfg::error_name(loaded).data());
        return 0;
    }

    const bool permissive = flag_present(permissive_flag);
    const auto targeting = permissive ? lsfg::profile::Targeting::permissive
                                      : lsfg::profile::Targeting::strict;

    const lsfg::ErrorCode allowed
        = lsfg::profile::check(g_profile, title_id, build_id, targeting);
    if (!lsfg::succeeded(allowed)) {
        SaltySDCore_printf(
            "lsfg-nx: %s refused this build (%s), staying out of the way\n",
            permissive ? "permissive targeting" : "strict targeting",
            lsfg::error_name(allowed).data());
        return 0;
    }

    SaltySDCore_printf(
        "lsfg-nx: %s allowed, %u recorded builds, generation %s\n",
        g_profile.name.data(),
        static_cast<unsigned>(g_profile.build_count),
        g_profile.supported ? "supported" : "unsupported");

    g_reporting_enabled = flag_present(log_flag);
    g_verbose_trace = flag_present(trace_flag);

    if (g_reporting_enabled && !lsfg::plugin::report::prepare_shared_transport()) {
        SaltySDCore_printf("lsfg-nx: shared report transport unavailable\n");
        g_reporting_enabled = false;
    } else if (g_reporting_enabled) {
        SaltySDCore_printf("lsfg-nx: shared report transport ready\n");
    }

    report_import_survey();

    if (!lsfg::plugin::deferred::arm(&install_hooks)) {
        SaltySDCore_printf("lsfg-nx: no deferred install point in this title\n");
        return 0;
    }

    return 0;
}
