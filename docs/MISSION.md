# Mission and working protocol

The brief this project is built to, preserved verbatim so a new session can pick
up without it being re-pasted. Current state is in `docs/PROGRESS.md`.

---

## MISSION

Build **ReelForge**: a cross-platform desktop non-linear video editor in **C++20**, whose
primary target is short-form vertical delivery (Instagram Reels, Stories, 4:5 feed), with:

- **UI model:** Premiere Pro — dockable panel workspaces, Project / Source / Program /
  Timeline / Effect Controls, JKL shuttle, three-point editing, ripple/roll/slip/slide,
  keyboard-first with a remappable command map.
- **Motion graphics:** After Effects — a composition graph nested into timeline clips,
  per-property keyframes with Bézier interpolation, masks and mattes, parented transform
  hierarchies, expressions, precomps.
- **Color and audio:** DaVinci Resolve — node-based grading graph, primaries/curves/
  qualifiers/power windows, scopes (waveform, parade, vectorscope, histogram), managed
  color via OpenColorIO, and a mixer page with busses, EQ, compression, loudness metering.

You are not shipping all of that in one pass. You are shipping a **vertical slice that
actually runs**, then widening it, one gated milestone at a time.

## GROUND-TRUTH RULE

You do not know current platform specs, current library APIs, or current codec support.
Your training data is stale and social platform specs change several times a year.
Before writing any code that encodes a hard number or an external API call:

1. **Search the web** for the current value. Prefer primary sources (Meta/Instagram
   developer docs, FFmpeg docs, Qt docs, OpenColorIO docs) over blog posts and SEO pages.
2. **Record the source URL and access date** in `docs/SPECS.md` next to the value.
3. If sources disagree, record *all* values found, mark the field `DISPUTED`, implement
   the **most conservative** one, and put the constraint behind a config entry — never a
   magic number in the encode path.

Every delivery preset is a **data file** (`presets/*.json`) validated against a schema at
startup, never hardcoded C++. When Instagram changes a spec, the fix must be a JSON edit.

## STACK CONSTRAINTS (non-negotiable unless justified in an ADR)

- C++20, no exceptions across module boundaries, `std::expected`-style error returns.
- **CMake** + a pinned dependency manifest (vcpkg or Conan). Reproducible from clean clone.
- **FFmpeg / libav\*** for demux, decode, encode, mux. Link the libraries; do not shell out
  to the `ffmpeg` binary anywhere in the render path.
- **Qt 6** for shell, docking, and widget chrome.
- GPU compositing via a single abstraction over **Vulkan** (primary) with an OpenGL 4.3
  fallback. Every effect is a shader, not a CPU loop, unless proven otherwise by profile.
- **OpenColorIO v2** for all color transforms. No ad-hoc gamma math.
- **OpenTimelineIO** for project interchange import/export, alongside the native format.
- Native project format is a **plain-text, diffable, versioned** document with a migration
  path — a corrupt or unversioned project file is a P0 bug.
- Tests: GoogleTest or Catch2. Golden-frame comparison for anything visual.
- Threading: a render graph on a job system. **No sleeps, no busy-waits, no data races** —
  builds run under TSan and ASan in CI.

## MILESTONE LADDER

You may not begin milestone *N+1* until *N* passes its exit gate. State the current
milestone at the top of every response.

| # | Milestone | Exit gate |
|---|---|---|
| M0 | Repo, CMake, dep manifest, CI, empty Qt shell | Clean clone → build → run on 2 OSes; CI green |
| M1 | Media engine: probe, decode, frame-accurate seek | Seek to any frame of a 10-min 4K file, decoded hash matches reference |
| M2 | Timeline data model + undo/redo command stack | 10k-op fuzz on the command stack, undo returns to byte-identical state |
| M3 | GPU compositor + Program monitor playback | 1080×1920, 3 layers, sustained 30 fps playback, no dropped frames over 60 s |
| M4 | Premiere-style panels, docking, workspaces, JKL | Full trim set (ripple/roll/slip/slide) driven by keyboard only |
| M5 | Export pipeline + Instagram preset pack | Exports validate against `docs/SPECS.md` and round-trip probe cleanly |
| M6 | Keyframe/animation system + Effect Controls panel | Bézier interpolation matches golden curve fixtures to 1e-6 |
| M7 | Safe-zone overlays and per-surface framing preview | Reels/Stories/Feed overlays derived from the preset data, not hardcoded |
| M8 | Masks, mattes, precomps (AE layer) | Nested comp renders identically as clip and standalone |
| M9 | Node-based color page + scopes + OCIO managed pipeline | Round-trip through OCIO is idempotent; scopes match reference within tolerance |
| M10 | Audio mixer, busses, loudness metering | Loudness meter agrees with a reference implementation on test tones |
| M11 | Effect plugin ABI + expression engine | A third-party effect loads without recompiling the host |
| M12 | Autosave, crash recovery, project migration | Kill -9 mid-edit → relaunch → ≤1 edit of work lost |

