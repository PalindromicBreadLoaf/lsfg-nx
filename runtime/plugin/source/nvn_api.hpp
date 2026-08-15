// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>

// The part of the nvn graphics API this plugin touches.
namespace lsfg::plugin::nvn {

struct Device;
struct Queue;
struct Window;
struct WindowBuilder;
struct Texture;
struct TextureBuilder;
struct MemoryPool;
struct Sync;

using BootstrapLoader = void* (*)(const char* name);
using DeviceGetProcAddress = void* (*)(Device* device, const char* name);

using TextureInitialize = int (*)(Texture* texture, const TextureBuilder* builder);
using TextureBuilderGetPool = MemoryPool* (*)(const TextureBuilder* builder);
using TextureBuilderGetOffset = std::int64_t (*)(const TextureBuilder* builder);
using TextureBuilderGetSize = std::size_t (*)(const TextureBuilder* builder);
using TextureBuilderGetInt = int (*)(const TextureBuilder* builder);
using TextureBuilderGetStride = std::int64_t (*)(const TextureBuilder* builder);
using TextureGetAddress = std::uint64_t (*)(const Texture* texture);

using MemoryPoolGetBufferAddress = std::uint64_t (*)(const MemoryPool* pool);
using MemoryPoolMap = void* (*)(const MemoryPool* pool);
using MemoryPoolGetSize = std::size_t (*)(const MemoryPool* pool);
using MemoryPoolGetFlags = int (*)(const MemoryPool* pool);

using WindowBuilderSetTextures
    = void (*)(WindowBuilder* builder, int count, Texture* const* textures);
using WindowInitialize = int (*)(Window* window, const WindowBuilder* builder);
using WindowSetPresentInterval = void (*)(Window* window, int interval);
using WindowGetPresentInterval = int (*)(const Window* window);
using WindowSetNumActiveTextures = void (*)(Window* window, int count);
using WindowGetNumActiveTextures = int (*)(const Window* window);
using WindowAcquireTexture = int (*)(Window* window, Sync* sync, int* texture_index);

using QueuePresentTexture = void (*)(Queue* queue, Window* window, int texture_index);
using QueueSubmitCommands = void (*)(Queue* queue, int count, const std::uint64_t* handles);
using QueueFinish = void (*)(Queue* queue);
using QueueFenceSync = void (*)(Queue* queue, Sync* sync, int condition, int flags);
using SyncWait = int (*)(Sync* sync, std::uint64_t timeout_ns);

} // namespace lsfg::plugin::nvn
