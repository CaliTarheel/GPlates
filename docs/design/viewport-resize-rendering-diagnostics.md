# Design: Keep the projection viewport correctly sized after resize (no code)

## Problem statement

At least two independent GPlates 2.5-era forum reports describe the render region becoming a small rectangle after a window resize/maximize, leaving the rest of the canvas black. Reports describe projection-dependent behavior: resizing with the 3D orthographic view active may clip 3D while 2D still works, and resizing with a 2D projection active may invert which projections work. Reinstalling, restarting, and changing versions did not resolve the problem for those users.

GPlates should render the full **current** drawable widget extent after a resize, maximize/restore, projection switch, and monitor/DPI change. The root cause is *not* established: it could involve viewport/framebuffer sizing, device-pixel ratio, render-target reuse, driver/Qt event timing, or something else. This is a **diagnosis and UX/behavior design**, not a claim that a particular Qt/OpenGL fix is known.

## Public user evidence

- May 24, 2026: [How do I fix broken view](https://discourse.gplates.org/t/how-do-i-fix-broken-view/3257) — 3D orthographic only, with an unexpectedly small drawn region; reinstall/restart did not resolve it.
- May 30, 2026: [Can only see part of viewer, rest is black](https://discourse.gplates.org/t/can-only-see-part-of-viewer-rest-is-black/3263) — after resizing or maximizing, the drawn region remains at the previous size; switching between flat and 3D projections changes which view is affected.
- Both topics showed renewed activity on November 7, 2026 in the forum index. The latest reply text could not be independently inspected during this review, so the new activity is **not** treated as proof of another reproduction or lack of a solution.

Related but not assumed identical: [upstream issue #10](https://github.com/GPlates/GPlates/issues/10), a Qt5/Wayland rendering failure on Ubuntu 24.04.1, with X11/xcb as a workaround. Its symptoms/environment are different and should be tested separately.

## Proposed UX/functionality

1. The viewport always redraws the whole usable 3D/2D canvas after the host window, splitter, dock, or display size changes. Its physical-pixel OpenGL viewport and any framebuffer/render targets track the real canvas size and device pixel ratio, not a cached pre-resize dimension.
2. Switching among 3D orthographic and flat projections must synchronize render target dimensions and redraw immediately; switching projection should not be required as a workaround for resize.
3. If the renderer cannot resize or recreate a target, show a **non-destructive**, actionable diagnostic (such as a small error message with Copy Diagnostics) rather than leaving an unexplained black rectangle.
4. Provide a developer/debug diagnostic record when needed: OS/window system, GPlates and Qt version, OpenGL vendor/renderer/version, current projection, logical widget width/height, physical device-pixel ratio, framebuffer size, GL viewport dimensions, resize/projection event sequence, and whether a framebuffer is complete. Exclude personal project contents and paths.
5. Do not change viewport scientific geometry, model state, saved project data, feature coordinates, camera reconstruction time, or colour schemes when resizing.

A simple user-facing explanation should distinguish a GPlates canvas-size mismatch from an unverified GPU driver issue; avoid recommending deletion of user configuration as the default remedy.

## Scope

- GPlates desktop's projection canvas sizing, viewport/draw target lifecycle and projection-change redraw.
- Window resize, maximize/restore, splitter resizing, high-DPI scale changes, and moving a window between different-DPI monitors where supported by Qt.
- Diagnostic capture and regression test fixtures for common desktop platforms.
- Where reasonable, a one-shot forced redraw/rebuild action for graceful recovery; do not expose unsafe OpenGL toggles without evidence.

## Non-goals

- Replacing the rendering engine, migrating Qt major versions, changing GPU drivers or OS configuration automatically.
- Solving every possible blank-canvas issue, including known Wayland platform-plugin failures, missing OpenGL features, corrupt datasets, or unsupported graphics hardware.
- Redesigning globe controls, camera navigation, projection mathematics, exported screenshots, or application/session persistence.
- Asserting that the forum reports share one root cause. This must be verified.

## Acceptance criteria

- **A1:** On a supported test platform, open the 3D globe and repeatedly resize/maximize/restore GPlates. Its rendered drawable extent matches the *current* canvas with no persistent black strip matching a previous window size.
- **A2:** Repeat with 2D flat projections and switch 2D→3D→2D after resizing; both views fill the active canvas without restart.
- **A3:** Repeat while splitter/dock sizes change and after minimize/restore; no stale framebuffer/GL viewport dimensions.
- **A4:** Test at devicePixelRatio 1.0 and at least one non-integer/high-DPI scale factor; where supported, move between monitors with differing scale factors.
- **A5:** Diagnostic mode records logical and physical canvas/GL viewport/framebuffer dimensions sufficient to distinguish sizing problems from a failure to create an OpenGL context; no model content is included.
- **A6:** Where a render target cannot be rebuilt, user sees a recoverable error and useful diagnostic information rather than silent failure.
- **A7:** No change to reconstructed geometry, session data, project files, or camera view merely because the window is resized.
- **A8:** Tests separately identify Linux Wayland/xcb paths and distinguish upstream #10 from the resize-specific report rather than declaring them the same bug.
- **A9:** A reproducible manual or automated regression fixture documents at least one of the forum failure signatures and confirms it no longer occurs. If it cannot be reproduced, instrument/triage before proposing a functional fix.

## Likely affected components (to confirm during investigation)

- Desktop window and projection-view widget resize/visibility/screen-change events (including `ViewportWindow` and its canvas integration).
- Qt high-DPI logical→physical pixel conversion and OpenGL viewport/scissor updates.
- Projection switching, globe/flat-view render-target creation and framebuffer lifecycle.
- Existing debug logging / graphics capability diagnostics and manual cross-platform render tests.

These are investigation targets, **not** a claim that a particular renderer source file contains the defect.

## Risks, edge cases and open questions

- Different Qt/OpenGL/driver combinations could yield superficially identical black rectangles for different reasons; avoid a brittle one-platform fix.
- Drawing on a stale GL context or reallocating framebuffers on the wrong event/thread might create new crashes.
- Fractional DPI conversion can cause one-pixel seams; tests should accept rounding only where explained.
- Redrawing on *every* event can cause performance regressions or resize loops. Coalesce events where feasible without missing the final physical size.
- Monitor hotplug and Wayland surface sizing may constrain behavior differently from Windows/macOS.
- Can a minimal repro be obtained from the May reporters? Do screenshots expose exact clipping dimensions? Does current development GPlates already behave differently from 2.5?
- Is there an existing manual Force Redraw path to reuse before adding one?

## Suggested implementation approach (future work, not part of this PR)

1. Gather reporter environment details and a minimal clean-session reproduction. Reproduce the two reported projection-dependent resize sequences on GPlates 2.5 and current development builds.
2. Instrument resize/projection/screen-change paths and the GL viewport/framebuffer physical sizes before modifying rendering code. Compare dimensions immediately before and after the black-strip event.
3. Identify the responsible lifecycle edge. Centralize or correct the size synchronization/invalidations at that edge rather than peppering arbitrary repaint calls throughout the UI.
4. Ensure render targets are rebuilt only with a valid context, and projection switches apply the target dimensions before drawing.
5. Add a focused automated test where headless OpenGL support permits; otherwise provide scripted manual regression procedures and capture diagnostics/screenshot comparisons.
6. Validate on Windows, macOS and Linux/xcb, then separately assess Wayland. Track any driver-specific defect with its own issue.

## Duplicate and overlap check (2026-11-08)

Searched open/closed PRs and issues in `CaliTarheel/GPlates` and `GPlates/GPlates` for resize, viewport, black rendering, OpenGL, and HiDPI problems. No direct design or fix for this projection-dependent *window-resize clipping* was found. Upstream [issue #10](https://github.com/GPlates/GPlates/issues/10) concerns Linux Wayland and is **related but not assumed duplicate**.

**This PR is documentation-only. It does not implement a workaround, claim the issue is fixed, or change any application code.**
