# Design: Snap to guide vertices while digitising (no code)

**Status:** Design proposal only; no implementation code. **Target:** CaliTarheel/GPlates, upstream-relevant.

## Problem and user evidence

GPlates can snap *existing* selected vertices to guide vertices through fork PR #13, but creating new polygon/polyline vertices still requires careful hand placement. Users repeatedly ask to place new vertices directly on existing reference features:

- July 17, 2020, with a March 14, 2024 follow-up: [Snapping vertices during digitisation and reconstruction](https://discourse.gplates.org/t/snapping-vertices-during-digitisation-and-reconstruction/148) describes manual scrap-geometry workarounds.
- December 16, 2023: [Various suggestions](https://discourse.gplates.org/t/various-suggestions-by-someone-with-absolutely-no-experience/802) specifically requests selecting a *reference object* while digitising so that the snapping does not target unwanted geometry.
- April 15, 2025: [Some ideas for potential features](https://discourse.gplates.org/t/some-ideas-for-potential-features/1204) describes snapping to the currently active guide feature and point-by-point alignment as a major time cost.

Related but distinct work: fork PR #13 snaps vertices **after** creation; design PR #44 welds near-coincident geometry **after** creation; PR #61 clamps **maximum segment length while placing** a vertex with Shift. None offers creation-time guide-vertex snapping.

## Proposed interaction

In the digitising tool, add an explicit **Snap to Guide Vertices** toggle, **off by default**. Show the active on/off state, the target filter and a user-adjustable threshold in the task panel.

- **Guides:** query visible reconstructed guide geometry at the current reconstruction time, excluding the unfinished geometry and optionally narrowing to a user-selected focused reference feature. Offer an **Active guide only** mode so other visible features cannot steal the snap.
- **Targets:** v1 is *vertex-to-vertex* only. When a cursor click is within the configured spherical separation of one or more eligible guide vertices, show a distinctive live proposed snap marker and the guide identity before placement. Prefer the nearest eligible vertex, using a deterministic tie-breaker when distances are equal. A placement click sets the new vertex to the guide's reconstructed position at the current time.
- **Coordinates and time:** resolve targets at the current reconstruction time, then let the standard digitisation and subsequent feature-creation process transform/save the geometry through its usual reference frame. Changing reconstruction time or guide visibility must invalidate the candidate and preview.
- **No match:** an ordinary click creates the vertex where clicked, with no fabricated target; the preview makes it clear when no match exists.
- **Precedence with Shift:** PR #61 uses **Shift-click** to clamp the new segment's maximum length. Reserve Shift for that existing behavior. While Shift is held, snapping is bypassed and the explicit segment clamp applies. Avoid combining two position-changing constraints silently.
- **Threshold:** accept a project-radius-aware great-circle distance expressed in km (internally angular distance). Reuse the project's resolution metadata when appropriate, but avoid silently adopting a broad 500-km project default as an aggressive snapping range. Start snap mode off; expose the threshold clearly and settle its default during design review.

## Scope

Creation-time point placement in the ordinary digitise polygon/polyline tools; reconstructed **visible** guide vertices; one optional chosen reference feature; live candidate feedback; deterministic candidate selection; project-aware radius and distance.

## Non-goals

No post-edit vertex movement (PR #13), no weld/clustering (PR #44), no segment/edge/intersection snapping, no auto topology creation or geometry repair, no feature metadata changes, no modification of guides, and no implicit snapping while the toggle is off. This design does not introduce a second plate-reference transformation scheme.

## Acceptance criteria

1. With the toggle off, all existing digitise operations—including plain click and Shift-clamp—behave exactly as before.
2. With snapping on, a point clicked near one eligible visible guide vertex is exactly coincident with that reconstructed vertex at the current time, and the preview identified it before committing.
3. Active-guide-only filtering never snaps to a closer point on another feature; invisible/reconstruction-ineligible guides are ignored.
4. When multiple candidates qualify, nearest-distance then documented stable tie-break yields the same result across repeat runs and map/globe views.
5. Polar, antimeridian, near-antipodal and non-Earth planetary radius cases use spherical distances rather than screen-space world distances.
6. A click with no candidate within threshold places the unsnapped point; no hidden movement or ghost candidate persists.
7. Shift-click preserves PR #61's maximum-segment-length clamp and does not also snap.
8. Time changes, tool changes, undo/redo and guide visibility updates invalidate old previews; guides are never modified.
9. New valid polygons/polylines maintain their ordinary minimum-vertex and commit rules; snapping must not silently create duplicate consecutive vertices or a degenerate geometry.

## Likely affected components

Digitise canvas interaction, the digitising task panel/preferences, reconstructed visible-feature vertex lookup/proximity queries, snap-candidate preview rendering, current reconstruction-time invalidation, and interaction tests. Coordinate/reference-frame conversion should reuse existing digitising paths. Exact source paths to be determined in code review.

## Suggested implementation

Build a read-only `GuideVertexCandidateIndex` scoped to the current visible reconstruction snapshot and optional guide feature, ideally factoring reusable candidate enumeration from PR #13 rather than duplicating its vertex scan. A pure candidate selector computes spherical proximity and a stable tie-break. Render its selected candidate as temporary overlay; only the ordinary digitise click commits a vertex. Invalidate/rebuild on reconstruction-time, visibility or guide changes. Keep Shift-clamp mutually exclusive with snap at click dispatch. Unit-test candidate selection and distances; UI-test live preview, ordinary placement, Shift precedence, and undo.

## Risks and open questions

- Crowded map/globe views create ambiguous snap markers; candidate cycling may become a v2 requirement.
- The project default resolution can be far too large as a snap threshold. Which smaller default avoids surprising attraction to distant points?
- Can PR #13's existing guide lookup be factored cleanly while it is still an unmerged child PR?
- Are guide vertices from dynamic topology/network reconstructions stable/identifiable enough to be eligible in v1?
- Should a guide's selected feature ID be persisted within the project or treated as session-only UI state?
- Should crossing/degenerate new segments be blocked, warned about, or left to standard geometry validation? In v1, preserve current validation rather than adding hidden repairs.

## Duplicate review (2026-10-25)

Searched open/closed fork and upstream issues/PRs for digitising-time snapping. PR #13 (post-edit snap), #44 (weld), #51/#61 (segment limit) are related but not duplicates; no creation-time snapping design/implementation PR found. This document contains **no code**.
