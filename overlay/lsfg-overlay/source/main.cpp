// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lsfg/common/protocol.hpp>
#include <lsfg/common/version.hpp>

#define TESLA_INIT_IMPL
#include <tesla.hpp>

#include <string>

// The overlay is a controller and a status display. It never touches the
// framebuffer and never blocks the game.

namespace {

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

        list->addItem(new tsl::elm::CategoryHeader("Build"));
        list->addItem(new tsl::elm::ListItem("Revision", std::string{lsfg::version::git_revision}));

        frame->setContent(list);
        return frame;
    }
};

class LsfgOverlay final : public tsl::Overlay {
public:
    void initServices() override {}
    void exitServices() override {}

    std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<StatusGui>();
    }
};

} // namespace

int main(int argc, char** argv) {
    return tsl::loop<LsfgOverlay>(argc, argv);
}
