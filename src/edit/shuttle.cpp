#include "rf/edit/shuttle.hpp"

#include <utility>

#include "rf/core/assert.hpp"

namespace rf::edit {

std::vector<media::Rational> Shuttle::default_ladder() {
    return {media::Rational{1, 1}, media::Rational{2, 1}, media::Rational{4, 1},
            media::Rational{8, 1}, media::Rational{16, 1}};
}

Shuttle::Shuttle(std::vector<media::Rational> ladder) : ladder_(std::move(ladder)) {
    // A ladder with no rungs would make every transition a no-op, and a shuttle
    // that silently refuses to move is worse than one that refuses to exist.
    RF_CHECK_MSG(!ladder_.empty(), "a shuttle ladder needs at least one speed");
}

media::Rational Shuttle::rate() const {
    if (direction_ == 0) {
        return media::Rational{0, 1};
    }
    const media::Rational speed = slow_ ? media::Rational{1, 2} : ladder_[rung_];
    return media::Rational{speed.numerator() * direction_, speed.denominator()};
}

void Shuttle::step(int towards) {
    // Slow motion is a state of its own rather than a rung, so the first press
    // of either direction key leaves it and rejoins the ladder at 1x.
    if (slow_) {
        slow_ = false;
        rung_ = 0;
        direction_ = towards;
        return;
    }

    if (direction_ == 0) {
        direction_ = towards;
        rung_ = 0;
        return;
    }

    if (direction_ == towards) {
        if (rung_ + 1 < ladder_.size()) {
            ++rung_;
        }
        return;
    }

    // Running the other way: step one rung toward zero, and stop rather than
    // reversing at speed. A single keystroke that flips direction at 16x is
    // hard to undo by feel -- see ADR 014.
    if (rung_ == 0) {
        direction_ = 0;
        return;
    }
    --rung_;
}

void Shuttle::forward() {
    step(1);
}

void Shuttle::backward() {
    step(-1);
}

void Shuttle::stop() noexcept {
    direction_ = 0;
    rung_ = 0;
    slow_ = false;
}

void Shuttle::slow_forward() noexcept {
    slow_ = true;
    direction_ = 1;
}

void Shuttle::slow_backward() noexcept {
    slow_ = true;
    direction_ = -1;
}

}  // namespace rf::edit
