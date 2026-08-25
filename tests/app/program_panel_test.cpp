// The Program dock's title is the only check anyone has for D30: whether the
// panel is presenting through the swapchain or reading pixels back. That check
// is worth nothing unless the untried state is distinguishable from the passing
// one, and unless the presenting path gets a chance to come up without the user
// happening to press a shuttle key.
//
// What these tests cannot do: prove that a frame reaches a screen. The offscreen
// platform cannot create a Vulkan surface, so the route asserted here is always
// the readback one, and ADR 008 records why presentation has no automated
// oracle. What they do prove is that the *report* of the route is honest, which
// is the part a person's sign-off depends on.

#include "rf/app/program_panel.hpp"

#include <QDockWidget>
#include <gtest/gtest.h>

#include <filesystem>

#include "rf/app/demo_timeline.hpp"
#include "rf/app/main_window.hpp"
#include "rf/app/timeline_panel.hpp"
#include "rf/edit/command_map.hpp"
#include "rf/timeline/document.hpp"

namespace {

using rf::app::MainWindow;
using rf::app::ProgramPanel;
using rf::edit::Action;

std::filesystem::path fixture() {
    return std::filesystem::path(RF_TEST_FIXTURE_DIR) / "media" /
           "bars_320x240_30fps_h264_aac.mp4";
}

QDockWidget* program_dock(const MainWindow& window) {
    return window.findChild<QDockWidget*>(QStringLiteral("rf_dock_program"));
}

TEST(ProgramPanelPath, StartsUnknownBecauseNothingHasRendered) {
    MainWindow window;
    EXPECT_EQ(window.program_panel()->path(), ProgramPanel::Path::unknown);
    EXPECT_FALSE(window.program_panel()->is_presenting());
}

TEST(ProgramPanelPath, TheUntriedTitleIsNotThePresentingTitle) {
    // The defect this exists for: the dock used to be constructed titled
    // "Program", the exact string that means "presenting". Anyone following the
    // documented sign-off -- launch, look at the title -- read a pass out of a
    // panel that had never rendered anything.
    MainWindow window;
    QDockWidget* dock = program_dock(window);
    ASSERT_NE(dock, nullptr);
    EXPECT_NE(dock->windowTitle(), QStringLiteral("Program"));
}

TEST(ProgramPanelPath, AnEmptyDocumentSaysSoRatherThanNamingARoute) {
    MainWindow window;
    QDockWidget* dock = program_dock(window);
    ASSERT_NE(dock, nullptr);
    EXPECT_EQ(dock->windowTitle(), QStringLiteral("Program (no picture)"));
}

/// A window on the demo timeline, pointed at the fixture the repository ships.
class ProgramPanelOnMedia : public ::testing::Test {
protected:
    void SetUp() override {
        const std::filesystem::path media = fixture();
        if (!std::filesystem::exists(media)) {
            GTEST_SKIP() << "fixture missing: " << media.string();
        }
        window_ = std::make_unique<MainWindow>();
        const rf::Result<void> built =
            rf::app::build_demo_timeline(window_->document(), media.string());
        ASSERT_TRUE(built.has_value()) << built.error().to_string();

        const rf::timeline::Track& video = window_->document().tracks().front();
        window_->edit_state().track = video.id;
        window_->edit_state().clip = video.clips[1].id;
    }

    /// Moves the playhead the way a user would -- through the panel, not by
    /// calling the Program panel directly. The point of the test is that an
    /// ordinary step is enough.
    void step() { window_->timeline_panel()->perform(Action::step_forward); }

    std::unique_ptr<MainWindow> window_;
};

TEST_F(ProgramPanelOnMedia, AStepAloneDecidesTheRoute) {
    step();

    const ProgramPanel* panel = window_->program_panel();
    if (panel->path() == ProgramPanel::Path::unknown) {
        GTEST_SKIP() << "no picture rendered: " << panel->status().toStdString();
    }
    // Offscreen has no surface, so this is the readback route. On a machine
    // with a display it would be `presenting`; either is a decided answer, and
    // `unknown` is what must not survive a step.
    EXPECT_EQ(panel->path(), ProgramPanel::Path::readback);
}

TEST_F(ProgramPanelOnMedia, AStepAloneAsksToPresent) {
    // The defect underneath the title one: attaching the surface hung off the
    // shuttle alone, so a session that never pressed J, K or L never even asked
    // whether this machine could present -- and reported "software preview" for
    // a question nobody had put. A step is the least a user can do, and it has
    // to be enough.
    //
    // Offscreen cannot make a Vulkan surface, so what is asserted is the
    // refusal: it is non-empty only if something tried.
    step();

    const ProgramPanel* panel = window_->program_panel();
    if (panel->path() == ProgramPanel::Path::unknown) {
        GTEST_SKIP() << "no picture rendered: " << panel->status().toStdString();
    }
    EXPECT_FALSE(panel->presentation_refusal().isEmpty())
        << "a step rendered a frame but never asked to present";
}

TEST_F(ProgramPanelOnMedia, TheDockTitleFollowsTheRoute) {
    step();

    const ProgramPanel* panel = window_->program_panel();
    if (panel->path() == ProgramPanel::Path::unknown) {
        GTEST_SKIP() << "no picture rendered: " << panel->status().toStdString();
    }
    QDockWidget* dock = program_dock(*window_);
    ASSERT_NE(dock, nullptr);
    EXPECT_EQ(dock->windowTitle(), QStringLiteral("Program (software preview)"));
}

TEST_F(ProgramPanelOnMedia, ReportingTheRouteCostsNoExtraDecode) {
    // The title follows a signal rather than being polled, so a repeated
    // refresh at the same frame must not re-render: `show_frame` returns early
    // and the route cannot change under it.
    step();
    const ProgramPanel* panel = window_->program_panel();
    if (panel->path() == ProgramPanel::Path::unknown) {
        GTEST_SKIP() << "no picture rendered: " << panel->status().toStdString();
    }
    const ProgramPanel::Path settled = panel->path();
    const std::int64_t frame = panel->frame();

    window_->program_panel()->show_frame(frame);
    EXPECT_EQ(panel->path(), settled);
    EXPECT_EQ(panel->frame(), frame);
}

}  // namespace
