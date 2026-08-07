// A timeline to look at.
//
// The Timeline panel's painting has no automated oracle (D23), and neither does
// the feel of a keyboard trim: a test can prove the document changed, not that
// pressing Ctrl+Right looks and feels like trimming a frame. Verifying either
// needs a person, and a person needs something on the screen.
//
// This is a fixture for a human, not a substitute for loading a project. It is
// built through the ordinary document API and refuses rather than papering over
// a failure, so what appears on screen is a real document and not a drawing of
// one. Project loading is a separate piece of work and this does not stand in
// for it -- which is why it is behind an explicit flag rather than being what an
// empty window shows.

#ifndef RF_APP_DEMO_TIMELINE_HPP
#define RF_APP_DEMO_TIMELINE_HPP

#include <string>

#include "rf/core/result.hpp"
#include "rf/timeline/document.hpp"

namespace rf::app {

/// Fills `document` with two tracks of clips, linked in pairs, with media to
/// spare at both ends so every trim in the set has somewhere to go.
///
/// `video_source` is what every picture clip points at. It defaults to a name
/// with no file behind it, which is fine for exercising the editor -- nothing in
/// the timeline decodes. Pass a real path to get a demo that also *renders*.
///
/// Fails if `document` already holds anything: silently appending to an open
/// project would be a surprising thing for a demo flag to do.
[[nodiscard]] Result<void> build_demo_timeline(timeline::Document& document,
                                               const std::string& video_source = {});

/// The fixture the repository ships, relative to the source tree. Small, real,
/// and already used by the media tests, so a demo built on it renders without
/// anyone having to find a video first.
[[nodiscard]] std::string demo_media_relative_path();

}  // namespace rf::app

#endif  // RF_APP_DEMO_TIMELINE_HPP
