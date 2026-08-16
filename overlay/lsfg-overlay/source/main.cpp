// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/common/protocol.hpp>
#include <lsfg/common/version.hpp>

#define TESLA_INIT_IMPL
#include <tesla.hpp>

#include <array>
#include <cstdio>
#include <cstdint>
#include <string>
#include <utility>

// The overlay is a controller and a status display. It never touches the
// framebuffer and never blocks the game.

namespace {

constexpr std::size_t salty_shared_size = 0x1000;
constexpr const char* log_path = "sdmc:/SaltySD/saltynx_core.log";

std::string g_report_status{"not checked"};

void publish_status(std::string status) {
    g_report_status = std::move(status);

    FILE* const file = std::fopen(log_path, "ab");
    if (file == nullptr) {
        return;
    }
    std::fprintf(file, "lsfg-nx: overlay report drain %s\n", g_report_status.c_str());
    std::fflush(file);
    std::fclose(file);
}

[[nodiscard]] Result connect_to_salty(Handle& session) noexcept {
    Result result = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    for (std::uint32_t attempt = 0; attempt < 5'000; ++attempt) {
        result = svcConnectToNamedPort(&session, "SaltySD");
        if (R_SUCCEEDED(result)) {
            return result;
        }
        svcSleepThread(1'000'000);
    }
    return result;
}

[[nodiscard]] Result get_shared_memory_handle(Handle& shared) noexcept {
    Handle session = INVALID_HANDLE;
    Result result = connect_to_salty(session);
    if (R_FAILED(result)) {
        return result;
    }

    Service service{};
    service.session = session;
    service.own_handle = 1;

    SfDispatchParams get_params{};
    get_params.in_send_pid = true;
    get_params.out_handle_attrs.attr0 = SfOutHandleAttr_HipcCopy;
    get_params.out_handles = &shared;
    result = serviceDispatchImpl(&service, 7, nullptr, 0, nullptr, 0, get_params);

    SfDispatchParams end_params{};
    end_params.in_send_pid = true;
    (void)serviceDispatchImpl(&service, 0, nullptr, 0, nullptr, 0, end_params);
    svcCloseHandle(session);
    return result;
}

[[nodiscard]] lsfg::protocol::ReportBlock* find_reports(SharedMemory& memory) noexcept {
    const std::uintptr_t base
        = reinterpret_cast<std::uintptr_t>(shmemGetAddr(&memory));
    constexpr std::size_t last_offset
        = salty_shared_size - sizeof(lsfg::protocol::ReportBlock);

    for (std::size_t offset = 0; offset <= last_offset;
         offset += alignof(lsfg::protocol::ReportBlock)) {
        auto* const reports
            = reinterpret_cast<lsfg::protocol::ReportBlock*>(base + offset);
        if (reports->magic == lsfg::protocol::report_magic
            && lsfg::succeeded(lsfg::protocol::validate(*reports))) {
            return reports;
        }
    }
    return nullptr;
}

void drain_reports() {
    Handle handle = INVALID_HANDLE;
    const Result handle_result = get_shared_memory_handle(handle);
    if (R_FAILED(handle_result) || handle == INVALID_HANDLE) {
        std::array<char, 48> status{};
        std::snprintf(
            status.data(), status.size(), "connect failed 0x%08x", handle_result);
        publish_status(status.data());
        return;
    }

    SharedMemory memory{};
    shmemLoadRemote(&memory, handle, salty_shared_size, Perm_Rw);
    const Result map_result = shmemMap(&memory);
    if (R_FAILED(map_result)) {
        shmemClose(&memory);
        std::array<char, 48> status{};
        std::snprintf(
            status.data(), status.size(), "mapping failed 0x%08x", map_result);
        publish_status(status.data());
        return;
    }

    lsfg::protocol::ReportBlock* const reports = find_reports(memory);
    if (reports == nullptr) {
        shmemClose(&memory);
        publish_status("runtime buffer absent");
        return;
    }

    FILE* const file = std::fopen(log_path, "ab");
    if (file == nullptr) {
        shmemClose(&memory);
        g_report_status = "log open failed";
        return;
    }

    std::uint64_t written = 0;
    bool write_failed = false;
    for (;;) {
        const std::string_view line = lsfg::protocol::peek_report(*reports);
        if (line.empty()) {
            break;
        }
        if (std::fwrite(line.data(), 1, line.size(), file) != line.size()) {
            write_failed = true;
            break;
        }
        if (std::fflush(file) != 0) {
            write_failed = true;
            break;
        }
        lsfg::protocol::consume_report(*reports);
        ++written;
    }
    std::fclose(file);

    const std::uint64_t dropped = lsfg::protocol::dropped_reports(*reports);
    shmemClose(&memory);

    if (write_failed) {
        publish_status("log write failed");
    } else {
        publish_status("wrote " + std::to_string(written)
            + ", dropped " + std::to_string(dropped));
    }
}

class StatusGui final : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame("LSFG-NX", std::string{lsfg::version::project});
        auto list = new tsl::elm::List();

        list->addItem(new tsl::elm::CategoryHeader("Runtime"));
        list->addItem(new tsl::elm::ListItem(
            "State",
            std::string{lsfg::protocol::state_name(lsfg::protocol::RuntimeState::unavailable)}));
        list->addItem(new tsl::elm::ListItem("Protocol", std::to_string(lsfg::protocol::abi_version)));
        list->addItem(new tsl::elm::ListItem("Reports", g_report_status));

        list->addItem(new tsl::elm::CategoryHeader("Build"));
        list->addItem(new tsl::elm::ListItem("Revision", std::string{lsfg::version::git_revision}));

        frame->setContent(list);
        return frame;
    }
};

class LsfgOverlay final : public tsl::Overlay {
public:
    void initServices() override {
        drain_reports();
    }
    void exitServices() override {}

    std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<StatusGui>();
    }
};

} // namespace

int main(int argc, char** argv) {
    return tsl::loop<LsfgOverlay>(argc, argv);
}
