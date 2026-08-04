# ADR 004 — Linking libav*, and the licensing consequence

- **Status:** Accepted, with one open question owned by the project owner
- **Date:** 2026-08-04
- **Milestone:** M1

## Context

M1 is the media engine: probe, decode, frame-accurate seek. The brief fixes the
library — FFmpeg — and fixes how it is used: *link the libraries; do not shell
out to the `ffmpeg` binary anywhere in the render path*.

That constraint is not stylistic. Shelling out cannot give frame-accurate seek
(no access to packet timestamps or decoder state), cannot cancel mid-operation,
cannot report structured errors, cannot be measured against a per-frame latency
budget, and pays process-spawn cost per operation. A CLI-driven editor is a
different, worse product.

## Decision

**Link `libavcodec`, `libavformat`, `libswresample` and `libswscale`** from
vcpkg's `ffmpeg` port, pinned to `8.1.2#3` in `vcpkg.json` alongside the existing
baseline pin.

Features are enabled per milestone rather than all at once:

| Feature | Enabled at | Why |
|---|---|---|
| `avcodec`, `avformat` | M1 | Demux and decode. |
| `swscale` | M1 | Pixel-format conversion for the reference frame hashes. |
| `swresample` | M1 | Audio format conversion; needed before M10. |
| `avfilter` | not yet | No filter graph in the design; effects are shaders (M3). |
| `avdevice` | not yet | ReelForge captures nothing. |
| `x264`, `x265` | M5 | H.264/HEVC encode for delivery. **See licensing below.** |

`avdevice` and `avfilter` are in the port's default feature set and are turned
off explicitly via `"default-features": false`. Dependencies that are not needed
are attack surface, build time, and binary size.

AAC encoding at M5 uses FFmpeg's **native** AAC encoder rather than `fdk-aac`.
`fdk-aac` requires the `nonfree` feature, which produces a binary that cannot be
distributed at all. Not a tradeoff worth making for an encoder difference that
does not survive Instagram's re-encode.

## The open question: GPL

`x264` and `x265` are GPL-2.0-or-later. Linking them requires FFmpeg's `gpl`
feature and makes the combined work GPL. ReelForge already declares
`GPL-3.0-or-later` in `README.md` and `vcpkg.json` — a declaration made in M0 on
exactly this reasoning — but there is still no `LICENSE` file, and the project
owner has not confirmed the choice.

This ADR proceeds **on the stated assumption that ReelForge is GPL-3.0-or-later**,
because that is what the repository currently declares and because H.264 delivery
is not optional for the product's primary target.

If the owner chooses LGPL instead, the consequences are concrete and land at M5,
not now:

- `x264`/`x265` are out. H.264/HEVC encoding would come from platform encoders
  (Media Foundation, VideoToolbox, VA-API) or `openh264`, each with its own
  quality and availability tradeoffs, and none available uniformly across the
  support matrix.
- FFmpeg itself must be built LGPL, and every future port feature has to be
  checked against it.
- The declarations in `README.md` and `vcpkg.json` change.

Nothing in M1 depends on the answer — decode and probe are LGPL-clean — so the
work proceeds and the decision is genuinely deferrable to M5. It should not be
deferred past M5.

## Consequences

- `rf_media` is the only target that may include `libav*` headers. The render
  graph, the timeline model, and the UI never see them; this is the layering rule
  from the brief and it is what makes the media engine testable headlessly.
- libav* is a C API with manual lifetime rules. Every `AVFormatContext`,
  `AVCodecContext`, `AVFrame` and `AVPacket` is owned by an RAII wrapper; no raw
  `av_*_free` calls appear outside `src/media/`.
- libav* reports failure as negative `int` error codes. Those are translated into
  `rf::Error` at the boundary, with the numeric code and `av_strerror` text
  preserved, so a decode failure at the UI names the actual libav reason.
- Pinning FFmpeg to an exact version is required for golden-frame tests: decoder
  output can change between FFmpeg releases, and an unpinned bump would look like
  a ReelForge regression.
