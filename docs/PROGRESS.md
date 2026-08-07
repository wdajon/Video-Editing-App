# Progress

## M4 — Premiere-style panels, docking, workspaces, JKL

**Exit gate:** the full trim set (ripple/roll/slip/slide) driven by keyboard
only.

**Gate status: met mechanically on Windows, with two caveats outstanding.**

```
TimelinePanelTest.TheWholeTrimSetAndBackAgainFromTheKeyboard
100% tests passed, 0 tests failed out of 425     (windows-debug, clean tree)
100% tests passed, 0 tests failed out of 425     (windows-release)
```

All four operations and undo, performed by `QTest::keyClick` on a focused
`TimelinePanel` inside a real `QMainWindow`, on the offscreen platform. Nothing
in that test calls the editor, the command map or `make_trim` directly.

**The two caveats are not formalities.**

1. **CI has not run against any of this.** GitHub Actions was in a major outage
   (webhooks throttled to ~15%) from iteration 4 onwards, so iterations 4 and 5
   have only ever been compiled by MSVC. Every earlier milestone leaned on the
   Linux/Clang and sanitizer jobs; this one has not had them.
2. **Nobody has looked at it.** The panel's painting has no oracle (D23) — the
   tests prove it does not crash and that it draws from the document, not that a
   person sees a usable timeline. M3's gate required the project owner to watch
   it run; the same applies here and has not happened. `reelforge --demo-timeline`
   exists so it can.

**JKL now exists** (D21 resolved, iteration 6) and moves the playback clock and
the drawn playhead. It does **not** move a picture (D25) — see below.

### Iteration 1 — the trim set, and the media limit that makes it honest

**Increment:** ripple, roll, slip and slide as undoable commands in `rf_timeline`,
with the reachable range of each computed exactly. **Falsifiable check:** each
operation asserted against the definitions in ADR 009 on a fixed fixture, plus a
property fuzz over 200 random tracks asserting that no trim leaves an illegal
document, that undoing everything returns byte-identical bytes, and that each
operation preserves what its definition says it preserves.

Before a key can be bound to a trim, the trim has to know where to stop — and it
could not. `Clip` recorded `source_in` and `duration` and nothing said how much
media the source had, so a slip could walk past the end of the file and the
failure would surface during playback as a missing picture rather than as a
refused edit. `Clip::source_duration` closes that, and the document now enforces
`0 <= source_in && source_in + duration <= source_duration` on every mutation.
The project format goes to version 2 for the new field.

Three of the four operations move more than one clip, and a half-applied trim
cannot be undone. `Document::replace_track_clips()` takes a whole new clip vector
for one track, validates all of it, and installs it only if every check passes;
the commands capture the previous vector and revert by putting it back. Atomicity
and exact inversion are properties of the primitive rather than promises each
command has to keep.

```
100% tests passed, 0 tests failed out of 327     (windows-debug)
100% tests passed, 0 tests failed out of 327     (windows-release)

timeline = 95 tests (was 51)
[trim fuzz] documents=200 applied=4393 refused=3607
```

Zero warnings at `/W4 /WX`. The refusals in the fuzz are real: a roll needs a
butt-joined neighbour, and a clip already at its media limit has nowhere to go.

**A defect found by writing the critique, not by a test.** The first `TrimRange`
used `std::optional` bounds, with `nullopt` meaning unbounded, on the reasoning
that a clip with nothing after it can slide right forever. That was wrong twice:
a tick is an `std::int64_t`, so the limit is real rather than absent, and leaving
it out let `start += delta` overflow for a delta the range had just declared
legal. The fix was to the API rather than to the caller — `Document` now refuses
a clip whose `start + duration` is not representable, which makes the bound
derivable, and every range is a plain pair of integers. `Trim.ASlideToThe
RepresentableLimitDoesNotOverflow` covers the case that used to be UB.

**What this iteration does not deliver.**

1. **No keyboard, no panels.** The gate says *driven by keyboard only*. This is
   the model layer; the command map, JKL and the docking workspace are the rest
   of M4.
2. **A ripple trims one track** (D15). Premiere ripples every sync-locked track
   together, so a keyboard ripple on V1 would leave a paired A1 clip behind. This
   blocks the M4 gate and is the next thing to fix.
3. **Media length lives on the clip** (D14), not in a media pool, so two clips
   cut from one source repeat the value with nothing enforcing that they agree.

### Iteration 2 — sync lock, and a defect report that was half wrong

**Increment:** D15 — a ripple moved only its own track, leaving downstream
material on every other track behind. **Falsifiable check:** the sync-lock cases
asserted exactly on a two-track fixture, plus the fuzz extended to multi-track
documents with a counter proving it actually crosses a track boundary rather
than merely being able to.

**Checking the ground truth before implementing changed the design.** D15 was
written as *"a ripple on V1 leaves the paired A1 clip behind"*. Adobe's
documentation shows that sentence conflates two features. Sync lock moves
*downstream material on other tracks* — the music bed, the lower third — and
never trims anything. The paired A1 clip is **clip linking**, which ReelForge
does not have. Building linking under sync lock's name would have produced
something that looked finished and still lost sync. D15 is resolved for what it
actually described; the other half is now D16, and ADR 010 records the
distinction so the next session does not read D15 as closing both.

The straddling case turned out to be genuinely hard rather than an oversight: a
clip spanning the ripple point cannot move and cannot stay. Premiere only
answered it recently, with an opt-in preference that *splits* the clip. ReelForge
refuses and names the obstruction, because the alternative — shifting around it —
is the silent desync this iteration exists to remove. The split is D17.

```
100% tests passed, 0 tests failed out of 339     (windows-debug)
100% tests passed, 0 tests failed out of 339     (windows-release)

timeline = 107 tests (was 95)
[trim fuzz] documents=200 applied=4237 refused=3763 crossed=319 straddled=1756
```

`crossed=319` and `straddled=1756` are asserted non-zero. Without them a
multi-track fuzz that never actually crossed a boundary would look identical to
the single-track one it replaced and would prove nothing about ADR 010.

The ripple point is the trimmed clip's **out point for both edges**, which is not
obvious for the in edge and is derived in ADR 010. One rule for both matters:
two ripple points differing by the clip's duration would show up as a silent
frame-level drift on other tracks rather than as an error.

Format version 3 for `Track::sync_locked`. `insert_track_at()` took seven
positional parameters and would have taken eight; it now takes a `Track`, which
is what its only caller already had in hand.

### Iteration 3 — linked clips, and an ADR the fuzz proved wrong

**Increment:** D16 — a ripple on a picture clip left its audio at the old length.
`Clip::link` groups clips that trim together; a trim applies to every member and
the reachable range is the intersection of what each member allows. **Falsifiable
check:** the linked cases asserted on a two-track fixture, plus the fuzz extended
to build linked pairs deliberately, with a counter proving it reaches them.

The intersection is the point of the feature. A pair whose audio has twenty ticks
of tail and whose picture has two hundred stops at twenty, and the two stay
together — a range taken from the picture alone would run the audio past the end
of its media.

**The ADR claimed something false and the fuzz caught it within the hour.**
Decision 2 first said that requiring aligned members made drift "unrepresentable
by construction". It is not. A ripple elsewhere shifts sync-locked tracks; if a
link has its picture on the rippled track and its audio on a track whose sync
lock the user turned *off*, the pair comes apart — and nothing should stop it,
because the user explicitly asked that track to stay put. Premiere behaves the
same way, which is exactly why it has a red out-of-sync indicator.

The fuzz failed on all five trim kinds, including slip, which moves nothing at
all. That was the tell: a slip cannot introduce drift, so the drift had to be
there already. The invariant was rewritten to the one that actually matters:

> A trim applies the same deltas to every member. It never introduces drift. It
> does not undo drift that an earlier edit legitimately created.

ReelForge has no out-of-sync indicator, so a desynced pair is currently silent.
That is a real gap, filed as D18 rather than glossed, and stated in a test so it
is documented behaviour rather than something the next reader discovers.

```
100% tests passed, 0 tests failed out of 360     (windows-debug)
100% tests passed, 0 tests failed out of 360     (windows-release)

timeline = 128 tests (was 107)
[trim fuzz] documents=200 applied=4101 refused=3899 crossed=374 straddled=1724 linked=165
```

`linked=165` is asserted non-zero. Independently laid out tracks essentially
never produce two clips with the same span, so the fuzz builds linked pairs on
purpose; without the counter it would have exercised none and said nothing.

