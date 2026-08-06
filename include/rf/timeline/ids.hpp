// Strongly-typed identifiers for timeline objects.
//
// A timeline is full of integers -- tick counts, track indices, clip ids, frame
// numbers -- and they are not interchangeable. Passing a track index where a
// clip id belongs must be a compile error, not a mystery at runtime.
//
// Ids are issued by the document from a counter that only ever moves forward.
// See docs/adr/005-timeline-model-and-undo.md: reusing an id after a delete
// would let two different histories produce identical bytes for different
// documents, which would make the undo gate meaningless.

#ifndef RF_TIMELINE_IDS_HPP
#define RF_TIMELINE_IDS_HPP

#include <cstdint>
#include <functional>
#include <string>

namespace rf::timeline {

/// CRTP base giving each id type its own distinct C++ type.
template <class Tag>
class Id {
public:
    using value_type = std::uint64_t;

    /// The null id. Never issued by a document; means "no object".
    constexpr Id() noexcept = default;
    constexpr explicit Id(value_type value) noexcept : value_(value) {}

    [[nodiscard]] constexpr value_type value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ != 0; }

    friend constexpr bool operator==(Id lhs, Id rhs) noexcept { return lhs.value_ == rhs.value_; }
    friend constexpr bool operator!=(Id lhs, Id rhs) noexcept { return lhs.value_ != rhs.value_; }
    friend constexpr bool operator<(Id lhs, Id rhs) noexcept { return lhs.value_ < rhs.value_; }

private:
    value_type value_ = 0;
};

struct ClipTag {};
struct TrackTag {};
struct LinkTag {};

using ClipId = Id<ClipTag>;
using TrackId = Id<TrackTag>;
/// Identifies a group of clips that trim together -- the picture and its audio.
/// Issued from the same counter as every other id, so ADR 005's rules about
/// reuse and undo apply to it unchanged. See docs/adr/011-linked-clips.md.
using LinkId = Id<LinkTag>;

[[nodiscard]] inline std::string to_string(ClipId id) {
    return "clip#" + std::to_string(id.value());
}
[[nodiscard]] inline std::string to_string(TrackId id) {
    return "track#" + std::to_string(id.value());
}
[[nodiscard]] inline std::string to_string(LinkId id) {
    return "link#" + std::to_string(id.value());
}

}  // namespace rf::timeline

namespace std {
template <class Tag>
struct hash<rf::timeline::Id<Tag>> {
    std::size_t operator()(rf::timeline::Id<Tag> id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value());
    }
};
}  // namespace std

#endif  // RF_TIMELINE_IDS_HPP
