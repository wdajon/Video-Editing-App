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
inline constexpr int kProjectFormatVersion = 1;

/// Serialises `document` canonically.
///
/// Deterministic: same document in, same bytes out, on every platform and in
/// every locale. Integers only -- there is no floating point anywhere in the
/// document model, which is what keeps that promise cheap to hold.
[[nodiscard]] std::string serialise(const Document& document);

}  // namespace rf::timeline

#endif  // RF_TIMELINE_SERIALISE_HPP