Format version 4 for `Clip::link`.

### Iteration 4 — the command map, and trimming in frames

**Increment:** `rf_edit` — key chords, a remappable command map, editor state
(tool, selection, edge) and the actions that turn a keystroke into a trim.
**Falsifiable check:** `keyboard_workflow_test.cpp` performs all four operations
and undoes them through `Editor::press` with key chords looked up in the map.
Nothing in it calls `make_trim` or names a tick count.

**A prerequisite turned up while sizing the work: nobody trims by ticks.** A tick
is 1/90000 s and Premiere's trim keys move whole frames, so a trim key has to
convert frames to ticks — and the document had no frame rate. ADR 005 gave it a
tick base, which is a resolution, not a rate. `Document::create()` now takes both
and refuses a frame rate whose period is not a whole number of ticks, so a
keyboard trim lands on a frame boundary by construction.

That refusal has a real cost, recorded rather than hidden: **23.976 fps cannot be
created at a 1/90000 base**, because 90000 × 1001 / 24000 is 3753.75. Rounding
would put a fraction of a frame of drift into every edit, which is the class of
error the integer tick model exists to prevent. Choosing a base that expresses
every broadcast rate exactly is D20.

The defaults were read from Adobe's documentation rather than remembered: `V`,
`B`, `N`, `Y`, `U` for the tools, `Ctrl+Left/Right` for a one-frame trim,
`Ctrl+Shift+Left/Right` for the large offset (5 frames, Premiere's default, and a
preference here too). Two bindings differ from Premiere and both are stated in
ADR 012 rather than smuggled: Premiere ships *Select Nearest Edit Point as Ripple
In/Out* unassigned, so a keyboard-only user cannot choose an edge out of the box,
which the gate requires — ReelForge puts them on `[` and `]`.

```
100% tests passed, 0 tests failed out of 403     (windows-debug)
100% tests passed, 0 tests failed out of 403     (windows-release)

edit = 41 tests (new), timeline = 130
```

