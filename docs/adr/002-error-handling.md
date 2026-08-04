# ADR 002 — Error handling: `rf::Result`, not exceptions

- **Status:** Accepted
- **Date:** 2026-08-04
- **Milestone:** M0

## Context

The mission brief requires "no exceptions across module boundaries,
`std::expected`-style error returns". Two things force the issue independently
of taste:

1. **The render thread.** Exception unwinding through a job-system worker that
   holds GPU command buffers and decoder contexts is difficult to make correct,
   and the cost is paid on paths that must not allocate.
2. **The plugin ABI (M11).** Exceptions cannot cross a C ABI boundary. A
   third-party effect compiled by a different vendor's compiler must report
   failure as a value.

`std::expected` is C++23. MSVC 19.44 has it, but the brief fixes the language
level at C++20 and the Linux/macOS compilers in the support matrix are not
guaranteed to. Waiting on C++23 for the single most pervasive type in the
codebase is not acceptable.

## Options

1. **`std::expected` and move to C++23.** Widens the compiler requirement for
   every contributor and every CI image, against an explicit constraint.
2. **`tl::expected` (third-party).** Mature, but adds a dependency that appears
   in every public signature in the project, and leaves us with a type we cannot
   extend with ReelForge-specific behaviour.
3. **Project-owned `rf::Result<T, E>`.** More code to own, but it is ~250 lines,
   it can carry ReelForge-specific semantics, and it migrates to `std::expected`
   later by changing one header.

## Decision

Implement **`rf::Result<T, E = rf::Error>`** in `include/rf/core/result.hpp`,
API-shaped like `std::expected` (`has_value`, `value`, `error`, `value_or`,
`and_then`, `map`, `map_error`) so the eventual migration is mechanical.

Specific choices that differ from a naive expected clone:

- **`[[nodiscard]]` on the class**, not just on functions. An ignored fallible
  call is a compile error everywhere, with no per-function annotation to forget.
- **`value()` on a failed Result aborts** (`RF_CHECK`) rather than throwing or
  returning a default. In a video editor, returning a default frame from a
  failed decode is exactly how a decode failure becomes a silently black export
  that the user does not notice until after publishing. There is a test for this
  (`ResultDeath.ValueOnErrorAborts`).
- **`Error` captures `std::source_location`** at construction, so a failure
  surfaced in the UI can be traced to the libav or GPU call that produced it
  without a debugger.
- **`Error::with_context()` is pure** and preserves the original code and origin,
  so adding call-site context never destroys the root cause.
- **Storage is `std::variant<T, E>`** rather than a hand-rolled union. The union
  saves a few bytes and costs correct special-member handling; `Result` is not
  in a hot loop, frames are, and `Result` never holds a frame by value.

`RF_CHECK` is active in **all** configurations including release. An invariant
that only fires in debug is an invariant that ships broken.

## Consequences

- Every fallible function in ReelForge returns `Result<T>`; the codebase gets one
  error idiom rather than three.
- Exceptions are still enabled at compile time (`/EHsc`) because the standard
  library throws; ReelForge code does not throw, and the boundary rule is
  enforced by review until a lint enforces it (tracked in `docs/BACKLOG.md`).
- Migration to `std::expected` when the language baseline moves is a
  find-and-replace plus deleting one header, because the API shapes match.
- `Result<T&>` is deliberately unsupported (static_assert) — reference semantics
  in an error-carrying type invite dangling returns.