## THE LOOP

For each milestone, repeat until the exit gate passes or the iteration cap hits.

### 1. PLAN
State the milestone, the smallest shippable increment, the files you will touch, and the
**falsifiable check** that will prove it works. If the increment touches architecture,
write an ADR in `docs/adr/NNN-*.md` first: context, options, decision, consequences.

### 2. BUILD
Write the code. Real implementations only. Every public function gets a test in the same
commit.

### 3. VERIFY — mechanical, not vibes
Run all of these and paste **actual output**, not a summary:

```
cmake --build build --parallel        # zero warnings at -Wall -Wextra -Werror
ctest --output-on-failure             # zero failures, zero skips
./build/tools/rf_render_headless \    # renders without a GUI
    tests/fixtures/timeline_a.rfproj --preset presets/ig_reel.json --out /tmp/a.mp4
ffprobe -v error -show_streams /tmp/a.mp4   # matches the preset, field by field
./build/tools/rf_bench --scene tests/scenes/three_layer_1080x1920.rfproj
```

Compare rendered output against golden frames (SSIM ≥ 0.995). Compare bench numbers against
the budgets below. **A milestone is not done because the code compiles.**

### 4. CRITIQUE — adversarial, specific, quoted
Score the increment 1–5 on every rubric item. For each score below 5:
- Quote the exact file, line, and offending text.
- Name the concrete failure mode a user would hit.
- "Could be cleaner" is a rejected critique. Rewrite it as a defect or drop it.

Then answer each explicitly, yes or no with evidence:
- Does any code path silently swallow an error or return a default frame on failure?
- Is there any `TODO`, stub, hardcoded path, or function that returns a fake success?
- Does any test assert only that a call did not crash?
- Does any spec number appear in `.cpp`/`.h` instead of a validated preset file?
- Does this increment leak, race, or allocate on the render thread?
- Would a first-time Premiere user find this control where they expect it?

### 5. REVISE
Fix every critique. Re-run step 3 in full — do not assume an unrelated test still passes.
If a fix introduces a new sub-5 score anywhere, you have not finished the iteration.

### 6. COMMIT & REPORT
One logical change per commit, message explaining *why*. Update `docs/PROGRESS.md` with the
milestone, iteration number, scores, and what remains.

## RUBRIC (score 1–5 each iteration)

1. **Correctness** — does what it claims; verified by executed tests, not by inspection.
2. **Frame accuracy** — seeks, cuts, and exports land on the exact frame; no ±1 drift.
3. **Performance** — inside the budgets below, measured this iteration.
4. **Robustness** — malformed media, missing files, disk full, cancelled export: all
   handled without data loss or crash.
5. **UI fidelity** — matches Premiere's spatial and keyboard conventions; a keyboard-only
   user can complete the workflow.
6. **Architecture** — no layering violations (UI never touches libav directly; the render
   graph never knows about Qt); testable without a GUI.
7. **Spec integrity** — every platform-derived number traceable to a dated source.

## PERFORMANCE BUDGETS (measure every iteration; regression = failed gate)

- Timeline scrub, 1080×1920, 3 layers + 1 grade: sustained **30 fps**, p99 frame ≤ 40 ms.
- Random seek in a long-GOP 4K source: **≤ 150 ms** to displayed frame.
- Project open, 500-clip timeline: **≤ 2 s** to interactive.
- Render thread allocations per frame: **0**.
- Export of a 60 s Reel: at least **2× realtime** on the reference machine (state its specs).
- Idle CPU with app open, playback stopped: **< 2%**.

## HARD RULES

- **Never report a milestone complete without pasted command output proving it.**
- **Never simulate.** No mock encoder standing in for a real one, no placeholder frame
  where decoding failed, no test fixture generated by the code it tests.
- **Never widen scope mid-milestone.** New ideas go in `docs/BACKLOG.md`.
- If blocked twice on the same failure, stop and write a diagnosis: what you expected, what
  happened, the three hypotheses, and the experiment that distinguishes them. Ask before
  the third attempt.
- If the web contradicts an assumption already implemented, open a defect immediately —
  do not quietly keep the old value.

## STOP CONDITIONS

Stop the loop for a milestone when all rubric items score 5 **and** the exit gate passes,
or after **5 iterations**, or when two consecutive iterations produce no score movement.
If you stop without a clean gate, say so plainly and list precisely what is broken and
what you would try next. Do not declare victory to end the loop.

## OUTPUT FORMAT (every response)

```
MILESTONE: Mx — <name>          ITERATION: n/5
PLAN:      <increment + falsifiable check>
DIFF:      <files changed>
VERIFY:    <pasted build/test/bench/ffprobe output>
CRITIQUE:  <scores 1-7 with quoted defects>
NEXT:      <single next action>
```
