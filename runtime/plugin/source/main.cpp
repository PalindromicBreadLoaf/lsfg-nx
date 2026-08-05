// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/common/version.hpp>

#include <switch.h>

#include <saltysd_core.h>
#include <saltysd_ipc.h>
#include <useful.h>

// The plugin runs inside a retail game. It installs no hooks, allocates no GPU
// resources, and returns immediately.

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

} // namespace

extern "C" void __libnx_init(void* ctx, const Handle main_thread, void* saved_lr) {
    extern char* fake_heap_start;
    extern char* fake_heap_end;

    fake_heap_start = &g_heap[0];
    fake_heap_end = &g_heap[sizeof g_heap];

    g_context = ctx;
    g_main_thread = main_thread;
    g_saved_lr = saved_lr;

    __libc_init_array();
    virtmemSetup();
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
    SaltySDCore_printf("lsfg-nx: passthrough mode, no hooks installed\n");

    return 0;
}
