// Where the shuttle meets the clock.
//
// `rf::edit::Shuttle` produces a rate and knows nothing about playback;
// `rf::playback::PlaybackClock` consumes a rate and knows nothing about
// keyboards. This joins them, in the application layer, so neither module gains
// a dependency on the other. See docs/adr/014-jkl-shuttle.md.

#ifndef RF_APP_TRANSPORT_HPP
#define RF_APP_TRANSPORT_HPP

#include <cstdint>

#include "rf/core/result.hpp"
#include "rf/edit/shuttle.hpp"
#include "rf/media/rational.hpp"
#include "rf/playback/clock.hpp"
#include "rf/playback/playback_clock.hpp"

namespace rf::app {

class Transport {
public:
    [[nodiscard]] static Result<Transport> create(const media::Rational& frame_rate);

    /// Applies the shuttle's current rate, anchoring at `now`.
    ///
    /// A zero rate pauses rather than "playing at zero speed" -- `PlaybackClock`
    /// rejects that deliberately, so a stopped shuttle must not be handed
    /// through as a rate.
    [[nodiscard]] Result<void> apply(const edit::Shuttle& shuttle, playback::Nanoseconds now);

    /// The frame the playhead is on at `now`. A pure function of the anchor and
    /// the rate, never accumulated, so shuttling cannot drift (ADR 006).
    [[nodiscard]] Result<std::int64_t> frame_at(playback::Nanoseconds now) const;

    /// Moves the playhead and re-anchors, leaving the rate alone.
    void seek(playback::Nanoseconds now, std::int64_t frame);

    [[nodiscard]] bool is_playing() const noexcept { return clock_.is_playing(); }
    [[nodiscard]] const media::Rational& rate() const noexcept { return clock_.rate(); }

private:
    explicit Transport(playback::PlaybackClock clock) : clock_(clock) {}

    playback::PlaybackClock clock_;
};

}  // namespace rf::app

#endif  // RF_APP_TRANSPORT_HPP
