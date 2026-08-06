// JKL: the shuttle every editor reaches for first.
//
// J plays backward, K stops, L plays forward, and repeated presses shuttle
// faster. This is the state machine only -- it produces a rate and knows nothing
// about playback. See docs/adr/014-jkl-shuttle.md, which also records what the
// rate is and is not connected to.

#ifndef RF_EDIT_SHUTTLE_HPP
#define RF_EDIT_SHUTTLE_HPP

#include <cstddef>
#include <vector>

#include "rf/media/rational.hpp"

namespace rf::edit {

/// A shuttle transport.
///
/// The rate is a signed rational, zero meaning stopped. Rational rather than a
/// double so half speed is exactly 1/2: `PlaybackClock` takes a rational rate
/// and a timeline is thousands of additions.
class Shuttle {
public:
    /// Speeds the ladder steps through, fastest last. Defaults to 1, 2, 4, 8,
    /// 16 -- doubling, which is what a jog wheel does and what most sources
    /// describe, though they disagree (ADR 014). It is a vector rather than a
    /// constant so a preferences page can replace it; nothing below knows how
    /// many rungs there are.
    [[nodiscard]] static std::vector<media::Rational> default_ladder();

    Shuttle() : ladder_(default_ladder()) {}
    explicit Shuttle(std::vector<media::Rational> ladder);

    /// Signed multiplier: 1 is normal speed, -2 is double speed backward, 0 is
    /// stopped.
    [[nodiscard]] media::Rational rate() const;

    [[nodiscard]] bool is_stopped() const noexcept { return direction_ == 0; }
    [[nodiscard]] const std::vector<media::Rational>& ladder() const noexcept { return ladder_; }

    /// L. Steps up the forward ladder, or one rung back toward zero when
    /// already running backward -- never straight into reverse at speed, which
    /// is hard to undo on a control operated without looking.
    void forward();

    /// J. The mirror of `forward()`.
    void backward();

    /// K. Stops immediately, at any speed and in either direction.
    void stop() noexcept;

    /// Shift+L and Shift+J. Half speed, from wherever the shuttle was.
    void slow_forward() noexcept;
    void slow_backward() noexcept;

private:
    void step(int towards);

    std::vector<media::Rational> ladder_;
    std::size_t rung_ = 0;
    int direction_ = 0;  ///< -1, 0 or +1.
    bool slow_ = false;
};

}  // namespace rf::edit

#endif  // RF_EDIT_SHUTTLE_HPP
