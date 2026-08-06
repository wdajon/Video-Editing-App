// Canonical serialisation of a timeline document.
//
// One document state has exactly one byte representation. That is what makes
// M2's exit gate -- "undo returns to a byte-identical state" -- mean anything:
// if two equal documents could serialise differently, a passing comparison
// would prove nothing about the command stack.
//
// The format is plain text, line-oriented, and diffable on purpose. A project
// file that can be read in a code review, and merged, is worth more than a
// compact one. It carries a version from the first commit, because a format
// without a version is a format that cannot be changed safely later.

#ifndef RF_TIMELINE_SERIALISE_HPP
#define RF_TIMELINE_SERIALISE_HPP

#include <string>

#include "rf/timeline/document.hpp"

namespace rf::timeline {

/// Schema version written into the header. Bumped whenever the meaning or
/// layout of any field changes; a reader that does not recognise a version must
/// refuse the file rather than guess.
/// Version 2 added `source_duration` to the clip record (ADR 009). A version 1
/// file is missing a field with no safe default, so it cannot be read by
/// widening alone -- which is the case the version number was put here for.
///
/// Version 3 added `sync_locked` to the track record (ADR 010). This one *does*
/// have a safe default -- Premiere's default is on, and so is ours -- so a
/// reader could widen a v2 file. The version still moves: a field that appears
/// in the bytes changes them, and byte-identity is the whole undo gate.
inline constexpr int kProjectFormatVersion = 3;

/// Serialises `document` canonically.
///
/// Deterministic: same document in, same bytes out, on every platform and in
/// every locale. Integers only -- there is no floating point anywhere in the
/// document model, which is what keeps that promise cheap to hold.
[[nodiscard]] std::string serialise(const Document& document);

}  // namespace rf::timeline

#endif  // RF_TIMELINE_SERIALISE_HPP
