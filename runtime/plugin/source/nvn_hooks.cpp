// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "nvn_hooks.hpp"

#include "imports.hpp"
#include "nvn_api.hpp"
#include "observations.hpp"
#include "report.hpp"

#include <lsfg/instrument/presentation.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>

namespace lsfg::plugin::nvn {
namespace {

using instrument::EventKind;

// The entry point every game reaches this API through.
constexpr const char* bootstrap_symbol = "nvnBootstrapLoader";
constexpr const char* proc_address_name = "nvnDeviceGetProcAddress";

constexpr std::uint32_t vblank_us = 16'667;
constexpr std::uint32_t default_tolerance_us = 4000;

constexpr std::uint16_t trace_per_kind = 16;

enum class Fn : std::size_t {
    texture_initialize,
    window_builder_set_textures,
    window_initialize,
    window_set_present_interval,
    window_set_num_active_textures,
    window_acquire_texture,
    queue_present_texture,
    queue_submit_commands,
    queue_fence_sync,
    queue_finish,
    sync_wait,
    count,
};

std::array<void*, static_cast<std::size_t>(Fn::count)> g_chain{};

template <typename T>
[[nodiscard]] T chained(const Fn function) noexcept {
    return reinterpret_cast<T>(g_chain[static_cast<std::size_t>(function)]);
}

struct Queries {
    TextureBuilderGetPool pool{};
    TextureBuilderGetOffset offset{};
    TextureBuilderGetSize size{};
    TextureBuilderGetInt width{};
    TextureBuilderGetInt height{};
    TextureBuilderGetInt depth{};
    TextureBuilderGetInt levels{};
    TextureBuilderGetInt samples{};
    TextureBuilderGetInt format{};
    TextureBuilderGetInt flags{};
    TextureBuilderGetInt target{};
    TextureBuilderGetStride stride{};
    TextureBuilderGetSize alignment{};
    TextureBuilderGetInt storage_class{};
    TextureGetAddress texture_address{};
    MemoryPoolGetBufferAddress pool_address{};
    MemoryPoolMap pool_map{};
    MemoryPoolGetSize pool_size{};
    MemoryPoolGetFlags pool_flags{};
    WindowGetPresentInterval present_interval{};
    WindowGetNumActiveTextures active_textures{};
    // Which of the above the game's own copy of the API does not export.
    std::uint32_t missing{};
    bool attempted{};
};

Options g_options{};
Queries g_queries{};
BootstrapLoader g_bootstrap{};
DeviceGetProcAddress g_proc_address{};
std::atomic<bool> g_engaged{false};

struct StagedTextures {
    const WindowBuilder* builder{};
    std::array<std::uint64_t, instrument::SwapchainMap::capacity> handles{};
    std::uint32_t count{};
    bool overflowed{};
};

StagedTextures g_staged{};
std::array<std::uint16_t, 16> g_traced{};

[[nodiscard]] std::uint64_t handle_of(const void* const object) noexcept {
    return reinterpret_cast<std::uintptr_t>(object);
}

// Callers hold the observation lock.
[[nodiscard]] bool should_trace(const EventKind kind) noexcept {
    const auto index = static_cast<std::size_t>(kind);
    if (index >= g_traced.size() || g_traced[index] >= trace_per_kind) {
        return false;
    }
    ++g_traced[index];
    return true;
}

void trace(const EventKind kind, const std::uint64_t object, const std::uint64_t detail) noexcept {
    const Observe observe;
    if (should_trace(kind)) {
        observe->trace.push(kind, now(), object, detail);
    }
}

int texture_initialize(Texture* const texture, const TextureBuilder* const builder) noexcept {
    const auto original = chained<TextureInitialize>(Fn::texture_initialize);
    const int result = original(texture, builder);
    if (result == 0 || builder == nullptr) {
        return result;
    }

    instrument::TextureRecord record{};
    record.handle = handle_of(texture);

    MemoryPool* const pool = g_queries.pool != nullptr ? g_queries.pool(builder) : nullptr;
    record.memory_pool = handle_of(pool);
    if (pool != nullptr && g_queries.pool_address != nullptr) {
        record.pool_address = g_queries.pool_address(pool);
    }
    if (pool != nullptr && g_queries.pool_map != nullptr) {
        record.pool_cpu_address = handle_of(g_queries.pool_map(pool));
    }
    if (pool != nullptr && g_queries.pool_size != nullptr) {
        record.pool_size = g_queries.pool_size(pool);
    }
    if (pool != nullptr && g_queries.pool_flags != nullptr) {
        record.pool_flags = static_cast<std::uint32_t>(g_queries.pool_flags(pool));
    }
    if (g_queries.texture_address != nullptr) {
        record.texture_address = g_queries.texture_address(texture);
    }
    if (g_queries.offset != nullptr) {
        record.storage_offset = static_cast<std::uint64_t>(g_queries.offset(builder));
    }
    if (g_queries.size != nullptr) {
        record.storage_size = g_queries.size(builder);
    }
    if (g_queries.stride != nullptr) {
        record.stride = static_cast<std::uint32_t>(g_queries.stride(builder));
    }
    if (g_queries.alignment != nullptr) {
        record.storage_alignment = g_queries.alignment(builder);
    }

    const auto read = [builder](const TextureBuilderGetInt query) noexcept {
        return query != nullptr ? static_cast<std::uint32_t>(query(builder)) : 0U;
    };
    record.width = read(g_queries.width);
    record.height = read(g_queries.height);
    record.depth = read(g_queries.depth);
    record.levels = read(g_queries.levels);
    record.samples = read(g_queries.samples);
    record.format = read(g_queries.format);
    record.flags = read(g_queries.flags);
    record.target = read(g_queries.target);
    record.storage_class = read(g_queries.storage_class);

    const Observe observe;
    observe->textures.record(record);
    return result;
}

void window_builder_set_textures(
    WindowBuilder* const builder, const int count, Texture* const* const textures) noexcept {
    const auto original
        = chained<WindowBuilderSetTextures>(Fn::window_builder_set_textures);
    original(builder, count, textures);

    const Observe observe;
    g_staged = StagedTextures{};
    g_staged.builder = builder;
    if (count > 0 && textures != nullptr) {
        const auto requested = static_cast<std::uint32_t>(count);
        g_staged.overflowed = requested > instrument::SwapchainMap::capacity;
        g_staged.count = g_staged.overflowed
            ? static_cast<std::uint32_t>(instrument::SwapchainMap::capacity)
            : requested;
        for (std::uint32_t index = 0; index < g_staged.count; ++index) {
            g_staged.handles[index] = handle_of(textures[index]);
        }
    }

    if (should_trace(EventKind::window_builder_set_textures)) {
        observe->trace.push(EventKind::window_builder_set_textures, now(), handle_of(builder),
            static_cast<std::uint64_t>(count));
    }
}

int window_initialize(Window* const window, const WindowBuilder* const builder) noexcept {
    const auto original = chained<WindowInitialize>(Fn::window_initialize);
    const int result = original(window, builder);

    trace(EventKind::window_initialize, handle_of(window), static_cast<std::uint64_t>(result));
    if (result == 0) {
        return result;
    }

    instrument::SwapchainMap adopted{};
    bool mapped = false;
    {
        const Observe observe;
        ++observe->window_initializations;

        if (g_staged.builder != builder || g_staged.count == 0 || g_staged.overflowed) {
            return result;
        }

        if (observe->swapchain.window != 0) {
            ++observe->swapchain_replacements;
        }

        mapped = observe->swapchain.adopt(
            handle_of(window), {g_staged.handles.data(), g_staged.count}, observe->textures);
        if (mapped) {
            if (g_queries.present_interval != nullptr) {
                observe->swapchain.present_interval
                    = static_cast<std::uint8_t>(g_queries.present_interval(window));
            }
            if (g_queries.active_textures != nullptr) {
                observe->swapchain.active_textures
                    = static_cast<std::uint8_t>(g_queries.active_textures(window));
            }
            observe->timeline.reset_intervals();
            adopted = observe->swapchain;
        }
    }

    if (mapped) {
        report::on_swapchain(adopted);
    }
    return result;
}

void window_set_present_interval(Window* const window, const int interval) noexcept {
    const auto original
        = chained<WindowSetPresentInterval>(Fn::window_set_present_interval);
    original(window, interval);

    const Observe observe;
    ++observe->interval_changes;
    if (observe->swapchain.window == handle_of(window) || observe->swapchain.window == 0) {
        observe->swapchain.present_interval = static_cast<std::uint8_t>(interval);
    }

    if (g_options.expected_interval_us == 0 && interval > 0) {
        observe->timeline.set_expected_interval_us(
            static_cast<std::uint32_t>(interval) * vblank_us,
            g_options.interval_tolerance_us != 0 ? g_options.interval_tolerance_us
                                                 : default_tolerance_us);
    }
    observe->timeline.reset_intervals();

    if (should_trace(EventKind::window_set_present_interval)) {
        observe->trace.push(EventKind::window_set_present_interval, now(), handle_of(window),
            static_cast<std::uint64_t>(interval));
    }
}

void window_set_num_active_textures(Window* const window, const int count) noexcept {
    const auto original
        = chained<WindowSetNumActiveTextures>(Fn::window_set_num_active_textures);
    original(window, count);

    const Observe observe;
    ++observe->active_texture_changes;
    if (observe->swapchain.window == handle_of(window) || observe->swapchain.window == 0) {
        observe->swapchain.active_textures = static_cast<std::uint8_t>(count);
    }
    if (should_trace(EventKind::window_set_num_active_textures)) {
        observe->trace.push(EventKind::window_set_num_active_textures, now(), handle_of(window),
            static_cast<std::uint64_t>(count));
    }
}

int window_acquire_texture(
    Window* const window, Sync* const sync, int* const texture_index) noexcept {
    const std::uint64_t begin = now();
    {
        const Observe observe;
        observe->timeline.on_acquire_begin(begin);
    }

    const auto original = chained<WindowAcquireTexture>(Fn::window_acquire_texture);
    const int result = original(window, sync, texture_index);

    const std::int32_t index
        = (result == 0 && texture_index != nullptr) ? *texture_index : -1;

    const Observe observe;
    observe->timeline.on_acquire_end(now(), index);
    if (should_trace(EventKind::window_acquire_texture)) {
        observe->trace.push(EventKind::window_acquire_texture, begin, handle_of(window),
            static_cast<std::uint64_t>(static_cast<std::int64_t>(index)));
    }
    return result;
}

void queue_present_texture(
    Queue* const queue, Window* const window, const int texture_index) noexcept {
    const std::uint64_t tick = now();
    {
        const Observe observe;
        if (observe->swapchain.window != 0 && observe->swapchain.window != handle_of(window)) {
            ++observe->foreign_window_presents;
        } else {
            observe->timeline.on_present(tick, texture_index);
        }
        if (should_trace(EventKind::queue_present_texture)) {
            observe->trace.push(EventKind::queue_present_texture, tick, handle_of(window),
                static_cast<std::uint64_t>(texture_index));
        }
    }

    const auto original = chained<QueuePresentTexture>(Fn::queue_present_texture);
    original(queue, window, texture_index);

    report::on_present();
}

void queue_submit_commands(
    Queue* const queue, const int count, const std::uint64_t* const handles) noexcept {
    const auto original = chained<QueueSubmitCommands>(Fn::queue_submit_commands);
    original(queue, count, handles);

    const Observe observe;
    observe->timeline.on_submit(now());
    if (should_trace(EventKind::queue_submit_commands)) {
        observe->trace.push(EventKind::queue_submit_commands, now(), handle_of(queue),
            static_cast<std::uint64_t>(count));
    }
}

void queue_fence_sync(
    Queue* const queue, Sync* const sync, const int condition, const int flags) noexcept {
    const auto original = chained<QueueFenceSync>(Fn::queue_fence_sync);
    original(queue, sync, condition, flags);

    const Observe observe;
    observe->timeline.on_fence(now());
    if (should_trace(EventKind::queue_fence_sync)) {
        observe->trace.push(EventKind::queue_fence_sync, now(), handle_of(sync),
            static_cast<std::uint64_t>(condition));
    }
}

void queue_finish(Queue* const queue) noexcept {
    const auto original = chained<QueueFinish>(Fn::queue_finish);
    original(queue);
    trace(EventKind::queue_finish, handle_of(queue), 0);
}

int sync_wait(Sync* const sync, const std::uint64_t timeout_ns) noexcept {
    const std::uint64_t begin = now();
    const auto original = chained<SyncWait>(Fn::sync_wait);
    const int result = original(sync, timeout_ns);
    const std::uint64_t end = now();

    const Observe observe;
    observe->timeline.on_sync_wait(begin, end);
    if (should_trace(EventKind::sync_wait)) {
        observe->trace.push(
            EventKind::sync_wait, begin, handle_of(sync), static_cast<std::uint64_t>(result));
    }
    return result;
}

struct Binding {
    const char* name;
    Fn function;
    void* hook;
};

const std::array<Binding, static_cast<std::size_t>(Fn::count)> g_bindings{
    Binding{"nvnTextureInitialize", Fn::texture_initialize,
        reinterpret_cast<void*>(&texture_initialize)},
    Binding{"nvnWindowBuilderSetTextures", Fn::window_builder_set_textures,
        reinterpret_cast<void*>(&window_builder_set_textures)},
    Binding{"nvnWindowInitialize", Fn::window_initialize,
        reinterpret_cast<void*>(&window_initialize)},
    Binding{"nvnWindowSetPresentInterval", Fn::window_set_present_interval,
        reinterpret_cast<void*>(&window_set_present_interval)},
    Binding{"nvnWindowSetNumActiveTextures", Fn::window_set_num_active_textures,
        reinterpret_cast<void*>(&window_set_num_active_textures)},
    Binding{"nvnWindowAcquireTexture", Fn::window_acquire_texture,
        reinterpret_cast<void*>(&window_acquire_texture)},
    Binding{"nvnQueuePresentTexture", Fn::queue_present_texture,
        reinterpret_cast<void*>(&queue_present_texture)},
    Binding{"nvnQueueSubmitCommands", Fn::queue_submit_commands,
        reinterpret_cast<void*>(&queue_submit_commands)},
    Binding{"nvnQueueFenceSync", Fn::queue_fence_sync,
        reinterpret_cast<void*>(&queue_fence_sync)},
    Binding{"nvnQueueFinish", Fn::queue_finish, reinterpret_cast<void*>(&queue_finish)},
    Binding{"nvnSyncWait", Fn::sync_wait, reinterpret_cast<void*>(&sync_wait)},
};

template <typename T>
void resolve(Device* const device, const char* const name, T& out) noexcept {
    void* const address = g_proc_address(device, name);
    if (address == nullptr) {
        ++g_queries.missing;
        return;
    }
    out = reinterpret_cast<T>(address);
}

void resolve_queries(Device* const device) noexcept {
    g_queries.attempted = true;

    resolve(device, "nvnTextureBuilderGetMemoryPool", g_queries.pool);
    resolve(device, "nvnTextureBuilderGetMemoryOffset", g_queries.offset);
    resolve(device, "nvnTextureBuilderGetStorageSize", g_queries.size);
    resolve(device, "nvnTextureBuilderGetWidth", g_queries.width);
    resolve(device, "nvnTextureBuilderGetHeight", g_queries.height);
    resolve(device, "nvnTextureBuilderGetDepth", g_queries.depth);
    resolve(device, "nvnTextureBuilderGetLevels", g_queries.levels);
    resolve(device, "nvnTextureBuilderGetSamples", g_queries.samples);
    resolve(device, "nvnTextureBuilderGetFormat", g_queries.format);
    resolve(device, "nvnTextureBuilderGetFlags", g_queries.flags);
    resolve(device, "nvnTextureBuilderGetTarget", g_queries.target);
    resolve(device, "nvnTextureBuilderGetStride", g_queries.stride);
    resolve(device, "nvnTextureBuilderGetStorageAlignment", g_queries.alignment);
    resolve(device, "nvnTextureBuilderGetStorageClass", g_queries.storage_class);
    resolve(device, "nvnTextureGetTextureAddress", g_queries.texture_address);
    resolve(device, "nvnMemoryPoolGetBufferAddress", g_queries.pool_address);
    resolve(device, "nvnMemoryPoolMap", g_queries.pool_map);
    resolve(device, "nvnMemoryPoolGetSize", g_queries.pool_size);
    resolve(device, "nvnMemoryPoolGetFlags", g_queries.pool_flags);
    resolve(device, "nvnWindowGetPresentInterval", g_queries.present_interval);
    resolve(device, "nvnWindowGetNumActiveTextures", g_queries.active_textures);

    report::on_queries_resolved(g_queries.missing);
}

void* device_get_proc_address(Device* const device, const char* const name) noexcept {
    void* const address = g_proc_address(device, name);
    if (name == nullptr) {
        return address;
    }

    if (!g_queries.attempted && device != nullptr) {
        resolve_queries(device);
    }

    if (std::strcmp(name, proc_address_name) == 0) {
        return reinterpret_cast<void*>(&device_get_proc_address);
    }

    for (const Binding& binding : g_bindings) {
        if (std::strcmp(name, binding.name) != 0) {
            continue;
        }
        if (address == nullptr) {
            return address;
        }

        void*& slot = g_chain[static_cast<std::size_t>(binding.function)];
        if (slot == nullptr) {
            slot = address;
        }
        trace(EventKind::device_get_proc_address, handle_of(device), handle_of(address));
        return binding.hook;
    }

    return address;
}

void* bootstrap_loader(const char* const name) noexcept {
    void* const address = g_bootstrap(name);
    g_engaged.store(true, std::memory_order_release);

    if (name == nullptr || address == nullptr || std::strcmp(name, proc_address_name) != 0) {
        return address;
    }

    g_proc_address = reinterpret_cast<DeviceGetProcAddress>(address);
    trace(EventKind::bootstrap_loader, 0, handle_of(address));
    return reinterpret_cast<void*>(&device_get_proc_address);
}

} // namespace

ErrorCode install(const Options& options) noexcept {
    g_options = options;
    report::configure(
        options.reporting_enabled, options.verbose_trace, options.report_every);

    if (options.expected_interval_us != 0) {
        const Observe observe;
        observe->timeline.set_expected_interval_us(options.expected_interval_us,
            options.interval_tolerance_us != 0 ? options.interval_tolerance_us
                                               : default_tolerance_us);
    }

    const imports::Sites sites = imports::find(bootstrap_symbol);
    if (sites.empty() || sites.overflow != 0) {
        return ErrorCode::hook_install_failed;
    }

    void* const current = imports::current_target(sites);
    if (current == nullptr) {
        return ErrorCode::hook_install_failed;
    }

    g_bootstrap = reinterpret_cast<BootstrapLoader>(current);
    imports::redirect(sites, reinterpret_cast<void*>(&bootstrap_loader));
    return ErrorCode::ok;
}

bool engaged() noexcept {
    return g_engaged.load(std::memory_order_acquire);
}

} // namespace lsfg::plugin::nvn