**Two defects found by the critique, both fixed rather than noted.**
`CommandMap::defaults()` skipped a default binding whose chord failed to parse,
which would have shipped a build with a key that silently did nothing; it now
fails the check loudly. And `EditState` carried a `playhead` that selection wrote
and nothing read — state by appearance, decoration in fact. It is gone until the
actions that need it (Premiere's Q and W) exist, which is the same stance taken
against inventing a linked-selection API before it has a caller.

**What this does not prove:** that a real key press in a real window reaches this
code. `rf_edit` is deliberately Qt-free, which is what makes the workflow
testable headless, but nothing yet routes a `QKeyEvent` into it. That plus
panels, docking, workspaces and JKL is the rest of M4 (D21, D22).

### Iteration 5 — a real key press, a real widget, a real edit

**Increment:** the last hop. `to_key_chord` turns a `QKeyEvent` into a chord,
`TimelinePanel` owns focus and painting, and `MainWindow` docks it and saves
named workspaces. **Falsifiable check:** the whole trim set and its undo driven
by `QTest::keyClick` against the panel, with the document asserted afterwards.

`rf_edit` stays Qt-free, so translation is a free function rather than a widget
method and is tested with synthesised events: every key in the enumeration is
asserted to survive Qt → `Key` → Qt. That caught the trap worth naming — Qt
reports `Qt::KeypadModifier` for the numeric keypad, which would have made
`Ctrl+Right` from the keypad a different chord from `Ctrl+Right` from the arrow
cluster, so a trim from the keypad would silently do nothing. It is masked.

**Only one panel ships**, and that is the point. M0 wrote down that *"a panel
that docks but shows nothing is indistinguishable from a broken panel"*, and the
rule binds harder now than it did then: an empty Project dock inside a finished
looking window is a worse lie than one in an empty shell. The Timeline is the
only panel with content to draw, so it is the only one built. A workspace is
`QMainWindow::saveState()` under a name — ReelForge does not invent a layout
format, and it propagates Qt's refusal of state it does not recognise rather than
leaving a workspace that silently does nothing.

**A build-system bug, found because the convention says to look.** Linking
`Qt6::Test` gave a test binary that built cleanly and died at startup with exit
code 53 and no output: `windeployqt` runs against `reelforge`, which has no
business linking Qt6::Test, so `Qt6Testd.dll` was never copied. `rf_deploy_qt_module`
copies the one extra library — narrower than a second `windeployqt` run, which
would race exactly as it did in M0 iteration 3a. Re-verified after
`rm -rf build/windows-debug`, because a populated `bin/` is what hid that class
of bug the first time.

Two test assertions were also wrong rather than the code: a dock inside a window
that was never shown is neither visible nor hidden, and `hasFocus()` additionally
requires window activation, which the offscreen platform does not grant.
`focusWidget()` is the part that carries meaning.

### Iteration 6 — JKL, wired to a clock rather than to nothing

**Increment:** D21. `rf::edit::Shuttle` is the JKL state machine; `rf::app::Transport`
applies its rate to the `PlaybackClock` M3 already built. **Falsifiable check:**
the ladder asserted rung by rung as exact rationals, the transport driven with an
explicit `now` rather than by waiting, and J/K/L pressed as real key events on
the window with the clock's rate asserted afterwards.

**The sources disagreed, so the ladder is data.** Doubling (1, 2, 4, 8) appears
alongside linear (1, 2, 3, 4), and the Shift behaviour is described both as slow
motion and as a finer step. The mission's rule for that case is to record all the
values, implement the most conservative, and put it behind configuration rather
than a magic number — so the ladder is a `std::vector<Rational>` defaulting to
doubling, and nothing in the transition logic knows how many rungs there are.
`ShuttleTest.TheLadderIsDataAndCanBeReplaced` runs a three-rung linear ladder.

Pressing the opposite key steps **one rung toward zero** rather than jumping to
reverse: a single keystroke that flips direction at 16x is hard to undo by feel,
and this is a control operated without looking.

The rate is a signed `Rational`, so half speed is exactly 1/2. `PlaybackClock`
already took a rational rate and already accepted a negative one, so reverse and
slow motion needed nothing new from it — a good sign the M3 clock was cut at the
right shape.

```
100% tests passed, 0 tests failed out of 453     (windows-debug)
100% tests passed, 0 tests failed out of 453     (windows-release)

edit = 54 tests (was 41), app = 47 (was 32)
```

**What JKL is not connected to, stated plainly (D25).** Pressing L sweeps the
playhead across the Timeline. It does not play video: the Program monitor owns a
Vulkan surface and sits outside the widget tree by design (ADR 013), so nothing
decodes or presents at the shuttle rate. Connecting it needs a device CI cannot
provide. Filed rather than faked.

**A test bug worth recording**, because the same mistake is easy to repeat:
`Nanoseconds` counts from an unspecified epoch, so passing an absolute two
seconds to `refresh_playhead` landed long *before* the window's anchor and read
as frame −6855364. Only differences are meaningful. Exactness lives in the
Transport tests, which own their `now` entirely; the window test only has to
prove the wiring carries.

### Iteration 7 — something to look at

`reelforge --demo-timeline` starts with four two-second clips on V1, each linked
to its sound on A1, every clip carrying two seconds of handle at both ends. It
exists because the two things still blocking M4 are a CI run and a person's eyes,
and the second was impossible: there is no project loading, so an empty window
had nothing to press keys against.

It is a fixture for a human, not a stand-in for loading a project — behind an
explicit flag, built through the ordinary document API, and refusing rather than
appending if the document already holds anything. The tests assert the properties
that make it usable rather than its exact contents: every clip butt-joined (roll
needs a neighbour, slide needs two), handles at both ends, picture linked to
sound, and **every trim in the set succeeding on the first press** — someone
trying the keyboard should not meet a refusal before they have seen anything
work. It is built from `document.ticks_per_frame()` rather than an assumed 3000,
so it is still frame-aligned at 29.97.

```
100% tests passed, 0 tests failed out of 460     (windows-debug)
100% tests passed, 0 tests failed out of 460     (windows-release)

reelforge.exe --demo-timeline  -> still running after 4s (started cleanly)
```

The smoke run matters because no test launches the actual binary with the flag —
the suite builds the document directly, and a broken `main` would not have shown
up in it.

### Iteration 8 — buttons, because the keyboard was unverifiable

**The owner ran the demo and could not tell whether the shortcuts worked.** That
is a defect, not a documentation problem: `B` selects the ripple tool and the
window looked identical afterwards, so three keystrokes in a row changed nothing
visible. A keyboard-first application still has to be learnable, and every editor
solves it the same way — a palette of tools you can click, each naming its key.

**Increment:** a Tools panel of buttons, and state that changes visibly.
**Falsifiable check:** the same edit performed by clicking and by pressing, with
the two serialised documents compared.

Three things make it hold together:

- **A button performs an `Action`; it never reimplements one.** Both routes end
  in `TimelinePanel::perform()`. Two entry points that each interpreted an action
  would be two things to keep in step, and the first divergence would be a button
  quietly doing something its shortcut did not.
- **The label is read from the live `CommandMap`**, never written beside the
  button, so a remapped key relabels itself and no button can advertise a
  shortcut that does nothing. The key is on the face of the button, not only in a
  tooltip — a hint you have to hover to find does not answer "did that work?".
- **Selecting a tool now shows.** The palette checks the active tool and the
  armed edge, and the status bar carries a permanent label: tool, selected clip,
  and the edge for ripple and roll only. Slip and slide use the whole clip, so
  showing an edge would advertise a choice with no effect.

```
100% tests passed, 0 tests failed out of 472     (windows-debug)
100% tests passed, 0 tests failed out of 472     (windows-release, clean tree)

app = 66 tests (was 54)
```

**On Adobe's shortcut page.** The owner supplied it and asked it be the source.
It could not be fetched — two attempts timed out, the page being very large — so
**nothing was changed on a guess**. What ReelForge binds already matches
Premiere for the tools, the trims and JKL (ADR 012, taken from Adobe pages
earlier in M4). The exceptions are still `[` and `]`, now D26.

**Two environment failures worth recording, neither in the code.** A stray
`reelforge.exe` from the earlier smoke run held the binary open and the release
link failed with `LNK1168`; the smoke-run helper should verify termination rather
than assume it. Then the half-written release binaries were refused by Windows
Smart App Control (*"An Application Control policy has blocked this file"*, with
enforcement confirmed on) while debug ran fine. A clean release rebuild cleared
it. Neither was a code defect, and neither would have been diagnosable from the
first error message alone.

**An old test was guarding the wrong thing.** `DoesNotShipEmptyPanelsStandingInForRealOnes`
asserted "exactly one dock" and failed the moment a second *real* panel arrived —
the opposite of its purpose. It now asserts every dock has content and a stable
object name, which is what M0's rule actually says.

### Iteration 9 — Adobe's real defaults, and the mouse

Two reports from the project owner, both correct.

**"The tools do not work."** They effectively did not. Slip and slide required
selecting a tool *and then* pressing a trim chord — two keystrokes before
anything moved, the first changing nothing visible. Adobe's page says Premiere
does not work that way at all.

**"This is also why I didn't want an only-keyboard video editor."** M4's gate is
*the full trim set driven by keyboard only*, and that phrasing quietly became the
whole design. The gate was a floor, not a ceiling, and reading it as a ceiling
was the mistake.

**The page was finally read, and it took a browser rather than a fetch.** Two
`WebFetch` attempts had timed out and I twice reported the page as unavailable
and carried on with what earlier searches suggested. A browser was available the
whole time. Loaded in it, the page reads fine — the failure was mine, not the
source's.

What it says, transcribed into ADR 016 and now bound exactly:

| Command | Key |
|---|---|
| Nudge Clip Selection Left / Right One Frame | `Alt+←` / `Alt+→` |
| Slip Clip Selection Left / Right One Frame | `Ctrl+Alt+←` / `Ctrl+Alt+→` |
| Slide Clip Selection Left / Right One Frame | `Alt+,` / `Alt+.` |
| Step Backward / Forward | `←` / `→` |
| Play | `Space` |

Five-frame variants add `Shift`. **All of these act on the clip selection with no
tool involved** — one chord, and something moves. That is the correction that
matters.

Checked rather than assumed: the page assigns **no** Timeline *Trim Backward /
Forward*, and `Ctrl+←`/`Ctrl+→` appear only under the Program Monitor as *Nudge
Selected Object*. So ReelForge's ripple and roll trim keys are its own and are
now labelled as such instead of implied to be Premiere's (D26).

**The timeline takes a mouse.** Click to select, drag to move, drag the ruler to
scrub. A drag is one undo entry — the clip follows the pointer and a single
command is executed on release, because committing per mouse-move would fill the
history with hundreds of steps. A drag ending where it started is a click, with
no command and no modified flag. An illegal drop is refused and explained rather
than clamped to the nearest legal spot, which would move the clip somewhere
nobody pointed at. Dragging moves the whole link group, as a keyboard nudge does.

**Why the palette looked dead:** `setAutoRaise(true)` draws a `QToolButton` flat,
and a flat checked button is nearly indistinguishable from an unchecked one. The
tool *was* selected; it just looked identical. Auto-raise is off, and the
shortcut is separated with spaces rather than a tab, which `QToolButton` does not
expand.

```
100% tests passed, 0 tests failed out of 488     (windows-debug)
100% tests passed, 0 tests failed out of 488     (windows-release)

edit = 63, app = 73
```

`nudge` joined `TrimKind`, so moving a clip reuses the atomicity, link-group and
undo machinery the trims already had rather than growing a second path.

### Iteration 10 — the Tools strip becomes a tool strip

A screenshot showed what iteration 8 actually produced: a full-width column of
two dozen buttons expanding to fill the window, with the Timeline squeezed behind
it and clip names overlapping button labels. The panel had eaten the application.

**The strip now holds tools; the menu bar holds commands.** Trim, nudge, slip,
slide, selection movement, transport and undo are under Clip, Sequence, Playback
and Edit — where Premiere keeps them and where a shortcut reads off a menu entry.
The strip's width is *fixed*, not merely preferred: an expanding size policy is
what swallowed the window, and a layout that cannot do it again beats a comment
asking the next person not to.

**Tools that share a slot share a button, with a flyout**, as asked for. Ripple
with rolling, slip with slide. `QToolButton::DelayedPopup` is exactly "click to
use, click and hold for the rest" — no custom timer, no reimplemented menu. A key
press moves the slot too, or the strip would claim one tool was active while
showing another's icon.

Only tools that do something: selection, ripple, rolling, slip, slide. Razor,
rate stretch, pen, hand, zoom and type are absent rather than inert — M0's rule
about panels that show nothing applies hardest in a tool strip. Icons are drawn
in a few lines of `QPainter` each, so there are no image files to keep in step.

**The Timeline is now the central widget.** A `QMainWindow` with no central
widget gives the leftover space to nothing, which is why a dead band ran across
the window with the Timeline pinned to the bottom.

```
100% tests passed, 0 tests failed out of 494     (windows-debug)
100% tests passed, 0 tests failed out of 494     (windows-release)
```

**`reelforge --screenshot <file>` was added, and it earned its place
immediately.** Two layout defects in a row reached the project owner because
nothing in a test suite can see that a panel has covered the application. The
first render caught the dead band before it shipped. It works on the offscreen
platform, so it needs no display.

### Iteration 11 — a demo the trim set can actually be tried on

*"Not sure if ripple, roll, slip or slide really do anything."* Three reasons,
two of them mine.

**Slip is invisible, and correctly so.** It moves neither the clip nor its
length — only which frames of the source it shows. With no picture on screen
(D25) there was nothing at all to perceive. The status bar now reads
`demo_2.mov at 60 src 60-120`, so slip changes a number even when it changes no
rectangle. That is a workaround for a missing Program monitor, not a substitute.

**The demo could not run half the set.** Every clip was butt-joined, so a nudge
had nowhere to go and was refused; and it selected the *first* clip, which starts
at zero with nothing to its left, so slide-left and nudge-left were refused too.
The first thing anyone tried failed.

Those two wants are opposites — roll and slide need a butt-joined neighbour,
nudge needs free space, and no clip offers both. So the demo now butt-joins the
first three (ripple, roll, slip and slide all work there, and it is the clip the
demo selects) and puts the last one past a gap (where a nudge has somewhere to
go). Two tests assert exactly that, replacing one that asserted everything was
butt-joined — which had been guarding half the requirement.

```
100% tests passed, 0 tests failed out of 496     (windows-debug)
100% tests passed, 0 tests failed out of 496     (windows-release)
```

### Iteration 12 — the first picture out of a timeline

**Increment:** the question nothing could answer — *given a playhead, what is
visible, in what order, and which frame of each source?* `layers_at()` answers
it as pure document logic; `rf_render` decodes, converts and composites the
result. **Falsifiable check:** eleven tests on the arithmetic with no decoder and
no GPU, eight on the whole path against real media, and a rendered PNG checked
against the video's own burned-in frame counter.

**The verification is the strongest in the project so far, and it came from
outside ReelForge.** `testsrc2` burns its frame number into the picture. Demo
clip 1 starts twenty frames into its source, so timeline frame 5 must show source
frame 25 — the rendered image reads `25`. Frame 30 falls in clip 2 and reads
`30`. That is an oracle the decoder did not compute and no assertion of mine
could fake.

```
100% tests passed, 0 tests failed out of 515     (windows-debug)
100% tests passed, 0 tests failed out of 515     (windows-release)

render = 8 tests (new), timeline = 141 (was 130)
```

Four rules `layers_at()` settles, each of which is a way to get a picture subtly
wrong: half-open coverage, so butt-joined clips show exactly one frame at the
join; bottom-first order, taken from `Document::tracks()` so the two cannot
disagree; **empty is an answer**, so a gap draws black rather than holding the
last frame; and a disabled clip leaves a hole rather than revealing what is
behind it — "skip and keep looking" is the natural way to write that loop and is
wrong.

`rf_render` links no Qt, because M5's export renders the same way into an encoder
rather than onto a screen. Building it once is what stops an exported file and a
previewed frame disagreeing about what the edit is. Decoders stay open between
frames; a test asserts two clips of one file share one.

**The demo timeline was resized to fit real media.** It described four
six-second clips and the shipped fixture is two seconds, so pointed at real media
it would have claimed frames the file never had — exactly what the renderer
refuses to paper over. Clips are now twenty frames from a sixty-frame source.

### Iteration 13 — a picture in the window

`ProgramPanel` docks on the right and shows the frame at the playhead. It follows
a scrub, a step and an edit — the last of those matters most, because a **slip**
changes what is under the playhead without moving anything, and it was the
operation impossible to believe in without seeing it.

```
100% tests passed, 0 tests failed out of 515     (windows-debug)
100% tests passed, 0 tests failed out of 515     (windows-release)
```

Confirmed by rendering the whole window offscreen: the Program dock shows decoded
testsrc2, letterboxed, with its burned-in counter reading `21` — playhead frame 1
on a clip starting twenty frames into its source.

**A deliberate trade, stated rather than buried.** This is a plain `QWidget`
painting what `rf_render` produces, not the Vulkan `QWindow` of ADR 008. It works
with no surface, so it runs offscreen and on a CI runner and shows a picture on
any machine with a device — and it reads pixels back per frame, which M3 measured
at p50 49.9 ms for 1080x1920 with three layers (D13). Fine for scrubbing;
**it will not sustain playback at a real sequence size.** The fast path already
exists and already presents under FIFO. Getting a picture into the window first
was worth more than getting the fast one there eventually.

Three things it refuses to fake: it says *why* there is no picture rather than
going black, it letterboxes rather than stretching (a monitor that changed the
aspect ratio would misrepresent the one thing it exists to show), and it skips
re-rendering a frame already showing, so a drag costs one decode rather than one
per mouse move.

### Next action

There is a picture, and playback is still the gap: pressing `L` sweeps the
playhead and the monitor cannot keep up at a real sequence size, because it reads
back per frame. Routing it through the Vulkan path is the rest of D30.

Still not code I can write: CI against iterations 4–13, and the owner's sign-off.

## M3 — GPU compositor + Program monitor playback

**Exit gate:** 1080x1920, 3 layers, sustained 30 fps playback, no dropped frames
over 60 s.

**Gate status: MET** — 2026-08-05, on the reference machine.

```
reelforge --play reels_1080x1920_30fps_60s.mp4
presented: 1800 frames
dropped:   0
interval p99: 36.67 ms
exit=0
```

Decode, YUV to RGBA conversion, GPU upload, three-layer composite, paced by the
display, presented in a window for a full minute.

**Confirmed visually by the project owner**, who watched the run and reported
the expected image: a vertical window playing colour bars with a red tint and a
white wash, smooth throughout, closing on its own. That confirmation is part of
the gate, not a formality — ADR 008 records that presentation has no automated
oracle, so a machine reporting 1800 frames cannot distinguish a correct picture
from a black one.

### What the gate does not cover

Recorded so the pass is not read as more than it is:

1. **The source is `testsrc2`.** Cheap to decode. Real camera footage at the
   same resolution carries far more entropy and will cost more.
2. **4K is not covered.** D9 stands: CPU decode manages ~66 fps on 4K, and three
   layers at 30 fps needs 90. Hardware decode is still the open item.
3. **YUV to RGBA runs on the CPU** through swscale, per frame. It fits at
   1080x1920; uploading the planes and doing the matrix in a shader is the
   eventual answer.
4. **One frame in flight.** The loop submits and waits. FIFO already paces it,
   so there is headroom, but a heavier scene would want pipelining.
5. **No OpenGL fallback** (D11), which the brief requires as a stack constraint.

**Reference machine for every performance number in this milestone:** AMD Ryzen
9 5900X (12 cores), NVIDIA GeForce RTX 3070 (8 GiB, Vulkan 1.4.341), Windows 11.

### What CI can and cannot prove here

CI runners have no GPU. Linux jobs install Mesa's **lavapipe**, a real Vulkan
implementation running on the CPU, so API misuse, validation errors, wrong
pixels and lifetime bugs are all caught on every commit. It is roughly two
orders of magnitude slower than hardware — `Instance.CreatesAndEnumerates` takes
2.35 s there against milliseconds on the RTX 3070.

**A green CI does not mean the M3 frame-rate gate is met, and never will.** That
number comes from the reference machine and is recorded with its hardware, the
same arrangement as the M1 seek baseline. This is written down because it is
exactly the kind of distinction that erodes quietly.

### Iteration 7 — paced playback, with real decoded video

**The gate's workload, end to end, on the reference machine:**

```
scene:   1080x1920, 3 layers, 30 fps for 60 s
produced 1800 frames in 60.0 s
decoded:  1800 frames from reels_1080x1920_30fps_60s.mp4
dropped:  0        late: 0
interval ms  p50 33.33  p99 35.75  max 37.46  (budget 33.33)
```

H.264 decode, YUV to RGBA conversion, GPU upload, three-layer composite, paced
to the clock, sustained for a full minute with nothing dropped. p50 sits exactly
on the frame period, which is what correct pacing looks like.

Synthetic layers only, without decode, measure p50 33.24 / p99 34.32 — so decode
and conversion cost roughly 1.4 ms of p99 headroom at this resolution.

**The pacer is what makes the zero-drop claim mean anything.** The previous
bench composited as fast as it could, never fell behind, and so could not drop a
frame however slow it was. `Pacer` waits until each frame is due and then
renders whatever the clock says is current — the gap between "the frame I wanted
next" and "the frame that should be showing" is exactly what a drop is. Skipping
to the current frame rather than the stale one is what keeps playback in sync;
falling one frame further behind on each slow frame would drift without bound.
Tests cover renderers at half and one-third of the required rate, because a
pacer that quietly rendered stale frames would look perfect by every other
measure.

Waiting is part of the `Clock` interface so it can be injected. `ManualClock`
jumps to the deadline instead of sleeping, which runs a sixty-second paced
scenario in milliseconds and deterministically, and lets a test simulate a slow
renderer. It counts sleeps too, so a pacer that spins fails a test rather than
merely burning a core.

**What this still does not prove.**

1. **Nothing has been presented to a screen.** The gate says *Program monitor*
   playback; this is a headless paced loop. Presentation is the remaining work.
2. **The source is `testsrc2`**, which is cheap to decode. Real camera footage
   at the same resolution carries far more entropy and will cost more. The 4K
   seek measurement (D9) already shows CPU decode at ~66 fps on 4K, so headroom
   here should not be read as headroom everywhere.
3. **YUV to RGBA runs on the CPU** through swscale, per frame. It fits at
   1080x1920 today. Doing the conversion in the shader by uploading the planes
   is the eventual answer and is the same class of fix that D13 turned out to
   need.

### Iteration 6 — the playback path, and a 217x result

D13 said the shape of the composite API was the problem, not the tuning. Acting
on that rather than optimising further inside the old shape:

```
scene:   1080x1920, 3 layers, 30 fps for 60 s   (RTX 3070)
produced 1800 frames in 0.4 s
dropped:  0
interval ms  p50 0.23  p99 0.42  max 1.61  (budget 33.33)
```

**p50 49.90 ms → 0.23 ms.** The same scene that took 88.5 seconds takes 0.4.
That confirms the iteration 5 diagnosis exactly: the cost was transfers, never
the blending.

`Texture` holds an image on the device. `Compositor::composite_into()` takes
GPU-resident layers and writes a GPU target — no upload, no readback. A still
layer uploads once when created; a video layer will upload once per decoded
frame; the result stays on the device for presentation.

`composite()` is kept for export and golden frames and is now **implemented on
top of `composite_into()`**, so the two paths cannot disagree about what a
composite means, and the existing sixteen compositor tests exercise the new code.

Each layer gets its own descriptor set. A set referenced by a submitted command
buffer must not be rewritten, so updating one between dispatches would have been
a race that validation catches on some drivers and not others.

**What this does not prove.** The bench composites as fast as it can rather than
pacing to the clock, so it never falls behind and its zero-drop result is close
to trivial. What it establishes is capacity: 0.23 ms against a 33.33 ms budget,
roughly 145x headroom. Paced playback with presentation is still untested, and
the gate needs it.

**A self-inflicted defect worth recording.** Editing these documents with
PowerShell's `Get-Content -Raw` / `Set-Content -Encoding utf8` corrupted every
em-dash: PowerShell 5.1 reads UTF-8 as ANSI by default, so the round trip
double-encoded them, and it added a BOM. Repaired by reading and writing through
`System.IO.File` with an explicit no-BOM UTF-8 encoding. Documentation and source
files are edited with tools that preserve encoding, not with shell text
substitution.

### Iteration 5 — measured sustained composite, and why it fails

**Gate shape measured on the reference machine (RTX 3070, RelWithDebInfo):**

```
scene:   1080x1920, 3 layers, 30 fps for 60 s
produced 1800 frames in 88.5 s
dropped:  856
interval ms  p50 49.90  p99 53.53  max 58.31  (budget 33.33)
no dropped frames: FAIL
p99 <= 40 ms:      FAIL
```

**The first hypothesis was wrong.** Per-frame allocation looked like the obvious
culprit -- `composite()` was creating two images, memory, views, a descriptor
pool, a command buffer and a fence every frame, which is precisely what the
"zero allocations on the render thread" budget forbids. Caching them moved p50
from 54.23 ms to 49.90 ms. Real, worth keeping, and about 4 ms of a 54 ms
problem.

**What actually dominates, from a scaling experiment rather than a guess:**

| Scene | p50 |
|---|---|
| 1080x1920, 3 layers | 48.75 ms |
| 540x960, 3 layers (quarter the pixels) | 8.75 ms |
| 270x480, 3 layers (one sixteenth) | 1.73 ms |
| 1080x1920, **1** layer | 38.53 ms |

Cost tracks pixel count, not dispatch count. Going from three layers to one
saves only 10 ms, so each layer costs about 5 ms -- its 8.3 MB upload -- leaving
a **fixed cost near 33 ms** that is the 8.3 MB readback plus the full stall on
the fence. The blending itself is a small fraction of the frame.

**Conclusion: the API shape cannot meet the budget, and tuning inside it will
not fix that.** `composite()` takes CPU pixels and returns CPU pixels,
synchronously. That is about 33 MB across PCIe per frame with no overlap, and it
is the right shape for tests and for export, where correctness matters and
latency does not. It is the wrong shape for playback.

The playback path needs three things this API cannot express (tracked as D13):

1. **Frames resident on the GPU.** A decoded frame should be uploaded once, not
   re-uploaded every time it is composited. Static layers should never re-upload.
2. **No readback.** Playback presents from the GPU image; pulling 8.3 MB back to
   the CPU only to hand it to a window is the single largest cost measured here.
3. **Frames in flight.** Stalling on a fence every frame serialises CPU and GPU
   completely.

The compositor is correct -- 265 tests, exact where the arithmetic is exact -- and
it is kept as the export and golden-frame path. Playback needs a different entry
point into the same pipeline, not a different compositor.

### Iteration 3 — headless compute render, verified pixel-exact

The whole compute path end to end: logical device, memory type selection,
storage image, descriptors, embedded SPIR-V, compute pipeline, layout barriers,
submission, fence, readback. Verified on the RTX 3070 and on lavapipe, including
a full 1080x1920 frame.

```
100% tests passed, 0 tests failed out of 249
```

**A crash was the most useful result.** `Device.OpensThePreferredDevice` died
with `0xc0000409` — a hard fault, not a failed assertion. The test helper let an
`Instance` be destroyed while a `Device` created from it was still alive, which
is undefined behaviour in Vulkan.

The fix went into the API, not the test. `Instance` now holds its implementation
in a `shared_ptr` and `Device` keeps a copy, so the instance outlives every
device made from it. An API where that rule is only documented is an API where
it gets violated, and the first test to touch the boundary proved it. The shared
pointer is declared first in `Device::Impl` so it is destroyed last — members die
in reverse declaration order, and the `VkDevice` has to go first.

**Two choices that keep the test able to fail:**

- The bring-up pattern is deliberately not a gradient. A gradient is symmetric
  enough that transposed axes, a wrong row stride, or a half-dispatched image
  can all still look plausible; encoding x and y differently makes those
  mistakes produce a mismatch.
- The expected image is written from the shader's specification on the CPU,
  never captured from a GPU run, so a wrong shader cannot quietly become the
  expected answer. Comparison is exact equality — `float(v % 256) / 255.0` into
  rgba8 unorm rounds back to `v % 256`.

`61x37` against an 8x8 workgroup is tested specifically: dispatches round up, so
the shader must bounds-check, and the edges must still be written rather than
left as whatever the allocation happened to contain.

### Iteration 2 — Vulkan device layer

Instance creation, device enumeration, capability reporting and selection, plus
`rf_gpu_info` so a user reporting a rendering problem can say what they are
running without installing a Vulkan SDK.

Vulkan is loaded dynamically through volk rather than linked against the SDK:
vcpkg's `vulkan` port needs a `VULKAN_SDK` variable, which would make a clean
clone fail on a machine without a manual install — the hidden prerequisite ADR
001 exists to prevent. It also means a machine with no driver is a runtime
condition the editor can report rather than a link error that stops it starting.

Vulkan include directories are PRIVATE to `rf_gpu`, so ADR 007's layering rule
is a compiler error rather than a review comment.

The ASan job immediately caught a 128-byte leak from the Vulkan loader's
process-global state, which it frees only on library unload while volk never
calls `dlclose`. Suppressed narrowly rather than by disabling leak detection for
the binary: `rf_gpu` is where leak detection matters most, since every Vulkan
object in the project is freed by hand there. `print_suppressions=1` stays on so
a stale suppression becomes visible (D12).

### Iteration 1 — the playback clock and frame accounting

Built before any GPU code, for the same reason M1's time model came before the
decoder: **the gate's claim is only as good as its definition.**

Three ways "no dropped frames over 60 s" gets faked, all rejected explicitly in
ADR 006 — averaging (1,800 frames in 60 s averages 30 fps and can stutter
throughout), deriving position from a frame counter (which makes drops
impossible by construction and the metric worthless), and floating-point time
(which drifts off the frame grid exactly as M1 spent a milestone avoiding).

Position is a pure function of elapsed wall time, never accumulated, so a
stalled renderer changes what the user sees without changing where playback is.
That is what makes a drop detectable rather than absorbed.

The clock is injected, so the full 1,800-frame gate scenario is a unit test that
runs in milliseconds instead of a 60-second sleep.

A drop is defined narrowly and each exclusion has a test: a late frame that is
still the right frame is pacing, not a drop; a repeated frame while paused is
not; frames jumped over by a seek were never due. Conflating them would report
two different failures as one and send the fix in the wrong direction. The
inverse test matters as much — a renderer managing only 15 fps must be reported
as dropping 899 frames, or the gate is decorative.

**One real defect, caught by a round-trip test written for exactly this class:**
`time_of_frame` rounded to nearest, which can land a nanosecond before the true
boundary, and `frame_at` then floors to the previous frame. The correct rounding
depends on direction of play — forward wants the earliest instant at or after
the boundary, reverse the latest at or before.

### Remaining for M3

1. Composite multiple layers with alpha, against golden frames.
2. Present to the Program monitor (swapchain), which is the first code that
   needs a window.
3. Measure sustained playback on the reference machine and record it here.
4. Hardware-accelerated decode, without which D9's 150 ms seek budget stays
   unmet and playback of 4K source cannot feed the compositor.

## M2 — Timeline data model + undo/redo command stack

**Exit gate:** 10,000-op fuzz on the command stack; undo returns to a
byte-identical state. **Gate status: PASSED.**

```
[fuzz] attempted=10000 applied=9060 rejected=940
100% tests passed, 0 tests failed out of 194
```

10,000 randomly generated edits, 9,060 of which applied. All 9,060 undone in
reverse; the document then matched its starting state on all three checks:
serialised bytes, structural equality, and the id counter.

**The 90.6% apply rate is part of the evidence, not trivia.** A fuzz that mostly
generates rejected edits exercises validation and never touches an inverse, and
would pass with every undo path broken. The generator therefore builds commands
against the document's actual current contents. The counter is asserted: the test
fails if fewer than half the attempts apply.

**Why the gate is checked three ways.** Byte equality alone would keep passing if
the serialiser omitted a field — both sides would omit it identically while undo
silently lost it forever. Structural equality alone would miss clip ordering and
id-counter drift, which serialise differently but compare equal member by member.
Neither check subsumes the other.

### What the design had to get right

- **Ids are returned on undo.** A command that spends an id restores the counter
  when reverted. Without it every visible object matches and the bytes still
  differ — which is exactly the failure byte-comparison exists to catch.
- **Redo reuses the id it originally issued.** A redo that mints a fresh id
  produces a document that looks correct and is not the one the user undid.
  `CreatingCommand` holds both rules so no individual command can forget one.
- **A failed command never enters the history.** Otherwise the next undo would
  reverse an edit that never happened.
- **Flag commands record the previous value** rather than flipping. Setting a
  flag to the value it already holds is a no-op whose inverse must also be a
  no-op; a flip would turn a no-op edit into a change on undo.

### Notes

The one failure during this milestone was **a defect in a test, not in the
code**: `DeepHistoryUndoesInReverseOrder` undid the whole stack while comparing
against a snapshot taken after a track was added. The implementation had
correctly returned the document to pristine — id counter back to 1 — and the
expectation was simply wrong. Corrected to undo only the 50 clip additions, then
assert the pristine state explicitly.

A `SetFlagCommand` template over a pointer-to-member was written and then
replaced with three plain classes before it ever compiled. It required explicit
specialisations, which is precisely the corner where MSVC and Clang disagree, and
it saved fewer lines than it risked — a lesson taken from the `<algorithm>`
failure earlier the same day.


## M1 — Media engine: probe, decode, frame-accurate seek

**Exit gate:** seek to any frame of a 10-minute 4K file; decoded hash matches a
reference.

**Gate status: PASSED for accuracy, performance budget NOT met.**

Measured 2026-08-04 on the reference machine (AMD Ryzen 9 5900X, 12 cores;
Windows 11; RelWithDebInfo; source on a local drive) against
`testsrc2_3840x2160_30fps_600s.mp4` — 10 minutes, 3840x2160, 30/1, GOP 250,
18,000 frames, 2.9 GB, generated per `tests/fixtures/media/README.md`:

```
linear decode: 18000 frames in 260.1 s (69.2 fps), 18000 distinct hashes
seeks: 200  mismatches: 0
latency ms  mean 193.7  p50 189.4  p95 348.8  p99 355.2  max 373.7  (budget 150)
accuracy: PASS
budget:   FAIL
```

200 random seeks, zero mismatches, against a linear-decode reference in which
all 18,000 hashes are distinct — so the comparison cannot pass vacuously. That
is the exit gate as written, and it is met.

The 150 ms random-seek budget from the performance section is **not** met.

### Seek latency baseline (reference machine)

| Change | mean | p99 | Notes |
|---|---|---|---|
| As first measured | 1133 ms | 2534 ms | Single-threaded decode; every discarded frame copied |
| + decoder threading | 519 ms | 1119 ms | `thread_count = 0` was never set — a defect, not a tuning knob |
| + no copy of discarded frames | **194 ms** | **355 ms** | Seek scan reads timestamps off the `AVFrame` and unrefs |

5.8x faster overall, and still 2.4x over budget. The remainder is not reachable
by tuning this loop: an average seek must decode ~125 frames of 4K, and CPU
H.264 will not do that in 150 ms at 2160p. Tracked as **D9**, owned by M3, whose
fix is hardware decode plus proxy media.

**Two predictions I got wrong, both corrected only by measuring.** Threading was
predicted at ~8x and delivered 1.33x on linear decode. The discarded-frame copy
was recorded in the backlog as an M3 concern and was in fact the dominant cost at
M1. Neither was visible to 140 passing tests or five green CI runs, because
nothing measured throughput.

### Guarding the fixes

`rf_seek_check` is a manual tool, so on its own it guards nothing. Two mechanical
checks now stand behind these results:

- **`SeekCost` tests** assert that a seek materialises exactly **one** frame,
  counting copies rather than milliseconds. Deterministic, resolution-independent,
  and immune to CI timing noise — this is what actually catches the copy
  regression coming back.
- **CI runs `rf_seek_check`** on the committed fixtures on every optimised build,
  proving accuracy on each commit. It cannot prove the 4K budget: the fixtures
  are 320x240 and the real source is 2.9 GB. That limitation is stated rather
  than papered over.

### Iteration 3 — sequential decode and frame hashing

**Increment:** walk a file's video stream frame by frame, and hash frames
reproducibly. Linear decode is not just a feature — it is the **oracle** the
next increment's seek accuracy is measured against.

**Falsifiable check:** a 60-frame fixture yields exactly 60 frames; NTSC frame
spacing is exactly 1001 ticks in a 1/30000 base with no drift across the file;
decoding the same file twice yields identical hashes; and all 60 hashes are
distinct.

**Verified (2026-08-04, MSVC 19.44.35228, FFmpeg 8.1.2, Debug):**

```
100% tests passed, 0 tests failed out of 130
app = 10 tests    core = 48 tests    media = 72 tests
```

**Design decisions worth recording:**

- **Draining is explicit.** Codecs with B-frames hold frames back until told the
  stream ended. A decoder that omits the null-packet flush loses the last frames
  of every file, and loses them *silently*. `DecodesEveryFrameIncludingTheOnes
  HeldBackForReordering` exists to fail if that flush is ever removed.
- **End of stream is `Result<optional<VideoFrame>>`.** Running out of frames is
  an outcome, not a fault; making it an error code would force every caller to
  special-case a sentinel and would eventually get mistaken for a real failure.
- **Frames are tightly packed** (`av_image_copy_to_buffer`, alignment 1). Stride
  padding is uninitialised memory that varies by platform and decoder build; if
  it reached the hash, the seek oracle would produce false mismatches and there
  would be no way to tell them from real ones.
- **`best_effort_timestamp` over raw `pts`,** because raw pts is frequently
  absent on the first frames of a stream.
- **Every libav resource has an RAII owner** and there are no bare `av_*_free`
  calls in `decoder.cpp`. The error paths are numerous and each one returns
  early; this is what keeps them leak-free without per-path cleanup.
- **`describe_stream` was lifted out of `probe.cpp` into a shared internal
  unit.** The decoder must describe its stream identically to the probe;
  two translations drifting apart would mean a file reports one frame rate when
  inspected and a different one when played.

**Two tests exist to stop the next increment passing vacuously.**
`FrameHash.IsDeterministicAcrossDecodes` and `FrameHash.DistinguishesDifferent
Frames` establish that the hash actually separates frames. Without them, a seek
test comparing hashes could pass while proving nothing at all.

### Iteration 2 — probe

**Increment:** open a container, describe its streams, close it. No decoding, no
seeking. The first code in the project that links libav.

**Falsifiable check:** probing three real files reports exactly what `ffprobe`
reports for them, field by field, including a 30000/1001 frame rate surviving as
an exact ratio; and every malformed-input path returns a usable error instead of
crashing or returning a plausible-looking empty result.

**Verified (2026-08-04, MSVC 19.44.35228, FFmpeg 8.1.2, Debug):**

```
[11/21] Building CXX object src\media\CMakeFiles\rf_media.dir\probe.cpp.obj
[16/21] Linking CXX executable bin\rf_media_tests.exe     <- zero warnings at /W4 /WX

100% tests passed, 0 tests failed out of 113
app = 10 tests    core = 48 tests    media = 55 tests
```

**Design decisions worth recording:**

- **Unknown is `std::optional`, never a sentinel or a default.** Audio streams
  really do report `0/0` for frame rate; some containers leave aspect ratio at
  `0/1`. Those arrive as `nullopt`, so nothing downstream can consume an invented
  25 or 30 fps as though the file had stated it. A test asserts this specifically.
- **A zero time base is left at zero** rather than defaulted to something
  plausible, so a caller can detect an unusable stream with `is_zero()` instead
  of silently computing wrong timestamps from a fabricated base.
- **`container_frame_count` is documented as untrusted.** Containers lie; the
  honest count comes from decoding. It is kept for diagnostics and cross-checks,
  and named so nobody seeks with it.
- **FFmpeg include directories are `PRIVATE`**, so the ADR 004 layering rule --
  nothing above `rf_media` includes a libav header -- is enforced by the build
  rather than by review.
- **libav errors keep both the text and the numeric code.** `av_strerror` alone
  frequently yields "Invalid argument", which is not actionable in a bug report.

**Fixture provenance is recorded and its limit stated.** `tests/fixtures/media/`
carries small committed files with the exact `ffmpeg` command that produced each
one, plus the `ffprobe` output the test expectations are drawn from. They are
committed rather than generated at test time because a test that needs an
`ffmpeg` binary on the machine fails for reasons unrelated to ReelForge.

These fixtures are **adequate for probe and demux tests and explicitly not
adequate for the M1 exit gate**: they were produced by the FFmpeg command-line
tool, and a decoder cannot be its own oracle. The gate needs real footage or a
reference hash from an independent decoder. Until it has one, the gate is not
met and will not be claimed as met.

### Iteration 1 — exact rational time arithmetic

**Increment:** the timestamp representation, before any libav code. Frame
accuracy is decided by the number type, not by the decoder: 1001/30000 is not
representable in binary floating point, so a float pipeline accumulates error
and lands a cut a frame off after a few thousand frames. That is the ±1 drift
the rubric forbids, and no amount of care downstream fixes a representation that
permits it.

**Falsifiable check:** every frame of a 10-minute 29.97 fps timeline converts to
a 1/90000 tick base and back to exactly the frame it started on — all 17,982 of
them, not a sample.

**Verified (2026-08-04, MSVC 19.44.35228, Debug):**

```
[4/17]  Building CXX object src\media\CMakeFiles\rf_media.dir\rational.cpp.obj
[10/17] Linking CXX executable bin\rf_media_tests.exe        <- zero warnings at /W4 /WX

100% tests passed, 0 tests failed out of 100
app = 10 tests    core = 48 tests    media = 42 tests
```

**Design decisions worth recording:**

- `Rational` is always gcd-normalised with a positive denominator, so equality is
  structural: `24000/1001 == 48000/2002` without a comparison epsilon. Every
  "is this 23.976?" check in the codebase depends on that being reliable.
- Ordering cross-multiplies at 128-bit width. In int64 it silently gives the
  wrong answer for large values; in floating point it gives the wrong answer for
  near-equal ones.
- `mul_div` and `rescale` carry a full 64x64→128 intermediate, hand-written from
  32-bit limbs rather than `_umul128` or `unsigned __int128`. One implementation
  to reason about and test on every target beats a platform split in the code
  every timestamp flows through, and this runs at frame rate, not pixel rate.
- Overflow is always an error value, never a wrap. A wrapped timestamp is a seek
  to the wrong frame that reports success — the exact failure mode the rubric's
  "silently swallow an error" question is asking about.
- `approximate()` is deliberately not called `to_double`, and is not an implicit
  conversion, so using a float inside timing logic is visible at the call site.

**Environmental defect found and guarded (from the log, not from a guess):**

FFmpeg's vcpkg build runs under MSYS/autotools and passes the vcpkg library
directory to `link.exe` as an **unquoted** `-libpath`:

```
link.exe ... -libpath:A:/Development/Claude/Video Editing app/build/...
LINK : fatal error LNK1181: cannot open input file 'Editing.obj'
```

The checkout path contains spaces, the argument splits, and `Editing` becomes a
phantom object file. Worked around with
`-DVCPKG_INSTALLED_DIR=<space-free path>`; FFmpeg then builds in ~7 minutes. A
guard now runs **before** `project()` and fails in a second with both remedies
named, rather than letting it surface minutes deep inside a vcpkg install.

**Process note:** the first configure after this fix still failed, because CMake
files were edited while a configure was in flight and it read a mixed state. Not
a code defect, but a repeat-able way to waste a cycle: leave the tree alone while
a configure runs.



## M0 — Repo, CMake, dep manifest, CI, empty Qt shell

**Exit gate:** clean clone → build → run on 2 OSes; CI green.
**Gate status: PASSED** — 2026-08-04, run
[30891518538](https://github.com/wdajon/Video-Editing-App/actions/runs/30891518538)
at `6a93924`, attempt 1, all six jobs green from a clean checkout:

| Job | Result | Duration |
|---|---|---|
| Windows x64 Debug | success | 2.5 min |
| Windows x64 Release | success | 2.6 min |
| Linux x64 Debug | success | 1.8 min |
| Linux x64 Release | success | 1.7 min |
| Linux ASan+UBSan | success | 1.6 min |
| Linux TSan | success | 1.7 min |

Every job runs configure → build → `ctest` → both binaries executed, the GUI
shell under the offscreen platform so a green run means it initialises rather
than merely links.

### Iteration 1 — build system and core error handling

**Increment:** a buildable, tested C++20 skeleton with the error-handling
foundation every later module returns through, plus the dependency manifest and
warning/sanitizer policy. Deliberately excludes Qt: acquiring Qt is a separate
decision (ADR 001) and bundling it into this iteration would have meant reporting
"the build works" without ever having run it.

**Falsifiable check:** configure → build at `/W4 /WX` with zero warnings → 48
GoogleTest cases pass in both Debug and RelWithDebInfo → the smoke binary runs
and prints build identity.

**Verified (2026-08-04, MSVC 19.44.35228, CMake 3.31.6, Ninja 1.12.1, vcpkg 2026-07-27):**

- `cmake --build build/windows-debug --parallel` — 11 targets, zero warnings, zero errors
- `ctest --preset windows-debug` — 48/48 passed, 0 failed, 0 skipped, 1.43 s
- `ctest --preset windows-release` — 48/48 passed, 0 failed, 0 skipped, 1.03 s
- `build/windows-release/bin/rf_version.exe` — runs, prints version/compiler/target

**Scores:**

| # | Rubric item | Score | Justification |
|---|---|---|---|
| 1 | Correctness | 4 | 48 executed tests, including death tests for every misuse path of `Result`. Not 5: the code has never been compiled by GCC or Clang, and the non-MSVC warning set is materially stricter (`-Wconversion`, `-Wold-style-cast`, `-Wsign-conversion`). Portability is asserted, not demonstrated. |
| 2 | Frame accuracy | n/a | No media path in this increment. |
| 3 | Performance | n/a | No render path in this increment. No budget applies yet. |
| 4 | Robustness | 4 | Failure is a value everywhere; misuse aborts loudly instead of returning a plausible default; `dev_env.ps1` fails with an actionable message on every missing prerequisite. Not 5: defect D1 — the embedded git revision can be stale. |
| 5 | UI fidelity | n/a | No UI in this increment. |
| 6 | Architecture | 4 | `rf_core` has zero third-party dependencies and no knowledge of Qt, libav, or the GPU; everything is testable headlessly. Not 5: there is not yet enough structure for the layering rule to have been tested by anything. |
| 7 | Spec integrity | 4 | `docs/SPECS.md` records every value with a dated primary source and resolves the three named disputes with stated reasoning. Not 5: the 4:5 feed section is `UNSPECIFIED`, and no preset file or schema exists yet to hold the values. |

**Checklist:**

| Question | Answer |
|---|---|
| Does any code path silently swallow an error or return a default on failure? | **No.** `Result` is `[[nodiscard]]` at class scope; `value()` on a failed result aborts (`ResultDeath.ValueOnErrorAborts`). |
| Any `TODO`, stub, hardcoded path, or fake success? | **No.** No `TODO` markers; no hardcoded toolchain paths (`vswhere` discovery); `rf_version` does real work. |
| Does any test assert only that a call did not crash? | **No.** Every test asserts a specific value, message, or abort message. |
| Does any spec number appear in `.cpp`/`.h`? | **No.** No delivery specs are implemented yet; `docs/SPECS.md` holds them and `presets/` does not exist. |
| Does this increment leak, race, or allocate on the render thread? | **No render thread exists yet.** Unverified by tooling: ASan/TSan presets are defined but have not been run — see D3. |
| Would a first-time Premiere user find this control where they expect it? | **n/a.** No UI. |

### Iteration 2 — Qt application shell

**Increment:** the Qt 6 shell M0 asks for — a real window with a real menu bar
and status bar — plus the acquisition path that makes Qt reproducible on a clean
machine and in CI. Deliberately no empty dock widgets standing in for the M4
panels: a panel that docks but shows nothing is indistinguishable from a broken
panel.

**Falsifiable check:** clean configure finds Qt through `QT_ROOT` → build at
`/W4 /WX` with zero warnings → 58 tests pass in Debug and RelWithDebInfo,
including widget tests that construct a real `MainWindow` with no display server
→ `reelforge.exe` starts, shows a titled window, and exits 0 on window close.

**Verified (2026-08-04, Qt 6.10.3, MSVC 19.44.35228):**

- `cmake --build build/windows-debug --parallel` — 21 targets, zero warnings
- `ctest --preset windows-debug` — 58/58 passed (48 core + 10 app), 2.03 s
- `ctest --preset windows-release` — 58/58 passed, 3.66 s
- `reelforge.exe` — ran 4 s, window title `ReelForge`, 54.3 MB working set,
  **0% CPU while idle** (budget: < 2%), closed cleanly with exit code 0

**Scores:**

| # | Rubric item | Score | Justification |
|---|---|---|---|
| 1 | Correctness | 4 | 58 executed tests; widget behaviour is asserted, not assumed. Still no GCC/Clang compile — unchanged from iteration 1. |
| 2 | Frame accuracy | n/a | No media path. |
| 3 | Performance | 5 | The only budget applicable to this increment — idle CPU < 2% — is met at 0%, measured over 3 s. |
| 4 | Robustness | 4 | Every failure path in the Qt plumbing produces an actionable message (missing Qt names `scripts/install_qt.ps1`; missing `windeployqt` and missing offscreen plugin each fail configuration rather than producing a binary that cannot start). Unchanged defect D1. |
| 5 | UI fidelity | 3 | Title bar follows Premiere's `App - path *` convention and Quit has a keyboard shortcut. That is the whole of the UI. There is no panel, no docking, no workspace, no JKL — all M4. Scored against what a Premiere user would expect to *find*, this is honestly a 3. |
| 6 | Architecture | 5 | The layering rule now has something to test it: `rf_core` does not link Qt, `window_title.cpp` holds the title format with no Qt dependency and is unit-tested without one, and `rf_app` is the only target that knows Qt exists. |
| 7 | Spec integrity | 4 | Unchanged from iteration 1; no new platform-derived numbers were introduced. |

**Checklist:** no silent error swallowing · no TODOs or stubs (the window has no
placeholder panels) · no test asserts merely "didn't crash" · no spec number in
C++ · no render thread yet; ASan/TSan still unrun (D3) · a Premiere user would
find File > Exit where they expect it and nothing else, because nothing else
exists yet.

**Two real defects found and fixed by running things rather than reading them:**

1. Qt 6.12.0 and 6.11.x cannot be installed at all by the current `aqtinstall`;
   the pin moved to 6.10.3 with the measured boundary recorded in ADR 003.
2. `windeployqt` deploys only the `qwindows` platform plugin, so the widget test
   binary aborted at startup with exit code 3 and no assertion output. Fixed by
   deploying `Qt6::QOffscreenIntegrationPlugin` explicitly. Had the test binary
   relied on a CTest environment property instead, discovery would have failed
   the same way — noted in `tests/app/main.cpp`.

### Iteration 3 — first CI run against real Linux

**Increment:** push to a remote and let the six-job matrix compile this code with
a compiler other than MSVC for the first time.

**Result — the prediction was wrong, in the good direction.** Iterations 1 and 2
both recorded an expectation that Clang would reject code MSVC accepted, because
`-Wconversion`, `-Wsign-conversion` and `-Wold-style-cast` had never run against
it. All four Linux jobs passed on the first attempt, including the moc-generated
sources that were called out as the most likely casualty:

```
[success] Linux x64 Debug        [success] Linux ASan+UBSan
[success] Linux x64 Release      [success] Linux TSan
```

ASan+UBSan and TSan passing on real hardware retires the "sanitizers are defined
but have never been run" caveat carried since iteration 1 (D3).

**What did break was the CI script, not the code.** The Windows job failed before
compiling anything, in the Qt install step:

```
py7zr.exceptions.Bad7zFile: Specified path is bad:
  lib/cmake/Qt6Gui/Qt6QWebpPluginTargets-relwithdebinfo.cmake
```

The first hypothesis was a path defect: the workflow built the install prefix by
appending `/Qt` to a Windows `$GITHUB_WORKSPACE`, producing the mixed-separator
`D:\a\Video-Editing-App\Video-Editing-App/Qt`. That hypothesis was **wrong** —
the Windows Release job ran the identical step in the same workflow run and
succeeded. The failure is intermittent extraction inside aqtinstall's bundled
py7zr, not anything deterministic about our inputs.

Fixed by retrying the install up to three times, wiping the partially extracted
tree first because it is not resumable, and failing loudly if `Qt6Config.cmake`
is still absent afterwards. The same retry is applied to `scripts/install_qt.ps1`
and `scripts/bootstrap_linux.sh` so all three acquisition paths behave alike. The
mixed separator was tidied with `cygpath` as well — it was not the bug, but it
was sloppy.

**Method note:** the first fix was nearly applied on the strength of a plausible
reading of the log. Re-running the failed job first cost one command and showed
the theory was wrong. A deterministic-looking failure is not deterministic until
it has failed twice.

### Iteration 3a — a race in the build system, found by CI after the gate passed

The gate run at `6a93924` was green. The very next commit (`891d1d9`, an Actions
version bump) turned Windows Debug red at the **Build** step:

```
FAILED: [code=1] bin/rf_app_tests.exe
Cannot copy C:\Windows\system32\icuuc.dll to ...\build\windows-debug\bin\icuuc.dll:
  Cannot open for output: Existing file ...\bin\icuuc.dll is not writable
```

`rf_deploy_qt` was attached to both `reelforge` and `rf_app_tests`, which share a
single output directory. Under `--parallel`, two `windeployqt` invocations raced
to write the same DLLs. Deployment now happens once, on `reelforge`, and
`rf_app_tests` takes a build dependency on it so the two are ordered.

**Two things this exposes about the earlier iterations, both worth keeping:**

1. **Iteration 2's checklist answer was wrong.** The question "does this increment
   leak, race, or allocate on the render thread?" was answered "no render thread
   exists yet". There was no render thread, but there *was* a race — in the build
   system, shipped in the same increment. The question deserved a wider reading
   than it got.
2. **Local verification was invalid and looked fine.** Every local build reused a
   `bin/` that was already populated, so `windeployqt` reported "up to date" and
   never wrote anything, so the two invocations never contended. Only a clean
   tree exercises the copy. The fix was re-verified after
   `rm -rf build/windows-debug`, which is now the standard for anything touching
   the build system.

### Gate blockers, resolved

| Raised | Blocker | Resolution |
|---|---|---|
| i1 | Never built on a second OS | Linux Debug/Release green on `ubuntu-24.04` with Clang |
| i1 | CI never executed (D3) | Six-job matrix green; ASan, UBSan and TSan all run on real hardware |
| i1 | No Qt shell | Delivered in iteration 2 |

Local Linux verification was attempted first and abandoned on evidence: this
machine's Ryzen 9 5900X reports `VirtualizationFirmwareEnabled: False`, so AMD-V
is disabled in firmware and no hypervisor — WSL2 included — can start. CI runners
are the substitute. `scripts/bootstrap_linux.sh` remains the local path for
anyone whose firmware allows it.

### Milestone closed

M0 is complete. Remaining known defects (D1, D2, D4, D5) are tracked in
`docs/BACKLOG.md`; none of them gate M0, and each is scheduled against the
milestone that first depends on it.

### Next action

Begin M1 — media engine: probe, decode, frame-accurate seek. Exit gate: seek to
any frame of a 10-minute 4K file with the decoded hash matching a reference.
