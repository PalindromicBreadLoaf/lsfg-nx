// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/common/ring_log.hpp>

#include <switch.h>

#include <cstdio>

int main() {
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    PadState pad{};
    padInitializeDefault(&pad);

    lsfg::RingLog log;
    log.push(0, lsfg::LogLevel::info, lsfg::ErrorCode::ok, "test-pattern stub started");

    std::printf("LSFG-NX test pattern\n\n");
    std::printf("The deko3d harness is not implemented yet.\n");
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
