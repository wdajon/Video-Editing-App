#include "rf/app/transport.hpp"

namespace rf::app {

Result<Transport> Transport::create(const media::Rational& frame_rate) {
    Result<playback::PlaybackClock> clock = playback::PlaybackClock::create(frame_rate);
    if (!clock) {
        return clock.error();
    }
    return Transport{clock.value()};
}

Result<void> Transport::apply(const edit::Shuttle& shuttle, playback::Nanoseconds now) {
    if (shuttle.is_stopped()) {
        if (!clock_.is_playing()) {
            // Already stopped. Pausing again would re-anchor at `now`, which is
            // harmless today and would quietly discard a pending seek later.
            return ok();
        }
        return clock_.pause(now);
    }
    return clock_.play(now, shuttle.rate());
}

Result<std::int64_t> Transport::frame_at(playback::Nanoseconds now) const {
    return clock_.frame_at(now);
}

void Transport::seek(playback::Nanoseconds now, std::int64_t frame) {
    clock_.seek(now, frame);
}

}  // namespace rf::app
