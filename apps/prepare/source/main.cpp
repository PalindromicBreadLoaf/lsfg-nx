// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/common/ring_log.hpp>

#include <switch.h>

#include <cstdio>

namespace {

constexpr const char* dll_path = "sdmc:/switch/lsfg-nx/Lossless.dll";

} // namespace

int main() {
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    PadState pad{};
    padInitializeDefault(&pad);

    lsfg::RingLog log;
    log.push(0, lsfg::LogLevel::info, lsfg::ErrorCode::ok, "preparation stub started");

    std::printf("LSFG-NX preparation app\n\n");
    std::printf("Expected input:\n%s\n\n", dll_path);
    std::printf("Shader extraction is not implemented yet.\n");
    std::printf("Press + to exit.\n");

    while (appletMainLoop()) {
        padUpdate(&pad);
        if ((padGetButtonsDown(&pad) & HidNpadButton_Plus) != 0U) {
            break;
        }
        consoleUpdate(nullptr);
    }

    consoleExit(nullptr);
    return 0;
}

