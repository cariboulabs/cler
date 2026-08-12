#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"

namespace {

struct GuiBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;

    GuiBlock(const char* name, std::vector<std::string>* log)
        : BlockBase(name), _log(log) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        return cler::Error::NotEnoughSamples;
    }

    void render() { _log->push_back(this->name()); }

private:
    std::vector<std::string>* _log;
};

struct PlainBlock : public cler::BlockBase {
    PlainBlock(const char* name, std::vector<std::string>* log)
        : BlockBase(name), _log(log) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        return cler::Error::NotEnoughSamples;
    }

    void render() { _log->push_back(std::string(this->name()) + ":render"); }

private:
    std::vector<std::string>* _log;
};

static_assert(cler::block_declares_is_gui_v<GuiBlock>);
static_assert(cler::block_declares_is_gui_v<GuiBlock&>);
static_assert(cler::block_declares_is_gui_v<const GuiBlock&>);
static_assert(!cler::block_declares_is_gui_v<PlainBlock>);
static_assert(!cler::block_declares_is_gui_v<PlainBlock&>);

TEST(GuiRender, ForEachBlockVisitsRunnerOrder) {
    std::vector<std::string> log;
    GuiBlock a("a", &log);
    PlainBlock b("b", &log);
    GuiBlock c("c", &log);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&a),
        cler::BlockRunner(&b),
        cler::BlockRunner(&c)
    );

    std::vector<std::string> visited;
    fg.for_each_block([&](auto& block) { visited.push_back(block.name()); });
    EXPECT_EQ(visited, (std::vector<std::string>{"a", "b", "c"}));
}

TEST(GuiRender, RenderBlocksRendersOnlyGuiBlocksInRunnerOrder) {
    std::vector<std::string> log;
    GuiBlock a("a", &log);
    PlainBlock b("b", &log);
    GuiBlock c("c", &log);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&a),
        cler::BlockRunner(&b),
        cler::BlockRunner(&c)
    );

    cler::gui::detail::render_blocks(fg);
    EXPECT_EQ(log, (std::vector<std::string>{"a", "c"}));
}

}  // namespace
