# Design: Weld Vertices tool

**Status:** design only — no implementation in this PR.
**Target release:** 2.6.0-dev*-SR1a
**Depends on:** [CaliTarheel/GPlates#5](https://github.com/CaliTarheel/GPlates/pull/5) (multi-vertex
editing: grouped undo commands, spherical lasso, selected-point rendering)

This document exists so the shape of the tool is agreed before any code is written. Every open
question below changes the implementation materially, so guessing at them and amending later is the
expensive path.

---

## The problem

Worldbuilding geometry accumulates vertices that *should* be the same point but are not:

- A plate boundary and its neighbour were digitised independently, so the shared edge has two
  near-coincident vertex chains and a sliver of empty ocean between them.
- Re-digitising or importing a coastline leaves duplicate vertices a few hundred metres apart.
- A split or a manual edit leaves a stub: two vertices close enough to render as one dot but far
  enough apart to produce a visible kink at high zoom.

None of these are fixable today except by deleting vertices one at a time and dragging survivors by
eye, which does not produce exactly-coincident points.

## The one decision that shapes everything else

There are two tools hiding under the phrase "weld vertices", and they share almost no code:

**(A) Intra-geometry weld.** Collapse near-coincident vertices *within a single geometry*. Pure
`GeometryBuilder` work: read the point sequence, cluster, replace. Undo is a grouped
`GeometryBuilderUndoCommand`, which PR #5 already established. Small, self-contained, testable
without a project loaded.

**(B) Inter-feature weld.** Make vertices of *two or more different features* exactly coincident, so
adjacent plates share a boundary rather than approximating one. This is the one that fixes slivers,
and it is the one worldbuilding actually wants. It has no existing home: it edits multiple feature
handles in one operation, needs a multi-feature undo command, and needs a reconstruction-time story
(vertices are stored in present-day coordinates but welded in reconstructed space, so each feature's
points must be reverse-reconstructed through *its own* rotation tree, exactly as Split Plate does in
`SplitPlateOperation.cc`).

**Recommendation:** build (A) first as a standalone, shippable tool, structured so the clustering
and the weld-position policy are reusable; add (B) in a second PR once (A)'s semantics have survived
contact with real editing. Shipping (B) first means designing multi-feature undo and reverse
reconstruction against semantics nobody has used yet.

**Open question 1:** Is that split acceptable, or is (B) the only version worth having?

---

## Threshold: what "within such a distance" means

### Units

Three candidates:

