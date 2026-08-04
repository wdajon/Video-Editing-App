# Media test fixtures

Small, committed, and reproducible. They are committed rather than generated at
test time because a test that depends on an `ffmpeg` binary being installed on
the machine is a test that fails for reasons unrelated to ReelForge.

**Provenance matters here.** These were produced by the FFmpeg *command-line
tool*, which is a different program from the `libav*` libraries ReelForge links,
but not a different codebase. That is acceptable for probe and demux tests,
which check that ReelForge reports what the container actually says. It is *not*
acceptable as the reference for the M1 exit gate ("decoded hash matches a
reference"): a decoder cannot be its own oracle. That gate needs either real
footage or a reference hash from an independent decoder, and until it has one
the gate is not met. See `docs/PROGRESS.md`.

Generated with `ffmpeg 8.1.2-full_build-www.gyan.dev` on 2026-08-04.

## `bars_320x240_30fps_h264_aac.mp4` (101,035 bytes)

Constant frame rate, video + audio — the ordinary case.

```bash
ffmpeg -f lavfi -i "testsrc2=size=320x240:rate=30:duration=2" \
       -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=2" \
       -c:v libx264 -preset veryfast -pix_fmt yuv420p -g 15 \
       -c:a aac -b:a 64k -ac 2 -movflags +faststart \
       bars_320x240_30fps_h264_aac.mp4
```

| Property | Value |
|---|---|
| format | `mov,mp4,m4a,3gp,3g2,mj2` |
| duration | 2.000000 s |
| stream 0 | h264, 320x240, yuv420p, 30/1 fps, time base 1/15360, 60 frames |
| stream 1 | aac, 48000 Hz, 2 channels, time base 1/48000, 95 frames |

Note stream 1 reports `r_frame_rate=0/0`. Audio streams genuinely do report a
zero-denominator rational, which is why `Rational::from` returns an error rather
than aborting, and why nothing may construct a `Rational` directly from
container metadata.

## `bars_320x240_2997fps_h264.mp4` (82,854 bytes)

Video only, 30000/1001 fps — the non-integer rate that a float timestamp
pipeline gets wrong.

```bash
ffmpeg -f lavfi -i "testsrc2=size=320x240:rate=30000/1001:duration=2" \
       -c:v libx264 -preset veryfast -pix_fmt yuv420p -g 15 -an \
       -movflags +faststart bars_320x240_2997fps_h264.mp4
```

| Property | Value |
|---|---|
| duration | 2.002000 s |
| stream 0 | h264, 320x240, yuv420p, 30000/1001 fps, time base 1/30000, 60 frames |

## `tone_mono_44100_aac.m4a` (7,257 bytes)

Audio only. Probing must not assume a video stream exists.

```bash
ffmpeg -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=1" \
       -c:a aac -b:a 48k -ac 1 tone_mono_44100_aac.m4a
```

## `truncated_h264.mp4` (50,519 bytes)

The 30 fps fixture cut to half its length **with raw byte IO, not with ffmpeg**,
so the damage is genuine rather than a clean re-mux of a shorter clip. Exercises
the malformed-media path the robustness rubric asks about.

## Large sources (not committed)

The M1 exit gate names a 10-minute 4K file. At 2.9 GB that cannot live in git,
so it is generated on demand and checked with `rf_seek_check` rather than by the
unit suite — 18,000 exhaustive seeks through a 250-frame GOP would be millions of
4K frame decodes.

```bash
ffmpeg -f lavfi -i "testsrc2=size=3840x2160:rate=30:duration=600" \
       -c:v libx264 -preset ultrafast -pix_fmt yuv420p -g 250 -crf 30 -an \
       -movflags +faststart testsrc2_3840x2160_30fps_600s.mp4

rf_seek_check testsrc2_3840x2160_30fps_600s.mp4 --seeks 200
```

| Property | Value |
|---|---|
| resolution | 3840x2160 |
| frame rate | 30/1, constant |
| GOP | 250 — a seek may decode ~249 frames forward |
| frames | 18,000 |
| time base | 1/15360 |
| size | 2.9 GB |

**What this file does not test.** `testsrc2` is constant frame rate with clean,
monotonic timestamps. It exercises scale and long-GOP seeking, and nothing else.
Variable frame rate, B-pyramids, non-monotonic or missing timestamps, rotation
metadata, and mixed-codec containers — the things real camera and screen-capture
footage carries, and where frame-accurate seeking actually breaks — are not
covered by any fixture here. That is a stated gap, not an oversight.

## `not_media.mp4` (25 bytes)

ASCII text with a video extension. Exercises the "user picked the wrong file"
path, which must produce a useful error and not a crash or a plausible-looking
empty result.
