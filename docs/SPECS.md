# Platform Delivery Specifications

Every number in this file is traceable to a dated source. **No value here may be
copied into `.cpp` or `.h`.** Values reach the encoder only through validated
preset documents in `presets/*.json` (schema: `presets/schema/delivery-preset.schema.json`).
When a platform changes a spec, the fix is a JSON edit plus a row in this file.

Status legend:

| Status | Meaning |
|---|---|
| `VERIFIED` | Stated by a primary source (platform vendor documentation). |
| `DISPUTED` | Sources disagree. All values recorded; the most conservative is implemented. |
| `UNSPECIFIED` | No primary source states it. ReelForge picks a defensible default and documents the reasoning. |

---

## Source register

| ID | Source | Type | Accessed |
|---|---|---|---|
| S1 | Meta for Developers — Instagram Platform, IG User `media` reference (Reels & Stories specifications tables) — https://developers.facebook.com/docs/instagram-platform/instagram-graph-api/reference/ig-user/media/ | Primary (vendor API docs) | 2026-08-04 |
| S2 | Instagram Help Center — "Reel size & aspect ratios on Instagram" — https://www.facebook.com/help/instagram/1038071743007909/ | Primary (vendor help centre) | 2026-08-04 |
| S3 | Third-party aggregators (socialpilot, hootsuite, influencermarketinghub, sellerpic, grynow, postfa.st, viralix, sendcove, wayin) surfaced by web search for "Instagram Reels specifications 2026" | Secondary — **not authoritative**, recorded only to document disagreement | 2026-08-04 |

S1 describes the **API publishing path** (what a third-party tool uploads through).
S2 describes the **in-app upload path**. They are not the same limits, and that
difference is the origin of most of the disagreement in S3. ReelForge delivers
files that users hand to Instagram, so it must satisfy the tighter of the two.

---

## Instagram Reels (9:16 vertical)

| Field | Value | Status | Source |
|---|---|---|---|
| Container | MOV or MP4 (MPEG-4 Part 14), no edit lists, `moov` atom at front of file | `VERIFIED` | S1 |
| Video codec | HEVC or H.264, progressive scan, closed GOP, 4:2:0 chroma subsampling | `VERIFIED` | S1 |
| Audio codec | AAC, ≤ 48 kHz sample rate, 1 or 2 channels | `VERIFIED` | S1 |
| Audio bitrate | 128 kbps | `VERIFIED` | S1 |
| Video bitrate | VBR, 25 Mbps maximum | `VERIFIED` | S1 |
| Frame rate | 23–60 fps (S1); minimum 30 fps recommended (S2) | `VERIFIED` | S1, S2 |
| Max horizontal pixels | 1920 columns | `VERIFIED` | S1 |
| Minimum resolution | 720 px | `VERIFIED` | S2 |
| Aspect ratio (accepted) | 0.01:1 – 10:1 (S1, API path); 1.91:1 – 9:16 (S2, in-app) | `VERIFIED` | S1, S2 |
| Aspect ratio (recommended) | 9:16 | `VERIFIED` | S1, S2 |
| Minimum duration | 3 seconds | `VERIFIED` | S1 |
| **Maximum duration** | **900 s (15 min)** per S1. S3 variously claims 90 s, 3 min, 15 min, and 20 min. | `DISPUTED` | S1 vs S3 |
| **Maximum file size** | **300 MB** per S1. S3 variously claims 650 MB, 1 GB, and 4 GB. | `DISPUTED` | S1 vs S3 |
| Colour space / transfer | Not stated by any primary source. | `UNSPECIFIED` | — |

### Resolution of the disputes

**Maximum duration — implemented as 900 s hard limit, 90 s advisory.**
S1 (primary) says 15 min. No primary source states 90 s or 20 min. Rather than
pick one number, the preset carries two fields: `max_duration_s` (hard, rejects
the export) and `recommended_max_duration_s` (advisory, warns and proceeds).
Hard-failing a 100-second reel because a blog said 90 s would be a defect;
silently producing a 20-minute file that the API refuses would also be a defect.

**Maximum file size — implemented as 300 MB.**
This is the most conservative value and the only one from a primary source.
A file that satisfies 300 MB satisfies every larger claimed limit, so the
conservative choice cannot produce a rejected upload. Recorded as `DISPUTED`
because S3 consistently disagrees and may be describing the in-app path, which
S2 does not quantify.

**Colour space — implemented as BT.709, explicitly signalled.**
The premise "sRGB vs Rec.709" is partly a false dichotomy: sRGB (IEC 61966-2-1)
and Rec.709 (ITU-R BT.709) share identical primaries and white point, and differ
only in transfer function. Since no primary source specifies either, ReelForge
writes BT.709 primaries / BT.709 transfer / BT.709 matrix coefficients into the
bitstream and into the container, because untagged H.264/HEVC at HD resolution is
interpreted as BT.709 by every mainstream decoder. The values live in the preset
as OCIO colour space names, not as hardcoded matrices. Revisit if Meta publishes
a statement; open a defect immediately if it contradicts this.

---

## Instagram Stories

| Field | Value | Status | Source |
|---|---|---|---|
| Container | MOV or MP4 (MPEG-4 Part 14), no edit lists, `moov` atom at front of file | `VERIFIED` | S1 |
| Video codec | HEVC or H.264, progressive scan, closed GOP, 4:2:0 | `VERIFIED` | S1 |
| Audio codec | AAC, ≤ 48 kHz, 1 or 2 channels, 128 kbps | `VERIFIED` | S1 |
| Video bitrate | VBR, 25 Mbps maximum | `VERIFIED` | S1 |
| Frame rate | 23–60 fps | `VERIFIED` | S1 |
| Max horizontal pixels | 1920 columns | `VERIFIED` | S1 |
| Aspect ratio | 0.1:1 – 10:1 accepted, 9:16 recommended | `VERIFIED` | S1 |
| Minimum duration | 3 seconds | `VERIFIED` | S1 |
| Maximum duration | 60 seconds | `VERIFIED` | S1 |
| Maximum file size | 100 MB | `VERIFIED` | S1 |

Note the Stories aspect-ratio floor is 0.1:1 while Reels is 0.01:1. That is not a
transcription error on our side; the two tables in S1 differ.

---

## Instagram feed, 4:5 portrait

| Field | Value | Status | Source |
|---|---|---|---|
| Aspect ratio | 4:5 | `UNSPECIFIED` | No primary source located in this pass |
| All other fields | — | `UNSPECIFIED` | — |

**Not yet researched to primary sources.** No 4:5 feed preset ships until this
section is filled in. M5 is blocked on it, and M7's 4:5 safe-zone overlay is
blocked on it, because the overlay geometry is derived from the preset.

---

## Open questions for the next research pass

1. Primary source for 4:5 and 1:1 feed video specifications.
2. Whether S2's in-app path has a documented file-size or duration ceiling
   anywhere on a Meta-owned domain.
3. Whether Meta documents a preferred colour signalling (`colr` atom / VUI) for
   Reels anywhere; currently `UNSPECIFIED`.
4. Safe-zone geometry (UI chrome overlay regions) for Reels and Stories from a
   Meta-owned source, required by M7.