| Unit | Deterministic? | Intuitive? | Notes |
|---|---|---|---|
| Screen pixels | No — zoom-dependent | Very | Same command gives different results at different zoom. Not reproducible, not scriptable. |
| Degrees of arc | Yes | Somewhat | Native to the unit sphere; no planet radius needed. |
| Kilometres | Yes | Very | Needs the planetary radius. The fork already carries per-project planetary radius metadata ([#15](https://github.com/CaliTarheel/GPlates/pull/15)). |

**Recommendation:** kilometres as the entry field, converted to an angular threshold using the
project's planetary radius, with the equivalent arc-degrees shown next to the field so the
conversion is never a surprise. Internally the comparison is angular
(`GPlatesMaths::AngularExtent`), never a chord length or a projected-plane distance.

This matters for non-Earth projects: 5 km on a 2000 km-radius world is a much coarser weld than 5 km
on Earth, and a user who set the radius deliberately should see that reflected.

**Open question 2:** kilometres-with-radius, or plain arc-degrees to avoid coupling the tool to
project metadata?

### Clustering, and the transitivity trap

Given threshold *t*, suppose A–B = 0.8t, B–C = 0.8t, A–C = 1.6t. Do A and C weld?

- **Transitive (union-find).** Yes. Chains collapse. A long string of vertices each slightly closer
  than *t* to the next collapses to a single point — the classic weld surprise that destroys a
  densely-digitised coastline in one click.
- **Non-transitive (pairwise, greedy).** No. Result depends on iteration order, which means it
  depends on vertex index order, which the user cannot see.

Neither is obviously right, and both have a failure mode a user will hit on day one.

**Recommendation:** transitive clustering (order-independent, explainable), plus **a hard cap on
cluster diameter**: a cluster is rejected if its two furthest members exceed some multiple of *t*
(2t is a reasonable default). Rejected clusters are reported in the preview, not silently split.
That kills the runaway-chain case while keeping results independent of vertex ordering.

**Open question 3:** Is a diameter cap the right guard, and what multiple?

### Where the welded vertex lands

- **Spherical centroid** — normalised vector mean. PR #5 already implements exactly this as
  *Average Positions*; reuse it rather than writing a second version.
- **First/lowest-index member** — preserves an existing exact coordinate; arbitrary from the user's
  point of view.
- **Snap to a designated authority** — for the inter-feature case: plate X's boundary is correct,
  plate Y snaps onto it. This is the *right* answer for fixing slivers, and it is meaningless for
  the intra-geometry case.

**Recommendation:** centroid for (A). For (B), snap-to-authority with the authoritative feature
picked explicitly by the user, because "average the two plates" produces a boundary that matches
neither.

---

## Guards

A weld must never produce invalid geometry. These are hard preconditions, checked before anything is
committed, not fixed up afterwards:

- Polygon exterior rings keep ≥ 3 distinct vertices; polylines keep ≥ 2; multipoints keep ≥ 1.
  PR #5 already enforces these counts for group delete — reuse that check.
- An interior ring (hole) must not collapse below 3 vertices, and must not be welded into its own
  exterior ring. If a weld would do either, reject the cluster and say so.
- The result must not self-intersect. Welding two vertices on opposite sides of a narrow neck pinches
  a polygon into a figure-eight. Detecting this properly is real work; if v1 cannot afford full
  self-intersection validation, it must at minimum refuse to weld two vertices that are more than N
  indices apart within the same ring, and say that it did.
- Vertices belonging to a feature used as a topological section: the weld changes the section
  geometry, so resolved topologies must be re-resolved. If v1 does not handle this, the tool must
  **refuse** on such features rather than leaving topologies stale.

**Open question 4:** Is refusing on topological sections acceptable for v1, or is that the main case?

---

## Preview and undo — non-negotiable

Two things are known-unreliable in this codebase and cannot be assumed to work by being wired up:

1. **Undo can be implemented and still silently do nothing.** Split Plate's undo is a confirmed
   defect in SR0 despite `undo()` being present and connected.
2. **Preview frequently does not render**, in several PRs that claim it.

So this tool ships only with:

- A rendered preview: every cluster drawn on the canvas with its members and its proposed welded
  position, and a count ("18 clusters, 47 vertices → 18"), *before* Apply.
- An undo that has been exercised interactively and observed to restore the original vertices — not
  merely compiled.

If the preview cannot be made to render, the tool is not ready, regardless of whether the weld maths
is correct.

---

## Where it lives

- Clustering, threshold conversion, and weld-position policy: a new
  `src/view-operations/WeldVerticesOperation.{h,cc}`, headless and unit-testable.
- Angular comparison: `GPlatesMaths::AngularExtent` / `AngularDistance`.
- Undo: `GeometryBuilderUndoCommands`, following PR #5's grouped-command pattern.
- UI: task-panel controls on the existing geometry-editing workflow for (A). For (B), a
  **World Building** menu entry following Split Plate's precedent
  (`ViewportWindow` + `ViewportWindowUi.ui`).

## Non-goals

- Not a general mesh-repair or topology-cleaning pass.
- No project-wide "weld everything" batch without per-cluster preview.
- Not a snapping mode for live digitising (a separate, useful feature — different design).
- No vertex *insertion* to make two chains correspond; weld only moves and removes.

---

## Questions to settle before code

1. (A) then (B), or (B) only?
2. Kilometres via planetary radius, or arc-degrees?
3. Transitive clustering with a diameter cap — right guard, right multiple?
4. Refuse on topological sections in v1, or handle re-resolution?
5. For (B): must both features be visible/loaded at the same reconstruction time, and what happens
   if they have different Plate IDs and diverge at other times? A weld that is exact at 0 Ma is not
   exact at 200 Ma. Is the tool welding *present-day coordinates* (permanent) or *reconstructed
   positions at the current time* (exact only at that instant)? **This is the subtlest question in
   the document and the one most likely to make the tool useless if answered wrong.**
