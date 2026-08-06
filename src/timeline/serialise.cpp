#include "rf/timeline/serialise.hpp"

#include <string>

namespace rf::timeline {
namespace {

/// Quotes a string so it survives a round trip and cannot forge a line break.
///
/// A media path can contain quotes, backslashes and -- on every platform that
/// allows it -- newlines. Writing one unescaped would let a file name terminate
/// a record early and turn a project file into a different, valid-looking
/// project file.
void append_quoted(std::string& out, std::string_view text) {
    out.push_back('"');
    for (const char character : text) {
        switch (character) {
            case '"':  out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\n': out.append("\\n");  break;
            case '\r': out.append("\\r");  break;
            case '\t': out.append("\\t");  break;
            default:   out.push_back(character); break;
        }
    }
    out.push_back('"');
}

void append_flag(std::string& out, bool value) {
    out.push_back(value ? '1' : '0');
}

}  // namespace

std::string serialise(const Document& document) {
    std::string out;
    // Rough reservation; growth is fine, this is not a hot path.
    out.reserve(128 + document.clip_count() * 64);

    out.append("reelforge/");
    out.append(std::to_string(kProjectFormatVersion));
    out.push_back('\n');

    out.append("timebase ");
    out.append(document.time_base().to_string());
    out.push_back('\n');

    // The id counter is state, not bookkeeping: two documents that differ only
    // in which ids have been spent are different documents, because their next
    // edits will differ. See docs/adr/005-timeline-model-and-undo.md.
    out.append("nextid ");
    out.append(std::to_string(document.next_id()));
    out.push_back('\n');

    for (const Track& track : document.tracks()) {
        out.append("track ");
        out.append(std::to_string(track.id.value()));
        out.push_back(' ');
        out.append(to_string(track.kind));
        out.push_back(' ');
        append_flag(out, track.muted);
        out.push_back(' ');
        append_flag(out, track.locked);
        out.push_back(' ');
        append_flag(out, track.sync_locked);
        out.push_back(' ');
        append_quoted(out, track.name);
        out.push_back('\n');

        // Clips are emitted in the order the document keeps them, which is
        // sorted by start then id. Relying on insertion order here would make
        // the bytes depend on edit history rather than on state.
        for (const Clip& clip : track.clips) {
            out.append("  clip ");
            out.append(std::to_string(clip.id.value()));
            out.push_back(' ');
            out.append(std::to_string(clip.source_in));
            out.push_back(' ');
            out.append(std::to_string(clip.source_duration));
            out.push_back(' ');
            out.append(std::to_string(clip.start));
            out.push_back(' ');
            out.append(std::to_string(clip.duration));
            out.push_back(' ');
            out.append(std::to_string(clip.link.value()));
            out.push_back(' ');
            append_flag(out, clip.enabled);
            out.push_back(' ');
            append_quoted(out, clip.source);
            out.push_back('\n');
        }
    }

    return out;
}

}  // namespace rf::timeline
